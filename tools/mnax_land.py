#!/usr/bin/env python3
"""Check worker edits before landing them.

    python tools/mnax_land.py              every src/smt3 unit changed since HEAD

For each changed unit: the USA compile (-DSKIP_ASM) must give the same section
bytes and symbols as build/usbase (taken before the edits), and every queued
Maniax function of the unit is run through mnax_check.py. Prints the units that
change the USA build, then MATCH/DIFF per function."""
import csv
import subprocess
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent.parent
SECS = (".text", ".data", ".rodata", ".lit4", ".sdata", ".sbss", ".bss")


def sections(obj):
    elf = ELFFile(obj.open("rb"))
    out = {}
    for name in SECS:
        sec = elf.get_section_by_name(name)
        if sec is not None:
            out[name] = sec.data() if sec["sh_type"] != "SHT_NOBITS" else sec["sh_size"]
    out["symbols"] = sorted((s.name, s["st_value"], s["st_size"]) for s in elf.get_section_by_name(".symtab").iter_symbols()
                            if s.name and s["st_info"]["bind"] == "STB_GLOBAL")
    return out


def main() -> None:
    changed = subprocess.run(["git", "diff", "--name-only", "HEAD", "--", "src/smt3"], cwd=ROOT,
                             capture_output=True, text=True).stdout.split()
    units = [Path(p).relative_to("src/smt3").with_suffix("").as_posix() for p in changed if p.endswith(".c")]
    queue = list(csv.DictReader((ROOT / "config/mnax/jp_queue.tsv").open(), delimiter="\t"))
    bad_us = []
    matched = 0
    for unit in units:
        base = ROOT / "build/usbase" / (unit.replace("/", "__") + ".o")
        now = ROOT / "build" / "land_us.o"
        r = subprocess.run([str(ROOT / "tools/cc.sh"), "-DSKIP_ASM", f"src/smt3/{unit}.c", "-o", str(now)],
                           cwd=ROOT, capture_output=True, text=True)
        if r.returncode or sections(now) != sections(base):
            bad_us.append(unit)
            print(f"USA BUILD CHANGED: {unit}" + (" (does not compile)" if r.returncode else ""))
        for row in queue:
            if row["unit"] == unit:
                r = subprocess.run([sys.executable, "tools/mnax_check.py", unit, row["function"]], cwd=ROOT,
                                   capture_output=True, text=True)
                line = (r.stdout or r.stderr).strip().splitlines()[0] if (r.stdout or r.stderr).strip() else "?"
                if line.startswith("MATCH"):
                    matched += 1
                    print(f"  {line}")
    print(f"{len(units)} units changed, {len(bad_us)} change the USA build, {matched} Maniax functions match")
    sys.exit(1 if bad_us else 0)


if __name__ == "__main__":
    main()
