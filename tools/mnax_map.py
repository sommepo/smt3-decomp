#!/usr/bin/env python3
"""Derive USA -> Maniax address anchors from the probe objects.

    python tools/mnax_map.py

Every function tools/mnax_probe.py finds in SLPM_654.62 carries relocations.
The object names what each relocated field refers to (symbol + addend), and the
Maniax bytes at the same instruction hold where that thing lives in Maniax. That
gives exact (USA address, Maniax address, name) anchors for code and data with
no USA executable.

Functions the probe found at several addresses (small look-alikes) are then
placed by evidence only: the candidate whose relocated fields agree with the
anchors, or the one directly after/before an already placed neighbour of the
same object. Writes config/mnax/anchors.tsv and config/mnax/functions.tsv."""
from __future__ import annotations

import csv
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parent.parent
DELTA = 0xFF000
GP_MNAX = 0x00552470
SECS = (".text", ".data", ".rodata", ".lit4", ".sdata")


def sext(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def usa_layout():
    """unit -> {section: USA vram start} from the USA splat config."""
    out = defaultdict(dict)
    for line in (ROOT / "config/smt3/SLUS_209.11.yaml").read_text().splitlines():
        m = re.match(r"\s*- \[(0x[0-9A-Fa-f]+), ([.\w]+), ([\w/]+)\]", line)
        if m:
            kind = ".text" if m.group(2) == "c" else m.group(2)
            if kind in SECS:
                out[m.group(3)].setdefault(kind, int(m.group(1), 16) + DELTA)
    return out


def usa_symbols():
    out = {}
    for line in (ROOT / "config/smt3/symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);", line)
        if m:
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


class Unit:
    def __init__(self, name: str, path: Path, layout: dict, usa: dict):
        self.name, self.layout, self.usa = name, layout, usa
        elf = ELFFile(path.open("rb"))
        self.names = [s.name for s in elf.iter_sections()]
        self.syms = list(elf.get_section_by_name(".symtab").iter_symbols())
        self.tidx = self.names.index(".text")
        self.code = elf.get_section_by_name(".text").data()
        # gp-relative fields against local symbols are stored relative to the object's gp0.
        self.gp0 = struct.unpack_from("<I", elf.get_section_by_name(".reginfo").data(), 20)[0]
        self.funcs = sorted((s["st_value"], s["st_size"], s.name) for s in self.syms
                            if s["st_info"]["type"] == "STT_FUNC" and s["st_shndx"] == self.tidx and s["st_size"])
        self.byoff = {v: n for v, _, n in self.funcs}
        self.clean = "INCLUDE_" not in (ROOT / "src/smt3" / (name + ".c")).read_text(errors="replace")
        self.rels = defaultdict(list)       # function offset -> relocations inside it
        starts = [v for v, _, _ in self.funcs]
        import bisect
        for sec in elf.iter_sections():
            if isinstance(sec, RelocationSection) and sec["sh_info"] == self.tidx:
                for rel in sorted(sec.iter_relocations(), key=lambda r: r["r_offset"]):
                    i = bisect.bisect_right(starts, rel["r_offset"]) - 1
                    if i >= 0 and rel["r_offset"] < starts[i] + self.funcs[i][1]:
                        self.rels[starts[i]].append((rel["r_offset"], rel["r_info_type"], rel["r_info_sym"]))

    def resolve(self, sym, addend, defined, skipped):
        """(USA address or None, name) of sym + addend."""
        shndx = sym["st_shndx"]
        if isinstance(shndx, int):
            sec = self.names[shndx]
            if sec == ".text":
                name = self.byoff.get(sym["st_value"] + addend)
                if name is None or name not in self.usa:
                    skipped["local text reference not at a known function"] += 1
                    return None, ""
                return self.usa[name], name
            if sym["st_info"]["type"] != "STT_SECTION" and sym.name in self.usa:
                return self.usa[sym.name] + addend, "" if addend else sym.name
            if sec not in self.layout or not self.clean:
                skipped["local data in a unit with fallbacks" if sec in self.layout else sec] += 1
                return None, ""
            return self.layout[sec] + sym["st_value"] + addend, ""
        if shndx == "SHN_UNDEF":
            base = self.usa.get(sym.name, defined.get(sym.name))
            if base is None and re.fullmatch(r"(?:D|func)_[0-9A-F]{8}", sym.name):
                base = int(sym.name.split("_")[1], 16)      # splat's automatic USA name
            if base is None:
                skipped["undef:" + sym.name] += 1
                return None, ""
            return base + addend, "" if addend else sym.name
        skipped[str(shndx)] += 1
        return None, ""

    def references(self, start: int, at: int, word, defined, skipped):
        """(USA address, Maniax address, name) for each relocation of the function
        at object offset `start` when it sits at Maniax address `at`."""
        last_hi = {}
        for off, kind, idx in self.rels[start]:
            wo = struct.unpack_from("<I", self.code, off)[0]
            if kind == 5:
                last_hi[idx] = (off, wo)
                continue
            wm = word(at + off - start)
            if kind == 4:
                addend, value = (wo & 0x3FFFFFF) << 2, (wm & 0x3FFFFFF) << 2
            elif kind == 6:
                hi = last_hi.get(idx)
                if hi is None:
                    skipped["lo16 without hi16"] += 1
                    continue
                hm = word(at + hi[0] - start)
                addend = ((hi[1] & 0xFFFF) << 16) + sext(wo & 0xFFFF)
                value = ((hm & 0xFFFF) << 16) + sext(wm & 0xFFFF)
            elif kind in (7, 8):
                addend, value = sext(wo & 0xFFFF), GP_MNAX + sext(wm & 0xFFFF)
                if isinstance(self.syms[idx]["st_shndx"], int):
                    addend += self.gp0
            else:
                continue
            base, name = self.resolve(self.syms[idx], addend, defined, skipped)
            if base is not None:
                yield base, value, name, f"{self.name}+0x{off:X} type {kind}"


def main() -> None:
    raw = (ROOT / "orig/mnax/SLPM_654.62").read_bytes()
    word = lambda vram: struct.unpack_from("<I", raw, vram - DELTA)[0]
    layout, usa = usa_layout(), usa_symbols()
    probe = list(csv.DictReader((ROOT / "config/mnax/probe.tsv").open(), delimiter="\t"))
    cands = defaultdict(dict)
    for r in probe:
        cands[r["unit"]][r["function"]] = [int(a, 16) for a in r["target"].split(",")] if r["target"] != "-" else []

    units = {}
    defined = {}
    for name in layout:
        path = ROOT / "build/probe" / (name + ".o")
        if path.exists():
            u = units[name] = Unit(name, path, layout[name], usa)
            if u.clean:
                for s in u.syms:
                    if s["st_info"]["bind"] == "STB_GLOBAL" and isinstance(s["st_shndx"], int):
                        sec = u.names[s["st_shndx"]]
                        if sec in u.layout and sec != ".text":
                            defined[s.name] = u.layout[sec] + s["st_value"]

    anchors, src = {}, {}     # USA address -> Maniax address
    placed = defaultdict(dict)  # unit -> function offset -> Maniax address
    conflicts = []
    skipped = defaultdict(int)

    def add(u, m, why):
        if anchors.setdefault(u, m) != m:
            conflicts.append((u, anchors[u], src[u], m, why))
            return False
        src.setdefault(u, why)
        return True

    def place(unit, start, name, at):
        placed[unit.name][start] = at
        if name in usa:
            add(usa[name], at, f"{unit.name} function")
        for base, value, _, why in unit.references(start, at, word, defined, skipped):
            add(base, value, why)

    for unit in units.values():
        for start, size, name in unit.funcs:
            c = cands[unit.name].get(name, [])
            if len(c) == 1:
                place(unit, start, name, c[0])

    taken = {m for u in placed.values() for m in u.values()}
    quiet = defaultdict(int)
    rounds = 0
    while True:
        rounds += 1
        progress = 0
        for unit in units.values():
            for i, (start, size, name) in enumerate(unit.funcs):
                if start in placed[unit.name]:
                    continue
                c = [a for a in cands[unit.name].get(name, []) if a not in taken]
                if not c:
                    continue
                pick = None
                # 1. the anchor for this function's own USA address
                if name in usa and anchors.get(usa[name]) in c:
                    pick = anchors[usa[name]]
                # 2. directly after the previous / before the next function of the object
                if pick is None and i and unit.funcs[i - 1][0] in placed[unit.name]:
                    p, ps, _ = unit.funcs[i - 1]
                    if start - (p + ps) < 8:
                        want = placed[unit.name][p] + (start - p)
                        pick = want if want in c else None
                if pick is None and i + 1 < len(unit.funcs) and unit.funcs[i + 1][0] in placed[unit.name]:
                    nx = unit.funcs[i + 1][0]
                    if nx - (start + size) < 8:
                        want = placed[unit.name][nx] - (nx - start)
                        pick = want if want in c else None
                # 3. the only candidate whose references all agree with known anchors
                if pick is None:
                    ok = []
                    for a in c:
                        refs = list(unit.references(start, a, word, defined, quiet))
                        known = [(b, v) for b, v, _, _ in refs if b in anchors]
                        if known and all(anchors[b] == v for b, v in known):
                            ok.append(a)
                    if len(ok) == 1:
                        pick = ok[0]
                if pick is not None:
                    place(unit, start, name, pick)
                    taken.add(pick)
                    progress += 1
        if not progress:
            break

    out = ROOT / "config/mnax"
    with (out / "anchors.tsv").open("w") as fh:
        fh.write("usa\tmnax\tdelta\tsource\n")
        for u in sorted(anchors):
            fh.write(f"0x{u:08X}\t0x{anchors[u]:08X}\t{anchors[u] - u}\t{src[u]}\n")
    tally = defaultdict(lambda: [0, 0])
    with (out / "functions.tsv").open("w") as fh:
        fh.write("unit\tfunction\tsize\tusa\tmnax\n")
        for unit in units.values():
            for start, size, name in unit.funcs:
                at = placed[unit.name].get(start)
                state = "placed" if at is not None else "ambiguous" if cands[unit.name].get(name) else "miss"
                tally[state][0] += 1
                tally[state][1] += size
                u = usa.get(name)
                fh.write(f"{unit.name}\t{name}\t{size}\t{f'0x{u:08X}' if u else '-'}\t"
                         f"{f'0x{at:08X}' if at is not None else state}\n")
    total = sum(v[1] for v in tally.values())
    for k, (n, b) in sorted(tally.items()):
        print(f"{k:9s} {n:5d} functions {b:8d} bytes {100 * b / total:6.2f}%")
    print(f"{len(anchors)} anchors, {len(conflicts)} conflicts, {rounds} rounds")
    for u, m0, w0, m1, w1 in conflicts[:20]:
        print(f"  conflict 0x{u:08X}: 0x{m0:08X} ({w0}) vs 0x{m1:08X} ({w1})", file=sys.stderr)
    for k, v in sorted(skipped.items(), key=lambda kv: -kv[1])[:8]:
        print(f"  skipped {v:5d} {k}", file=sys.stderr)


if __name__ == "__main__":
    main()
