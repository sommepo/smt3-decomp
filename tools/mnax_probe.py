#!/usr/bin/env python3
"""Locate every C-matched USA function in another Nocturne build.

    python tools/mnax_probe.py [--elf orig/mnax/SLPM_654.62] [--jobs 6]

Compiles each src/smt3 unit with -DSKIP_ASM (asm fallbacks drop out, so the USA
executable is not needed), masks exactly the instruction fields its relocations
cover, and searches the target build's .text for each function. A function found
at one address compiles from the USA source unchanged in the target build.
Writes config/mnax/probe.tsv: unit, function, size, usa address, status,
target address(es)."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parent.parent
OBJ = ROOT / "build" / "probe"
DELTA = 0xFF000
TEXT_START = 0x00100000
# R_MIPS_26 covers the jump target; the 16-bit forms cover the immediate halfword.
MASKS = {4: 0xFC000000, 5: 0xFFFF0000, 6: 0xFFFF0000, 7: 0xFFFF0000, 8: 0xFFFF0000, 9: 0xFFFF0000}


def compile_unit(src: Path) -> tuple[Path, Path | None, str]:
    out = OBJ / src.relative_to(ROOT / "src" / "smt3").with_suffix(".o")
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists() and out.stat().st_mtime > src.stat().st_mtime:
        return src, out, ""
    r = subprocess.run([str(ROOT / "tools" / "cc.sh"), "-DSKIP_ASM", str(src), "-o", str(out)],
                       capture_output=True, text=True)
    if r.returncode:
        out.unlink(missing_ok=True)
        return src, None, (r.stderr.strip().splitlines() or ["?"])[-1]
    return src, out, ""


def functions(obj: Path):
    """(name, words, masks) for each function in the object's .text, in address order."""
    with obj.open("rb") as fh:
        elf = ELFFile(fh)
        text = elf.get_section_by_name(".text")
        if text is None or text["sh_size"] == 0:
            return []
        tidx = elf.get_section_index(".text")
        data = np.frombuffer(text.data(), dtype="<u4").copy()
        mask = np.full(len(data), 0xFFFFFFFF, dtype=np.uint32)
        for sec in elf.iter_sections():
            if isinstance(sec, RelocationSection) and sec["sh_info"] == tidx:
                for rel in sec.iter_relocations():
                    kind = rel["r_info_type"]
                    if kind not in MASKS:
                        raise SystemExit(f"{obj}: unhandled relocation type {kind}")
                    mask[rel["r_offset"] // 4] &= MASKS[kind]
        syms = []
        for sym in elf.get_section_by_name(".symtab").iter_symbols():
            if sym["st_info"]["type"] == "STT_FUNC" and sym["st_shndx"] == tidx and sym["st_size"]:
                syms.append((sym["st_value"], sym["st_size"], sym.name))
        syms.sort()
        return [(n, data[v // 4:(v + s) // 4], mask[v // 4:(v + s) // 4]) for v, s, n in syms]


def find(text: np.ndarray, words: np.ndarray, mask: np.ndarray) -> np.ndarray:
    n = len(words)
    want = words & mask
    # Anchor on the unmasked word that is rarest to keep verification cheap.
    full = np.flatnonzero(mask == 0xFFFFFFFF)
    cand = None
    for k in full[:6] if len(full) else []:
        hits = np.flatnonzero(text[k:len(text) - n + k + 1] == words[k])
        if cand is None or len(hits) < len(cand):
            cand = hits
        if len(cand) < 64:
            break
    if cand is None:
        cand = np.arange(len(text) - n + 1)
    out = [c for c in cand if np.array_equal(text[c:c + n] & mask, want)]
    return np.array(out, dtype=np.int64)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", type=Path, default=ROOT / "orig" / "mnax" / "SLPM_654.62")
    ap.add_argument("--text-size", type=lambda s: int(s, 0), default=1991876)
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--out", type=Path, default=ROOT / "config" / "mnax" / "probe.tsv")
    args = ap.parse_args()

    raw = args.elf.read_bytes()
    off = TEXT_START - DELTA
    text = np.frombuffer(raw[off:off + args.text_size], dtype="<u4")

    usa = {}
    for line in (ROOT / "config" / "smt3" / "symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);.*type:func", line)
        if m:
            usa.setdefault(m.group(1), int(m.group(2), 16))

    srcs = sorted((ROOT / "src" / "smt3").rglob("*.c"))
    with ThreadPoolExecutor(args.jobs) as pool:
        built = list(pool.map(compile_unit, srcs))

    rows, failed = [], []
    tally = {"unique": [0, 0], "multi": [0, 0], "miss": [0, 0]}
    for src, obj, err in built:
        unit = str(src.relative_to(ROOT / "src" / "smt3").with_suffix(""))
        if obj is None:
            failed.append((unit, err))
            continue
        for name, words, mask in functions(obj):
            hits = find(text, words, mask)
            status = "unique" if len(hits) == 1 else "multi" if len(hits) else "miss"
            tally[status][0] += 1
            tally[status][1] += len(words) * 4
            addrs = ",".join(f"0x{TEXT_START + int(h) * 4:08X}" for h in hits[:256])
            u = usa.get(name)
            rows.append((unit, name, len(words) * 4, f"0x{u:08X}" if u else "-", status, addrs or "-"))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w") as fh:
        fh.write("unit\tfunction\tsize\tusa\tstatus\ttarget\n")
        for row in rows:
            fh.write("\t".join(map(str, row)) + "\n")
    total_f = sum(v[0] for v in tally.values())
    total_b = sum(v[1] for v in tally.values())
    for k, (f, b) in tally.items():
        print(f"{k:7s} {f:6d} functions {100 * f / total_f:6.2f}%  {b:8d} bytes {100 * b / total_b:6.2f}%")
    print(f"total   {total_f:6d} functions          {total_b:8d} bytes  ({100 * total_b / args.text_size:.2f}% of target .text)")
    for unit, err in failed:
        print(f"compile failed: {unit}: {err}", file=sys.stderr)


if __name__ == "__main__":
    main()
