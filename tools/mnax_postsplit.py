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
    duplicates = drop_duplicate_pieces()
    print(f"mnax: {removed} migrated rodata blocks left to their C definitions, {dropped} dangling include lines dropped, "
          f"{duplicates} pieces the C already defines dropped")


def drop_duplicate_pieces() -> int:
    """An INCLUDE_RODATA/SDATA piece at an address where the unit's C defines the same
    object under another name (the carried name is missing or points elsewhere, so
    splat labelled the address automatically) would put the bytes in twice. A piece
    whose Maniax bytes start with the exact bytes of one of the unit's C objects (at
    least 8 bytes, not all zero, no relocations) is that object: its line goes, and
    whatever points at the label resolves through the linker's PROVIDE."""
    from elftools.elf.elffile import ELFFile
    raw = (ROOT / "orig/mnax/SLPM_654.62").read_bytes()
    delta = 0xFF000
    piece = re.compile(r'^INCLUDE_(?:RODATA|SDATA)\([^,]+,\s*"([^"]+)",\s*(\w+)\);\n(?:SDATA_RESUME\(\);\n)?\n?', re.M)
    dropped = 0
    named = {}
    for line in (ROOT / "config/mnax/symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);.*size:(0x[0-9A-Fa-f]+)", line)
        if m and "type:func" not in line:
            named[m.group(1)] = (int(m.group(2), 16), int(m.group(2), 16) + int(m.group(3), 16))
    for src in sorted((ROOT / "src/mnax").rglob("*.c")):
        text = src.read_text(errors="replace")
        if "INCLUDE_RODATA" not in text and "INCLUDE_SDATA" not in text:
            continue
        unit = src.relative_to(ROOT / "src/mnax").with_suffix("").as_posix()
        obj = ROOT / "build/probe" / (unit + ".o")
        if not obj.exists() or obj.read_bytes()[:4] != b"\x7fELF":
            continue
        elf = ELFFile(obj.open("rb"))
        objects = []
        for sec_name in (".rodata", ".sdata", ".data"):
            sec = elf.get_section_by_name(sec_name)
            if sec is None:
                continue
            rel = elf.get_section_by_name(".rel" + sec_name)
            relocated = {r["r_offset"] for r in rel.iter_relocations()} if rel is not None else set()
            idx = elf.get_section_index(sec_name)
            data = sec.data()
            for sym in elf.get_section_by_name(".symtab").iter_symbols():
                if sym["st_shndx"] == idx and sym["st_info"]["type"] == "STT_OBJECT" and sym["st_size"] >= 8:
                    lo, hi = sym["st_value"], sym["st_value"] + sym["st_size"]
                    b = data[lo:hi]
                    if any(b) and not any(lo <= r < hi for r in relocated):
                        objects.append(b)
        # Objects the C defines whose Maniax address and size the symbol file knows:
        # a piece starting inside one is part of it.
        spans = [named[n] for n in set(DATA_DEF.findall(text)) if n in named]

        def keep(m):
            nonlocal dropped
            f = ROOT / "asm/mnax/nonmatchings" / m.group(1) / (m.group(2) + ".s")
            a = re.search(r"^\s*/\* [0-9A-F]+ ([0-9A-F]{8})", f.read_text(), re.M) if f.exists() else None
            if a is None:
                return m.group(0)
            vaddr = int(a.group(1), 16)
            at = vaddr - delta
            if any(raw[at:at + len(b)] == b for b in objects) or any(lo <= vaddr < hi for lo, hi in spans):
                dropped += 1
                return ""
            return m.group(0)
        new = piece.sub(keep, text)
        if new != text:
            src.write_text(new)
    return dropped


if __name__ == "__main__":
    main()
