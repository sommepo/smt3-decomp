#!/usr/bin/env python3
"""Maniax assembly of a function, for reading and for m2c.

    python tools/mnax_asm.py --freeze              copy every queued function's asm to build/mnax_jp/
    python tools/mnax_asm.py <function>            print it
    python tools/mnax_asm.py <function> --m2c      m2c draft, with the unit's src/smt3 context

Reads build/mnax_jp/<function>.s, which --freeze extracts from the Maniax split
(asm/mnax), so it stays put while the split is regenerated. The queue is
config/mnax/jp_queue.tsv."""
from __future__ import annotations

import argparse
import csv
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
OUT = ROOT / "build/mnax_jp"


def freeze() -> None:
    names = {r["function"]: r["unit"] for r in csv.DictReader((ROOT / "config/mnax/jp_queue.tsv").open(), delimiter="\t")}
    OUT.mkdir(parents=True, exist_ok=True)
    found = set()
    for path in sorted((ROOT / "asm/mnax").rglob("*.s")):
        text = path.read_text()
        if "glabel " not in text:
            continue
        for m in re.finditer(r"^glabel (\w+)\n.*?^endlabel \1\n", text, re.M | re.S):
            name = m.group(1)
            if name not in names or name in found:
                continue
            body = m.group(0)
            if "nonmatchings" in path.parts:
                body = text            # keeps the rodata splat moved into the function
            (OUT / f"{name}.s").write_text('.include "macro.inc"\n\n.section .text\n\n' + body
                                           if not body.startswith(".section") else '.include "macro.inc"\n\n' + body)
            found.add(name)
    missing = sorted(set(names) - found)
    print(f"{len(found)} functions frozen in {OUT.relative_to(ROOT)}; {len(missing)} not found in the split")
    if missing:
        print("  " + " ".join(missing[:40]))


def print_data(text: str) -> None:
    """The first bytes of each data symbol the function refers to, from the Maniax ELF,
    as hex and as a C string literal (Shift-JIS stays as octal escapes)."""
    syms = {}
    for line in (ROOT / "config/mnax/symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);", line)
        if m:
            syms[m.group(1)] = int(m.group(2), 16)
    raw = (ROOT / "orig/mnax/SLPM_654.62").read_bytes()
    refs = sorted(set(re.findall(r"%(?:hi|lo|gp_rel)\((\w+)\)", text)))
    shown = False
    for name in refs:
        at = syms.get(name)
        if at is None and re.fullmatch(r"D_[0-9A-F]{8}", name):
            at = int(name[2:], 16)
        if at is None or not 0x2F4D00 <= at < 0x54FB00:
            continue
        chunk = raw[at - 0xFF000:at - 0xFF000 + 64]
        end = chunk.find(b"\0")
        lit = "".join(chr(b) if 32 <= b < 127 and chr(b) not in '"\\' else f"\\{b:03o}" for b in chunk[:end if end >= 0 else 64])
        if not shown:
            print("/* data the function refers to (Maniax bytes) */")
            shown = True
        print(f"/* {name} @ 0x{at:08X}: {chunk[:32].hex(' ')} */")
        print(f'/*   as string: "{lit}" */')


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("function", nargs="?")
    ap.add_argument("--freeze", action="store_true")
    ap.add_argument("--m2c", action="store_true")
    args = ap.parse_args()
    if args.freeze:
        return freeze()
    asm = OUT / f"{args.function}.s"
    if not asm.exists():
        sys.exit(f"{asm} missing; run tools/mnax_asm.py --freeze")
    if not args.m2c:
        text = asm.read_text()
        print(text)
        print_data(text)
        return
    import m2ctx
    unit = next((r["unit"] for r in csv.DictReader((ROOT / "config/mnax/jp_queue.tsv").open(), delimiter="\t")
                 if r["function"] == args.function), None)
    cmd = [sys.executable, str(ROOT / "tools/m2c_ee.py"), "-t", "mipsee-gcc-c"]
    with tempfile.NamedTemporaryFile("w", suffix=".c") as ctx:
        if unit:
            ctx.write(m2ctx.context(ROOT / "src/smt3" / f"{unit}.c")
                      .replace("__SIZE_TYPE__", "unsigned int").replace("__PTRDIFF_TYPE__", "int"))
            ctx.flush()
            cmd += ["--context", ctx.name]
        cmd.append(str(asm))
        sys.exit(subprocess.run(cmd).returncode)


if __name__ == "__main__":
    main()
