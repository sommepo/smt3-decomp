#!/usr/bin/env python3
"""Generate the Maniax (SLPM_654.62) version target from the USA one.

    python tools/mnax_probe.py && python tools/mnax_map.py && python tools/mnax_config.py

Reads the USA split config, the anchors and placements written by mnax_map.py and
the probe objects, and writes

    config/mnax/SLPM_654.62.yaml   split config
    config/mnax/symbol_addrs.txt   USA names at Maniax addresses
    config/mnax/units.tsv          every USA unit and why it is or is not built from C
    src/mnax/                      generated copy of the units built from C

A unit is built from C only when every section of its object is found in Maniax
byte for byte (relocated fields aside). Everything else is disassembled from the
Maniax executable, so the image stays byte-identical while units move over."""
from __future__ import annotations

import bisect
import csv
import re
import shutil
import struct
import sys
from collections import defaultdict
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parent.parent
# The USA sources to derive from; MNAX_SRC points at a snapshot while src/smt3 is being edited.
SRC = Path(__import__("os").environ.get("MNAX_SRC", ROOT / "src/smt3"))
DELTA = 0xFF000
SERIAL = "SLPM_654.62"
SECS = (".text", ".data", ".rodata", ".lit4", ".sdata")
BLOB = {".text": "asm", ".data": "data", ".rodata": "rodata", ".lit4": "lit4", ".sdata": "sdata"}
REL_MASK = {2: 0, 4: 0xFC000000, 5: 0xFFFF0000, 6: 0xFFFF0000, 7: 0xFFFF0000, 8: 0xFFFF0000}


def read_usa_yaml():
    """Ordered (rom, type, name) subsegments of the USA main segment."""
    out = []
    for line in (ROOT / "config/smt3/SLUS_209.11.yaml").read_text().splitlines():
        m = re.match(r"\s*- \[(0x[0-9A-Fa-f]+), ([.\w]+), ([\w/]+)\]", line)
        if m and m.group(3) not in ("elf_header", "elf_trailer"):
            out.append((int(m.group(1), 16), m.group(2), m.group(3)))
    return out


class Map:
    """Piecewise USA -> Maniax address map from the anchors."""

    def __init__(self):
        rows = csv.DictReader((ROOT / "config/mnax/anchors.tsv").open(), delimiter="\t")
        self.pairs = sorted((int(r["usa"], 16), int(r["mnax"], 16)) for r in rows)
        self.keys = [u for u, _ in self.pairs]

    def exact(self, usa):
        i = bisect.bisect_left(self.keys, usa)
        return self.pairs[i][1] if i < len(self.keys) and self.keys[i] == usa else None

    def around(self, usa):
        i = bisect.bisect_right(self.keys, usa)
        lo = self.pairs[i - 1] if i else None
        hi = self.pairs[i] if i < len(self.pairs) else None
        return lo, hi

    def get(self, usa, same_region):
        """Maniax address, or None when the anchors either side disagree."""
        hit = self.exact(usa)
        if hit is not None:
            return hit
        lo, hi = self.around(usa)
        if lo and hi and lo[1] - lo[0] == hi[1] - hi[0] and same_region(lo[0], usa) and same_region(usa, hi[0]):
            return usa + lo[1] - lo[0]
        return None


def skip_noncode(text: str, i: int) -> int:
    """Index after the comment, string or character literal starting at i, else i."""
    if text.startswith("//", i):
        j = text.find("\n", i)
        return len(text) if j < 0 else j
    if text.startswith("/*", i):
        j = text.find("*/", i + 2)
        return len(text) if j < 0 else j + 2
    if text[i] in "\"'":
        q, j = text[i], i + 1
        while j < len(text) and text[j] != q:
            j += 2 if text[j] == "\\" else 1
        return j + 1
    return i


def skip_else_branch(text: str, i: int) -> int:
    """At a line-initial #else/#elif: index of the matching #endif (only the first
    branch of a conditional counts, so both branches opening a brace stay balanced)."""
    if not (text.startswith("#", i) and (i == 0 or text[i - 1] == "\n")):
        return i
    word = re.match(r"#\s*(\w+)", text[i:])
    if not word or word.group(1) not in ("else", "elif"):
        return i
    depth, k = 1, text.find("\n", i)
    while k != -1 and depth:
        m = re.match(r"\n\s*#\s*(\w+)", text[k:])
        if m and m.group(1) in ("if", "ifdef", "ifndef"):
            depth += 1
        elif m and m.group(1) == "endif":
            depth -= 1
            if not depth:
                return k + 1
        k = text.find("\n", k + 1)
    return len(text)


def match_close(text: str, i: int, open_: str, close: str) -> int:
    """Index after the bracket that closes the one at i, or -1."""
    depth = 0
    while i < len(text):
        j = skip_else_branch(text, i)
        if j != i:
            i = j
            continue
        j = skip_noncode(text, i)
        if j != i:
            i = j
            continue
        if text[i] == open_:
            depth += 1
        elif text[i] == close:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def find_definition(text: str, name: str):
    """(start, end) of the C definition of function `name`, or None."""
    for m in re.finditer(r"^[A-Za-z_][^\n;{}()#=]*?\b" + re.escape(name) + r"\s*\(", text, re.M):
        close = match_close(text, m.end() - 1, "(", ")")
        if close < 0:
            continue
        k = close
        while k < len(text):
            j = skip_noncode(text, k)
            if j != k:
                k = j
            elif text[k].isspace():
                k += 1
            else:
                break
        if k < len(text) and text[k] == "{":
            end = match_close(text, k, "{", "}")
            if end > 0:
                return m.start(), end
    return None


def named_set(lines):
    return {line.split(" = ")[0] for line in lines}


# Lines the split tools place from the split itself; the USA placement does not apply.
DATA_LINES = re.compile(r"^(?:INCLUDE_(?:RODATA|SDATA)\([^;]*\);|SDATA_PAD\(\d+\);|SDATA_RESUME\(\);)\n\n?", re.M)


def adapt_source(text: str, unit: str, keep_asm, absent, extra):
    """The unit's source for Maniax: `keep_asm` functions become INCLUDE_ASM, `absent`
    ones go, and `extra` [(after function, name)] Maniax-only functions are included.
    Returns (text, None) or (None, reason)."""
    line = lambda name: f'INCLUDE_ASM(const s32, "{unit}", {name});'
    for name in keep_asm:
        span = find_definition(text, name)
        if span is None:
            return None, f"cannot isolate {name} in the source"
        head = text[span[0]:text.index("{", span[0])].strip()
        head = re.sub(r"^(?:static\s+|inline\s+)+", "", head)
        head = re.sub(r"(?:/\*.*?\*/|//[^\n]*)", "", head, flags=re.S).strip()
        text = text[:span[0]] + head + ";\n" + line(name) + text[span[1]:]
    for name in absent:
        span = find_definition(text, name)
        if span is not None:
            text = text[:span[0]] + text[span[1]:]
        text = re.sub(r"^INCLUDE_ASM\([^,]+,[^,]+,\s*" + re.escape(name) + r"\);\n", "", text, flags=re.M)
    for after, name in reversed(extra):
        m = re.search(r"^INCLUDE_ASM\([^,]+,[^,]+,\s*" + re.escape(after) + r"\);", text, re.M)
        span = (m.start(), m.end()) if m else find_definition(text, after)
        if span is None:
            return None, f"cannot place Maniax-only {name} after {after}"
        text = text[:span[1]] + "\n\n" + line(name) + text[span[1]:]
    return text, None


def near(amap, usa, same_region):
    """Maniax address of a section boundary. Where the anchors either side disagree,
    take the delta of the first anchor at or after it: a unit's first referenced
    object is usually close to its start, so the change lies before the boundary.
    A wrong guess fails the checksum and tools/mnax_settle.py demotes the unit."""
    hit = amap.get(usa, same_region)
    if hit is not None:
        return hit
    lo, hi = amap.around(usa - 1)
    if hi and same_region(usa, hi[0]) and hi[0] - usa < 0x1000:
        return usa + hi[1] - hi[0]
    if lo and same_region(lo[0], usa) and usa - lo[0] < 0x1000:
        return usa + lo[1] - lo[0]
    return None


def main() -> None:
    raw = (ROOT / "orig/mnax" / SERIAL).read_bytes()
    target = ELFFile((ROOT / "orig/mnax" / SERIAL).open("rb"))
    msec = {s.name: (s["sh_addr"], s["sh_size"]) for s in target.iter_sections() if s["sh_addr"]}
    amap = Map()
    usa_yaml = read_usa_yaml()

    # USA section regions, from the fixed entries of the USA config.
    fixed = {name: rom + DELTA for rom, kind, name in usa_yaml}
    usa_region = [
        (".text", 0x100000, fixed["text_tail_padding"]),
        (".data", fixed["data"], fixed["eh_frame"]),
        (".rodata", fixed["rodata_0052DC80"], None),
        (".lit4", None, fixed["lit4_pad"]),
        (".sdata", None, None),
    ]
    order = defaultdict(list)       # section -> [(usa start, unit)]
    for rom, kind, name in usa_yaml:
        sec = ".text" if kind in ("c", "asm") else kind if kind in SECS else None
        if sec and not name.startswith(("hand/",)):
            order[sec].append((rom + DELTA, name))
    lit4_start = order[".lit4"][0][0]
    sdata_start = order[".sdata"][0][0]
    bounds = {".text": (0x100000, fixed["text_tail_padding"]), ".data": (fixed["data"], fixed["eh_frame"]),
              ".rodata": (fixed["rodata_0052DC80"], lit4_start), ".lit4": (lit4_start, sdata_start),
              ".sdata": (sdata_start, 0x551B80), ".sbss": (0x551B80, 0x552180), ".bss": (0x552180, 0x5A0400)}

    def region(a):
        for name, (lo, hi) in bounds.items():
            if lo <= a < hi:
                return name
        return None

    same = lambda a, b: region(a) is not None and region(a) == region(b)

    placed = defaultdict(dict)
    missed = defaultdict(list)      # compiled from C but not found in Maniax
    for r in csv.DictReader((ROOT / "config/mnax/functions.tsv").open(), delimiter="\t"):
        placed[r["unit"]][r["function"]] = int(r["mnax"], 16) if r["mnax"].startswith("0x") else None
        if r["mnax"] == "miss":
            missed[r["unit"]].append(r["function"])
    # Every USA function's Maniax counterpart (tools/mnax_funcs.py).
    funcs, maniax_only = [], []
    for r in csv.DictReader((ROOT / "config/mnax/funcs.tsv").open(), delimiter="\t"):
        if r["usa"] == "-":
            maniax_only.append((int(r["mnax"], 16), r["name"]))
        else:
            funcs.append((int(r["usa"], 16), r["name"], int(r["mnax"], 16) if r["mnax"].startswith("0x") else r["mnax"]))
    funcs.sort()
    func_keys = [a for a, _, _ in funcs]
    func_at = {name: at for _, name, at in funcs}

    # ---- data placement by the unit's own named objects -----------------------------
    # The previous round's symbol table holds Maniax addresses for named data. A unit's
    # data section starts where its named objects say it does (majority vote), measured
    # in its last full build object when there is one (which includes the assembly
    # pieces), else in the probe object for units without fallbacks.
    prev_syms = {}
    prev = ROOT / "config/mnax/symbol_addrs.txt"
    if prev.exists():
        for line in prev.read_text().splitlines():
            mm = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);", line)
            if mm:
                prev_syms[mm.group(1)] = int(mm.group(2), 16)

    def named_place(unit, secname, clean):
        from collections import Counter
        for path in (ROOT / "build/mnax/src/mnax" / (unit + ".o"), ROOT / "build/probe" / (unit + ".o")):
            if path.name and "probe" in str(path) and not clean:
                continue
            if not path.exists() or path.read_bytes()[:4] != b"\x7fELF":
                continue
            e = ELFFile(path.open("rb"))
            sec = e.get_section_by_name(secname)
            if sec is None or not sec["sh_size"]:
                return None
            idx = e.get_section_index(secname)
            votes = Counter(prev_syms[sy.name] - sy["st_value"] for sy in e.get_section_by_name(".symtab").iter_symbols()
                            if sy["st_shndx"] == idx and sy.name in prev_syms
                            and sy["st_info"]["type"] in ("STT_OBJECT", "STT_NOTYPE") and not sy.name.startswith("."))
            if not votes:
                return None
            (base, n), = votes.most_common(1)
            if n * 2 <= sum(votes.values()):
                return None
            size = (sec["sh_size"] + 7) & ~7 if secname == ".rodata" else (sec["sh_size"] + 3) & ~3
            return base, size
        return None

    # ---- which units build from C ------------------------------------------------
    usa_start = defaultdict(dict)
    for sec, rows in order.items():
        for a, unit in rows:
            usa_start[unit][sec] = a
    # A unit's section runs to the next USA entry: that covers its tail padding too.
    usa_extent = defaultdict(dict)
    flat = [(rom + DELTA, ".text" if kind in ("c", "asm") else kind, name) for rom, kind, name in usa_yaml]
    for (a, sec, unit), (b, _, _) in zip(flat, flat[1:] + [(0x551B54, "", "")]):
        if sec in SECS:
            usa_extent[unit][sec] = b - a
    units = [name for rom, kind, name in usa_yaml if kind == "c"]
    good, verdict, sources = {}, {}, {}
    # Maniax start of each USA .text entry: its first function that exists in Maniax.
    text_end_m = 0x100000 + msec[".text"][1]
    mregion_end = {".data": msec[".eh_frame"][0], ".rodata": msec[".lit4"][0], ".lit4": msec[".sdata"][0],
                   ".sdata": (msec[".sdata"][0] + msec[".sdata"][1]) & ~3}
    text_entries = sorted(order[".text"]) + [(fixed["text_tail_padding"], None)]
    text_start, text_next = {}, {}
    firsts = []
    for (a, unit), (b, _) in zip(text_entries, text_entries[1:]):
        lo, hi = bisect.bisect_left(func_keys, a), bisect.bisect_left(func_keys, b)
        firsts.append(next((at for _, _, at in funcs[lo:hi] if isinstance(at, int)), None))
    firsts.append(text_end_m)
    for i, (a, unit) in enumerate(text_entries[:-1]):
        text_start[unit], text_next[unit] = firsts[i], firsts[i + 1]
    demote = ROOT / "config/mnax/demote.txt"
    demoted = dict(line.split("\t", 1) for line in demote.read_text().splitlines() if "\t" in line) \
        if demote.exists() else {}
    for unit in units:
        src = SRC / (unit + ".c")
        obj = ROOT / "build/probe" / (unit + ".o")
        fallbacks = bool(re.search(r"^\s*INCLUDE_(ASM|RODATA|SDATA)\(", src.read_text(errors="replace"), re.M))
        if unit in demoted:
            verdict[unit] = "demoted: " + demoted[unit]
            continue
        if not obj.exists():
            verdict[unit] = "does not compile standalone"
            continue
        elf = ELFFile(obj.open("rb"))
        names = [s.name for s in elf.iter_sections()]
        syms = list(elf.get_section_by_name(".symtab").iter_symbols())
        if any(s["st_shndx"] == "SHN_COMMON" for s in syms):
            verdict[unit] = "defines common symbols"
            continue
        starts, why = {}, None
        if fallbacks or missed[unit]:
            # The object lacks the pieces that stay assembly, so its sections cannot be
            # compared whole. Admit the unit when both ends of each section are known in
            # Maniax; the build's checksum is the real test, and tools/mnax_settle.py
            # lists a unit that fails it in demote.txt.
            t0 = usa_start[unit][".text"]
            lo = bisect.bisect_left(func_keys, t0)
            hi = bisect.bisect_left(func_keys, t0 + usa_extent[unit][".text"])
            mine = funcs[lo:hi]
            if any(at == "unknown" for _, _, at in mine):
                why = "a function's Maniax address is unknown"
            present = [(at, name) for _, name, at in mine if isinstance(at, int)]
            m_text = text_start.get(unit)
            m_next = text_next.get(unit)
            if why is None and (m_text is None or m_next is None or m_next <= m_text):
                why = ".text bounds unknown in Maniax"
            if why is None:
                starts[".text"] = (m_text, m_next - m_text, t0)
                for name, u0 in usa_start[unit].items():
                    if name == ".text":
                        continue
                    size = usa_extent[unit][name]
                    placed_by_name = named_place(unit, name, clean=False)
                    if placed_by_name is not None:
                        starts[name] = (placed_by_name[0], placed_by_name[1], u0)
                        continue
                    m0 = near(amap, u0, same)
                    m1 = mregion_end[name] if u0 + size == bounds[name][1] else near(amap, u0 + size, same)
                    if m0 is None or m1 is None or m1 <= m0:
                        why = f"{name} bounds unknown in Maniax"
                        break
                    size = m1 - m0
                    # The object of the last build (even from a round where the unit was
                    # demoted) knows the unit's real Maniax size better than the anchors.
                    built = ROOT / "build/mnax/src/mnax" / (unit + ".o")
                    if name != ".rodata" and built.exists() and built.read_bytes()[:4] == b"\x7fELF":
                        bsec = ELFFile(built.open("rb")).get_section_by_name(name)
                        if bsec is not None and bsec["sh_size"]:
                            size = (bsec["sh_size"] + 3) & ~3
                    if name == ".rodata":
                        # Real end of the object's .rodata (see the note below on blob
                        # alignment): from the last build's object when there is one,
                        # else the last non-zero byte, which a rebuild then corrects.
                        body = raw[m0 - DELTA:m0 - DELTA + size].rstrip(b"\0")
                        real = (len(body) + 8) & ~7
                        built = ROOT / "build/mnax/src/mnax" / (unit + ".o")
                        if built.exists() and built.read_bytes()[:4] == b"\x7fELF":   # a failed compile leaves a stub
                            sec = ELFFile(built.open("rb")).get_section_by_name(".rodata")
                            if sec is not None and (sec["sh_size"] + 7) & ~7 >= len(body):
                                real = (sec["sh_size"] + 7) & ~7
                        size = min(size, real)
                    starts[name] = (m0, size, u0)
            if why is None:
                keep = [n for n in missed[unit] if isinstance(func_at.get(n), int)]
                gone = [name for _, name, at in mine if at == "absent"]
                lost = [n for n in missed[unit] if func_at.get(n) not in ("absent",) and not isinstance(func_at.get(n), int)]
                extra = []
                for at, name in maniax_only:
                    if m_text <= at < m_next:
                        before = [n for a, n in present if a < at]
                        if not before:
                            why = f"Maniax-only {name} precedes the unit's first function"
                            break
                        extra.append((before[-1], name))
                if lost:
                    why = f"cannot locate {lost[0]} in Maniax"
                if why is None:
                    text, why = adapt_source(src.read_text(errors="replace"), unit, keep, gone, extra)
                    if why is None:
                        sources[unit] = text
            if why:
                verdict[unit] = why
            else:
                good[unit] = starts
                kept = len(missed[unit])
                verdict[unit] = "C with fallbacks" + (f", {kept} functions kept as Maniax assembly" if kept else "")
            continue
        for sec in elf.iter_sections():
            if sec.name not in SECS + (".bss", ".sbss") or not sec["sh_size"]:
                continue
            if sec.name not in usa_start[unit]:
                why = f"object has {sec.name} but the USA config gives it none"
                break
            data = bytearray(sec.data())
            mask = [0xFFFFFFFF] * (len(data) // 4)
            rel = elf.get_section_by_name(".rel" + sec.name)
            if isinstance(rel, RelocationSection):
                for r in rel.iter_relocations():
                    mask[r["r_offset"] // 4] &= REL_MASK.get(r["r_info_type"], 0)
            u0 = usa_start[unit][sec.name]
            votes = set()
            if sec.name == ".text":
                tidx = names.index(".text")
                for s in syms:
                    if s["st_info"]["type"] == "STT_FUNC" and s["st_shndx"] == tidx and s["st_size"] \
                            and placed[unit].get(s.name) is not None:
                        votes.add(placed[unit][s.name] - s["st_value"])
            else:
                i = bisect.bisect_left(amap.keys, u0)
                while i < len(amap.keys) and amap.keys[i] < u0 + sec["sh_size"]:
                    votes.add(amap.pairs[i][1] - (amap.keys[i] - u0))
                    i += 1
                if not votes:
                    guess = amap.get(u0, same)
                    if guess is not None:
                        votes.add(guess)
            if len(votes) != 1 and sec.name != ".text":
                byname = named_place(unit, sec.name, clean=True)
                if byname is not None:
                    votes = {byname[0]}
            if len(votes) != 1:
                why = f"{sec.name} start {'unknown' if not votes else 'ambiguous'}"
                break
            m0 = votes.pop()
            there = raw[m0 - DELTA:m0 - DELTA + len(data)]
            n = len(data) // 4
            a = struct.unpack(f"<{n}I", bytes(data[:n * 4]))
            b = struct.unpack(f"<{n}I", there[:n * 4])
            if any((x ^ y) & k for x, y, k in zip(a, b, mask)) or data[n * 4:] != there[n * 4:]:
                why = f"{sec.name} bytes differ"
                break
            # A splat blob is aligned to 8, a C unit's .rodata to 16: a blob that follows
            # must start at the object's real end, or the padding before it is lost.
            # The unit occupies what its Maniax object occupies; the USA extent is wrong
            # wherever the Japanese data is longer or shorter. Whatever follows up to the
            # next unit (padding, other data) becomes a blob.
            size = (sec["sh_size"] + 7) & ~7 if sec.name == ".rodata" else (sec["sh_size"] + 3) & ~3
            if sec.name == ".text":
                size = sec["sh_size"]
            starts[sec.name] = (m0, size, u0)
        if why is None and set(usa_start[unit]) - set(starts):
            why = "USA config has " + ",".join(sorted(set(usa_start[unit]) - set(starts))) + " the object lacks"
        if why:
            verdict[unit] = why
        else:
            good[unit] = starts
            verdict[unit] = "C"

    # Guessed bounds can collide. Drop the less certain unit of an overlapping pair.
    while True:
        clash = None
        for sec in SECS:
            entries = sorted((good[u][sec][0], good[u][sec][1], u) for u in good if sec in good[u])
            for (s0, z0, a), (s1, z1, b) in zip(entries, entries[1:]):
                if s1 < s0 + z0:
                    clash = (b if verdict[b] != "C" else a, a if verdict[b] != "C" else b, sec)
                    break
            if clash:
                break
        if not clash:
            break
        verdict[clash[0]] = f"{clash[2]} bounds overlap {clash[1]}"
        del good[clash[0]]
        sources.pop(clash[0], None)

    # ---- split config ------------------------------------------------------------
    mregion = {
        ".text": (0x100000, 0x100000 + msec[".text"][1]),
        ".data": (msec[".data"][0], msec[".eh_frame"][0]),
        ".rodata": (msec[".rodata"][0], msec[".lit4"][0]),
        ".lit4": (msec[".lit4"][0], msec[".sdata"][0]),
        ".sdata": (msec[".sdata"][0], (msec[".sdata"][0] + msec[".sdata"][1]) & ~3),
    }
    lines = []
    emit = lambda vram, kind, name: lines.append(f"      - [0x{vram - DELTA:X}, {kind}, {name}]")
    for sec in SECS:
        lo, hi = mregion[sec]
        entries = sorted((good[u][sec][0], good[u][sec][1], u) for u in good if sec in good[u])
        cursor = lo
        for start, size, unit in entries + [(hi, 0, None)]:
            if start < cursor:
                sys.exit(f"{sec}: {unit} at 0x{start:X} overlaps the previous unit ending 0x{cursor:X}")
            pad = raw[cursor - DELTA:start - DELTA]
            # Zeros up to a C unit's 16-byte .rodata alignment are the linker's, not a blob.
            if start > cursor and not (sec == ".rodata" and unit and not any(pad) and (cursor + 15) & ~15 == start):
                emit(cursor, BLOB[sec], f"{sec[1:]}_{cursor:08X}")
            if unit:
                emit(start, "c" if sec == ".text" else sec, unit)
                cursor = start + size
        if sec == ".text":
            emit(mregion[".text"][1], "pad", "text_tail_padding")
            emit(msec[".vutext"][0], "textbin", "vutext")
            emit(msec[".ctors"][0], "databin", "ctors_dtors_reginfo")
        if sec == ".data":
            emit(msec[".eh_frame"][0], "databin", "eh_frame")
            emit(msec[".vudata"][0], "databin", "vudata")
    sdata_end = mregion[".sdata"][1]
    bss_size = msec[".vubss"][0] - sdata_end
    usa_text = (ROOT / "config/smt3/SLUS_209.11.yaml").read_text()
    head = usa_text[:usa_text.index("segments:")]
    head = head.replace("SLUS_209.11", SERIAL).replace("(USA)", "Maniax (Japan)")
    head = re.sub(r"(config|orig|build|asm|src|assets)/smt3", r"\1/mnax", head)
    head = re.sub(r"sha1: \w+", "sha1: a2dee659dcf6111f4e4ff82b6c5b3ebe086d9c13", head)
    head = re.sub(r"gp_value: 0x\w+", "gp_value: 0x00552470", head)
    body = [
        "# Generated by tools/mnax_config.py from config/smt3/SLUS_209.11.yaml; do not edit.",
        head.rstrip(), "", "segments:", "  - [0x0, bin, elf_header]", "",
        "  - name: main", "    type: code", "    align: 1", "    start: 0x1000", "    vram: 0x100000",
        f"    bss_size: 0x{bss_size:X}", "    subsegments:", *lines,
        f"      - {{ start: 0x{sdata_end - DELTA:X}, type: sbss, vram: 0x{msec['.sbss'][0]:08X}, name: sbss }}",
        f"      - {{ start: 0x{sdata_end - DELTA:X}, type: bss, vram: 0x{msec['.bss'][0]:08X}, name: bss }}",
        "", f"  - [0x{sdata_end - DELTA:X}, bin, elf_trailer]", f"  - [0x{len(raw):X}]", "",
    ]
    cfg = ROOT / "config/mnax"
    (cfg / f"{SERIAL}.yaml").write_text("\n".join(body))

    # ---- symbols -----------------------------------------------------------------
    size_of = {}
    for u, fs in placed.items():
        for name, at in fs.items():
            if at is not None:
                size_of[name] = True
    out, seen_addr, dropped = [], {}, 0
    for line in (ROOT / "config/smt3/symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);\s*(//.*)?", line)
        if not m:
            continue
        name, usa, attrs = m.group(1), int(m.group(2), 16), m.group(3) or ""
        at = amap.get(usa, same)
        if "type:func" in attrs and name in func_at:
            at = func_at[name] if isinstance(func_at[name], int) else None
        if at is None or at in seen_addr:
            dropped += 1
            continue
        sz = re.search(r"size:(0x[0-9A-Fa-f]+)", attrs)
        if sz:
            end = amap.get(usa + int(sz.group(1), 16), same)
            keep = name in size_of if "type:func" in attrs and amap.exact(usa) is not None and name in size_of \
                else end is not None and end - at == int(sz.group(1), 16)
            if "type:func" in attrs and not keep:
                keep = amap.exact(usa) is None and end is not None and end - at == int(sz.group(1), 16)
            if not keep:
                attrs = re.sub(r"\s*size:0x[0-9A-Fa-f]+", "", attrs)
        seen_addr[at] = name
        out.append(f"{name} = 0x{at:08X}; {attrs}".rstrip())
    # Names that carry a USA address (D_XXXXXXXX, func_XXXXXXXX) are splat's automatic
    # ones in the USA build; the shared sources use them, so Maniax needs them by name.
    # Only the names the C-built units define or reference are needed.
    auto = set()
    for unit in good:
        elf = ELFFile((ROOT / "build/probe" / (unit + ".o")).open("rb"))
        for sym in elf.get_section_by_name(".symtab").iter_symbols():
            if re.fullmatch(r"(?:D|func|jtbl)_[0-9A-F]{8}", sym.name):
                auto.add(sym.name)
        # Fallback functions are not in the probe object; their names are in the source.
        auto.update(re.findall(r"INCLUDE_(?:ASM|RODATA|SDATA)\([^,]+,[^,]+,\s*((?:D|func|jtbl)_[0-9A-F]{8})\)",
                               (SRC / (unit + ".c")).read_text(errors="replace")))
    have = {line.split(" = ")[0] for line in out}
    spans = sorted((u0, u0 + usa_extent[u][sec], m0) for u in good for sec, (m0, size, u0) in good[u].items())
    # USA functions with sizes, for code addresses inside one (labels, fragments).
    usa_fn = []
    for line in (ROOT / "config/smt3/symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);.*type:func.*size:(0x[0-9A-Fa-f]+)", line)
        if m and isinstance(func_at.get(m.group(1)), int):
            usa_fn.append((int(m.group(2), 16), int(m.group(3), 16), func_at[m.group(1)]))
    usa_fn.sort()
    usa_fn_keys = [a for a, _, _ in usa_fn]

    def in_function(usa):
        i = bisect.bisect_right(usa_fn_keys, usa) - 1
        if i >= 0 and usa < usa_fn[i][0] + usa_fn[i][1]:
            return usa_fn[i][2] + usa - usa_fn[i][0]
        return None

    # Every paired function by its own name, including automatic ones the C defines.
    for _, name, at in funcs:
        if isinstance(at, int) and name not in have and at not in seen_addr:
            seen_addr[at] = name
            have.add(name)
            out.append(f"{name} = 0x{at:08X}; // type:func")

    def after_function(usa):
        """Code address past every known function end: the delta of the function before it."""
        i = bisect.bisect_right(func_keys, usa) - 1
        while i >= 0 and not isinstance(funcs[i][2], int):
            i -= 1
        return funcs[i][2] + usa - funcs[i][0] if i >= 0 else None

    carried = {}
    for name in sorted(auto - have):
        usa = int(name.split("_")[1], 16)
        code = bounds[".text"][0] <= usa < bounds[".text"][1]
        at = in_function(usa) if code else None
        if at is None:
            at = next((m0 + usa - lo for lo, hi, m0 in spans if lo <= usa < hi), None)
        if at is None:
            at = amap.get(usa, same)
        if at is None and code:
            at = after_function(usa)
        if at is not None and at not in seen_addr:
            seen_addr[at] = name
            carried[name] = at
            out.append(f"{name} = 0x{at:08X};" + (" // type:func" if name.startswith("func_") else ""))
    for at, name in maniax_only:
        if at not in seen_addr and name not in named_set(out):
            seen_addr[at] = name
            out.append(f"{name} = 0x{at:08X}; // type:func")
    # A carried name can equal the automatic name splat gives a different Maniax
    # address. Those addresses get an explicit name; the list only grows, so a
    # re-split cannot bring the clash back.
    renames = cfg / "renames.txt"
    clash = set(int(x, 16) for x in renames.read_text().split()) if renames.exists() else set()
    named = {line.split(" = ")[0]: int(line.split(" = ")[1][:10], 16) for line in out}
    for path in (ROOT / "asm/mnax").rglob("*.s"):
        for label, at in re.findall(r"^(?:dlabel|glabel|jlabel) ((?:D|func|jtbl)_[0-9A-F]{8})\n\s*/\* (?:[0-9A-F]+ )?([0-9A-F]{8})",
                                    path.read_text(), re.M):
            # Automatic (the name is the address it sits at), yet the C-built units use
            # that name for something else: carried elsewhere, or defined by one of them.
            here = int(at, 16)
            if here == int(label.split("_")[1], 16) and (label in named or label in auto) and named.get(label) != here:
                if label.startswith("jtbl_"):
                    print(f"jump table name clash left alone: {label}", file=sys.stderr)
                else:
                    clash.add(here)
    renames.write_text("".join(f"0x{x:08X}\n" for x in sorted(clash)))
    # Inside a C-built unit the automatic name only appears in its reference
    # disassembly, and a real name there would make include_rodata.py claim it.
    owned = sorted((m0, m0 + size) for u in good for m0, size, _ in good[u].values())
    for x in sorted(clash):
        if x not in seen_addr and not any(lo <= x < hi for lo, hi in owned):
            seen_addr[x] = True
            out.append(f"mnax_{x:08X} = 0x{x:08X};" + (" // type:func" if x < mregion[".text"][1] else ""))
    carried = len(carried)
    print(f"{carried} of {len(auto - have)} address-named symbols carried over")
    (cfg / "symbol_addrs.txt").write_text(
        "// Generated by tools/mnax_config.py: USA names carried to SLPM_654.62 addresses.\n" + "\n".join(out) + "\n")

    for name in ("cflags.txt", "compilers.txt", "asflags.txt"):
        shutil.copyfile(ROOT / "config/smt3" / name, cfg / name)
    (cfg / "reloc_addrs.txt").write_text("// Relocation overrides for SLPM_654.62.\n")

    with (cfg / "units.tsv").open("w") as fh:
        fh.write("unit\tstate\n")
        for unit in units:
            fh.write(f"{unit}\t{verdict[unit]}\n")

    # ---- sources -----------------------------------------------------------------
    dst = ROOT / "src/mnax"
    shutil.rmtree(dst, ignore_errors=True)
    for unit in good:
        (dst / unit).parent.mkdir(parents=True, exist_ok=True)
        text = sources.get(unit) or (SRC / (unit + ".c")).read_text(errors="replace")
        (dst / (unit + ".c")).write_text(text)
    shutil.copytree(SRC / "sdf/vu", dst / "sdf/vu", dirs_exist_ok=True)

    tally = defaultdict(int)
    for unit in units:
        tally[re.sub(r"\d+ functions", "N functions", re.sub(r"(isolate|locate|only|after) \w+", r"\1 X", verdict[unit]))] += 1
    for k, v in sorted(tally.items(), key=lambda kv: -kv[1]):
        print(f"{v:4d} units: {k}")
    text_c = sum(good[u][".text"][1] for u in good if ".text" in good[u])
    print(f"C units own {text_c} of {msec['.text'][1]} .text bytes ({100 * text_c / msec['.text'][1]:.1f}%)")
    print(f"{len(out)} symbols carried over, {dropped} dropped; bss_size 0x{bss_size:X}")


if __name__ == "__main__":
    main()
