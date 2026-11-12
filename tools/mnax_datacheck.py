#!/usr/bin/env python3
"""Check a unit's data (.data/.rodata/.sdata/.lit4) against the Maniax executable.

    python tools/mnax_datacheck.py <unit> [--all]

Compiles src/smt3/<unit>.c as Maniax (-DVERSION_MNAX, asm fallbacks off) and,
for every data section, places it in Maniax by the named symbols it defines
(config/mnax/symbol_addrs.txt): each named object must sit at the same offset
from the section's anchor as in the object. Then compares the whole section,
relocated words masked, and prints each object (named, or anonymous string and
constant spans) as MATCH or DIFF with both byte strings. Japanese text is
Shift-JIS; write it in C as octal escapes copied from the Maniax bytes."""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parent.parent
DELTA = 0xFF000
SECS = (".data", ".rodata", ".sdata", ".lit4")
FLAGS = ["-UVERSION_SMT3", "-DVERSION_MNAX", "-DSKIP_ASM"]


def lit(b: bytes) -> str:
    return "".join(chr(c) if 32 <= c < 127 and chr(c) not in '"\\' else f"\\{c:03o}" for c in b)


def usa_start(unit: str, sec: str):
    for line in (ROOT / "config/smt3/SLUS_209.11.yaml").read_text().splitlines():
        m = re.match(r"\s*- \[(0x[0-9A-Fa-f]+), ([.\w]+), ([\w/]+)\]", line)
        if m and m.group(3) == unit and m.group(2) == sec:
            return int(m.group(1), 16) + DELTA
    return None


def best_fit(raw: bytes, data: bytes, mask: bytes, usa):
    """Maniax address where the most bytes agree, searched near the USA address
    shifted by the nearby anchors' delta. Data moves by at most ~0x3000 here."""
    if usa is None:
        return None
    import bisect
    import csv
    pairs = sorted((int(r["usa"], 16), int(r["mnax"], 16))
                   for r in csv.DictReader((ROOT / "config/mnax/anchors.tsv").open(), delimiter="\t"))
    i = bisect.bisect_left([u for u, _ in pairs], usa)
    guess = usa + (pairs[min(i, len(pairs) - 1)][1] - pairs[min(i, len(pairs) - 1)][0])
    probe = [(k, data[k]) for k in range(0, len(data), max(1, len(data) // 256)) if mask[k] and data[k]]
    best, where = -1, None
    for at in range(guess - 0x3000, guess + 0x3000, 4):
        o = at - DELTA
        score = sum(raw[o + k] == v for k, v in probe)
        if score > best:
            best, where = score, at
    return where


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("unit")
    ap.add_argument("--all", action="store_true", help="also print matching objects")
    args = ap.parse_args()
    syms = {}
    for line in (ROOT / "config/mnax/symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);", line)
        if m:
            syms[m.group(1)] = int(m.group(2), 16)
    raw = (ROOT / "orig/mnax/SLPM_654.62").read_bytes()
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as tmp:
        obj = Path(tmp) / "out.o"
        r = subprocess.run([str(ROOT / "tools/cc.sh"), *FLAGS, f"src/smt3/{args.unit}.c", "-o", str(obj)], cwd=ROOT,
                           capture_output=True, text=True, env={**os.environ, "SMT3_AS_UNIT": f"src/smt3/{args.unit}.c"})
        if r.returncode:
            sys.exit("compile failed:\n" + "\n".join(l for l in r.stderr.splitlines() if "warning" not in l)[-3000:])
        elf = ELFFile(obj.open("rb"))
        allsyms = list(elf.get_section_by_name(".symtab").iter_symbols())
        bad_total = 0
        for name in SECS:
            sec = elf.get_section_by_name(name)
            if sec is None or not sec["sh_size"]:
                continue
            idx = elf.get_section_index(name)
            data = sec.data()
            mask = bytearray(b"\xff" * len(data))
            rel = elf.get_section_by_name(".rel" + name)
            if isinstance(rel, RelocationSection):
                for rr in rel.iter_relocations():
                    if rr["r_info_type"] == 2:              # R_MIPS_32: a pointer word
                        mask[rr["r_offset"]:rr["r_offset"] + 4] = b"\0\0\0\0"
            named = sorted((s["st_value"], s["st_size"], s.name) for s in allsyms
                           if s["st_shndx"] == idx and s.name and s["st_info"]["type"] == "STT_OBJECT")
            votes = Counter(syms[n] - v for v, _, n in named if n in syms)
            if not votes:
                base = best_fit(raw, data, mask, usa_start(args.unit, name))
                if base is None:
                    print(f"{name}: {len(data)} bytes, cannot place it in Maniax")
                    continue
                n_votes = 0
                print(f"{name}: no named object to place it by; best byte fit at 0x{base:08X}")
            else:
                base, n_votes = votes.most_common(1)[0]
            if len(votes) > 1:
                odd = [n for v, _, n in named if n in syms and syms[n] - v != base]
                print(f"{name}: objects disagree on placement (layout differs before {odd[0]}); "
                      f"using the majority ({n_votes} of {sum(votes.values())})")
            there = raw[base - DELTA:base - DELTA + len(data)]
            # Spans: named objects, and anonymous gaps between them (literals, constants).
            spans, at = [], 0
            for v, size, n in named + [(len(data), 0, None)]:
                if v > at:
                    spans.append((at, v - at, "(anonymous)"))
                if n:
                    spans.append((v, max(size, 1), n))
                at = max(at, v + size)
            bad = 0
            for off, size, n in spans:
                a, b = data[off:off + size], there[off:off + size]
                ok = all((x ^ y) & k == 0 for x, y, k in zip(a, b, mask[off:off + size]))
                if ok and not args.all:
                    continue
                bad += not ok
                print(f"{name} +0x{off:X} 0x{base + off:08X} {n} ({size} bytes): {'MATCH' if ok else 'DIFF'}")
                if not ok:
                    # Show the window around the first differing byte, not just the start.
                    first = next(i for i, (x, y, k) in enumerate(zip(a, b, mask[off:off + size])) if (x ^ y) & k)
                    lo = max(0, first - 16)
                    print(f'    first difference at +0x{first:X}; bytes from +0x{lo:X}:')
                    print(f'    C:      "{lit(a[lo:lo + 128])}"')
                    print(f'    Maniax: "{lit(b[lo:lo + 128])}"')
            tail = raw[base - DELTA + len(data):base - DELTA + len(data) + 16]
            print(f"{name}: {len(data)} bytes at 0x{base:08X}, {bad} objects differ"
                  + (f"; Maniax continues with {tail.hex(' ')}" if bad else ""))
            bad_total += bad
        print("ALL DATA MATCH" if not bad_total else f"{bad_total} data objects differ")


if __name__ == "__main__":
    main()
