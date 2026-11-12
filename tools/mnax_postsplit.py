#!/usr/bin/env python3
"""Post-split fixups for the Maniax target (configure.py runs it after splat).

    python tools/mnax_postsplit.py

splat moves rodata that only one function uses into that function's asm file.
In Maniax a function can be assembly while its unit's C still defines that
rodata (the C was written for the USA build, where the function is C), so the
INCLUDE_ASM would define the symbol twice. Such blocks are taken out of the
function's file: the C definition emits them in their source position.

The generated sources also keep the USA build's INCLUDE_RODATA/SDATA lines.
The split tools re-place the ones they own; a line whose symbol the Maniax
split does not have is dropped here (the link then says if it was needed)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from include_sdata import DATA_DEF  # noqa: E402

BLOCK = re.compile(r"(?:\.align \d+\n)?nonmatching (\w+)[^\n]*\n\n?dlabel \1\n.*?enddlabel \1\n\n?", re.S)


def main() -> None:
    removed = 0
    for src in sorted((ROOT / "src/mnax").rglob("*.c")):
        unit = src.relative_to(ROOT / "src/mnax").with_suffix("").as_posix()
        folder = ROOT / "asm/mnax/nonmatchings" / unit
        if not folder.is_dir():
            continue
        defined = set(DATA_DEF.findall(src.read_text(errors="replace")))
        for asm in folder.glob("*.s"):
            text = asm.read_text()
            if not text.startswith(".section .rodata"):
                continue
            head, sep, tail = text.partition(".section .text")
            if not sep:
                continue
            new = BLOCK.sub(lambda m: "" if m.group(1) in defined else m.group(0), head)
            if new != head:
                removed += head.count("dlabel ") - new.count("dlabel ")
                asm.write_text(new + sep + tail)
    dropped = 0
    line = re.compile(r'^(INCLUDE_(?:ASM|RODATA|SDATA)\([^,]+,\s*"([^"]+)",\s*(\w+)\);)\n(?:SDATA_RESUME\(\);\n)?\n?', re.M)
    for src in sorted((ROOT / "src/mnax").rglob("*.c")):
        text = src.read_text(errors="replace")

        def keep(m):
            nonlocal dropped
            if (ROOT / "asm/mnax/nonmatchings" / m.group(2) / (m.group(3) + ".s")).exists():
                return m.group(0)
            dropped += 1
            return ""
        new = line.sub(keep, text)
        if new != text:
            src.write_text(new)
    print(f"mnax: {removed} migrated rodata blocks left to their C definitions, {dropped} dangling include lines dropped")


if __name__ == "__main__":
    main()
