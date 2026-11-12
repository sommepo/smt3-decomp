#!/usr/bin/env python3
"""Check one function's C against the Maniax executable.

    python tools/mnax_check.py <unit> <function> [--asm]

Compiles src/smt3/<unit>.c as the Maniax build sees it (-DVERSION_MNAX, asm
fallbacks off), takes <function> from the object and compares it with the
function at its Maniax address (config/mnax/funcs.tsv), relocated fields
masked. Prints MATCH, or the instructions side by side with the differences
marked. Maniax-specific C goes in src/smt3 under `#ifdef VERSION_MNAX`, with
the USA code in the `#else` branch unchanged. --asm prints the Maniax asm path."""
from __future__ import annotations

import argparse
import csv
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import rabbitizer
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parent.parent
DELTA = 0xFF000
MASKS = {2: 0, 4: 0xFC000000, 5: 0xFFFF0000, 6: 0xFFFF0000, 7: 0xFFFF0000, 8: 0xFFFF0000, 9: 0xFFFF0000}
FLAGS = ["-UVERSION_SMT3", "-DVERSION_MNAX", "-DSKIP_ASM"]


def maniax_span(name: str):
    rows = [r for r in csv.DictReader((ROOT / "config/mnax/funcs.tsv").open(), delimiter="\t")]
    starts = sorted({int(r["mnax"], 16) for r in rows if r["mnax"].startswith("0x")})
    hit = [r for r in rows if r["name"] == name]
    if not hit or not hit[0]["mnax"].startswith("0x"):
        sys.exit(f"{name}: no Maniax address in config/mnax/funcs.tsv ({hit[0]['mnax'] if hit else 'unknown name'})")
    at = int(hit[0]["mnax"], 16)
    nxt = next(s for s in starts if s > at)
    return at, nxt


def compile_function(unit: str, name: str):
    src = ROOT / "src/smt3" / (unit + ".c")
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as tmp:
        obj = Path(tmp) / "out.o"
        r = subprocess.run([str(ROOT / "tools/cc.sh"), *FLAGS, str(src), "-o", str(obj)],
                           capture_output=True, text=True, env={**os.environ, "SMT3_AS_UNIT": f"src/smt3/{unit}.c"})
        if r.returncode:
            sys.exit("compile failed:\n" + "\n".join(l for l in r.stderr.splitlines() if "warning" not in l)[-3000:])
        elf = ELFFile(obj.open("rb"))
        text = elf.get_section_by_name(".text")
        tidx = elf.get_section_index(".text")
        code = text.data()
        sym = next((s for s in elf.get_section_by_name(".symtab").iter_symbols()
                    if s.name == name and s["st_info"]["type"] == "STT_FUNC"), None)
        if sym is None:
            sys.exit(f"{name} is not defined as C in {unit} under VERSION_MNAX")
        lo, hi = sym["st_value"], sym["st_value"] + sym["st_size"]
        mask = {}
        relname = {}
        syms = list(elf.get_section_by_name(".symtab").iter_symbols())
        for sec in elf.iter_sections():
            if isinstance(sec, RelocationSection) and sec["sh_info"] == tidx:
                for rel in sec.iter_relocations():
                    if lo <= rel["r_offset"] < hi:
                        o = rel["r_offset"] - lo
                        mask[o] = mask.get(o, 0xFFFFFFFF) & MASKS.get(rel["r_info_type"], 0)
                        relname[o] = syms[rel["r_info_sym"]].name or "(local)"
        words = list(struct.unpack(f"<{(hi - lo) // 4}I", code[lo:hi]))
        return words, mask, relname


def dis(word: int, vram: int) -> str:
    return rabbitizer.Instruction(word, vram, category=rabbitizer.InstrCategory.R5900).disassemble()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("unit")
    ap.add_argument("function")
    ap.add_argument("--asm", action="store_true")
    ap.add_argument("--all", action="store_true", help="print every line, not only around differences")
    args = ap.parse_args()
    at, nxt = maniax_span(args.function)
    if args.asm:
        print(ROOT / "asm/mnax/nonmatchings" / args.unit / f"{args.function}.s")
        return
    raw = (ROOT / "orig/mnax/SLPM_654.62").read_bytes()
    target = list(struct.unpack(f"<{(nxt - at) // 4}I", raw[at - DELTA:nxt - DELTA]))
    while len(target) > 1 and target[-1] == 0 and len(target) > 0:
        target.pop()                                  # alignment padding after the function
    words, mask, relname = compile_function(args.unit, args.function)
    while len(words) > 1 and words[-1] == 0 and len(words) > len(target):
        words.pop()
    n = max(len(words), len(target))
    bad = [i for i in range(n) if i >= len(words) or i >= len(target)
           or (words[i] ^ target[i]) & mask.get(i * 4, 0xFFFFFFFF)]
    if not bad:
        print(f"MATCH {args.function}: {len(words) * 4} bytes at 0x{at:08X}")
        return
    print(f"DIFF {args.function}: C {len(words) * 4} bytes, Maniax {len(target) * 4} bytes at 0x{at:08X}; "
          f"{len(bad)} of {n} instructions differ")
    near = set()
    for i in bad:
        near.update(range(i - 2, i + 3))
    last = -1
    for i in range(n):
        if not args.all and i not in near:
            continue
        if last != -1 and i != last + 1:
            print("   ...")
        last = i
        c = dis(words[i], at + i * 4) if i < len(words) else ""
        t = dis(target[i], at + i * 4) if i < len(target) else ""
        tag = "!!" if i in bad else "  "
        extra = f"  [{relname[i * 4]}]" if i * 4 in relname else ""
        print(f"{tag} {i * 4:5X}  {c:<44s} | {t}{extra}")


if __name__ == "__main__":
    main()
