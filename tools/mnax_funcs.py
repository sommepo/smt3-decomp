#!/usr/bin/env python3
"""Find every USA function's Maniax counterpart, including the ones that differ.

    python tools/mnax_funcs.py        (after mnax_probe.py and mnax_map.py)

Functions the probe placed, or that matched code calls, have exact addresses.
The rest sit in gaps between those. A gap that kept its length moved rigidly.
Otherwise the Maniax function starts inside it are found from the code (the
word after a `jr $ra` and its delay slot, on an 8-byte boundary) and paired with
the USA functions in order, by similarity to the compiled USA function when the
counts differ. A USA function with no counterpart is `absent`; a Maniax start
with no USA function is listed under its automatic name with `-` for USA.

Writes config/mnax/funcs.tsv: usa name, usa address, maniax address, how."""
from __future__ import annotations

import csv
import difflib
import re
import struct
from collections import Counter
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent.parent
DELTA = 0xFF000
TEXT = (0x00100000, 0x00100000 + 0x1E64C4)
USA_TEXT_END = 0x002E6E3C
JR_RA = 0x03E00008


def main() -> None:
    raw = (ROOT / "orig/mnax/SLPM_654.62").read_bytes()
    word = lambda a: struct.unpack_from("<I", raw, a - DELTA)[0]

    usa = set()
    for line in (ROOT / "config/smt3/symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"(\w+) = (0x[0-9A-Fa-f]+);.*type:func", line)
        if m and TEXT[0] <= int(m.group(2), 16) < USA_TEXT_END:
            usa.add((int(m.group(2), 16), m.group(1)))
    usa = sorted(usa)
    anchors = {int(r["usa"], 16): int(r["mnax"], 16)
               for r in csv.DictReader((ROOT / "config/mnax/anchors.tsv").open(), delimiter="\t")}
    placed, unit_of, size_of = {}, {}, {}
    for r in csv.DictReader((ROOT / "config/mnax/functions.tsv").open(), delimiter="\t"):
        unit_of[r["function"]] = r["unit"]
        if r["mnax"].startswith("0x"):
            placed[r["function"]] = int(r["mnax"], 16)
            size_of[r["function"]] = int(r["size"])

    jal = set()
    for a in range(TEXT[0], TEXT[1], 4):
        w = word(a)
        if w >> 26 == 3:
            jal.add((w & 0x3FFFFFF) << 2)

    def ends_function(w, strict):
        # `jr $ra`, or in the loose pass a sibling call's `j`.
        return w == JR_RA or not strict and w >> 26 == 2

    def starts(lo, hi, strict, first=None):
        """Function starts in (lo, hi). `first` is where the function at lo is known to end."""
        out = [first] if first is not None and lo < first < hi else []
        for s in range((max(lo, first or lo) + 8) & ~7, hi, 8):
            if not (ends_function(word(s - 8), strict) or ends_function(word(s - 12), strict) and word(s - 4) == 0):
                continue
            w = word(s)
            if strict and not (s in jal or w >> 16 == 0x27BD and w & 0x8000):
                continue
            out.append(s)
        return out

    objs = {}

    def compiled(name):
        """Upper halfwords (opcode and registers) of the compiled USA function."""
        unit = unit_of.get(name)
        if unit is None:
            return None
        if unit not in objs:
            elf = ELFFile((ROOT / "build/probe" / (unit + ".o")).open("rb"))
            code = elf.get_section_by_name(".text").data()
            objs[unit] = {s.name: code[s["st_value"]:s["st_value"] + s["st_size"]]
                          for s in elf.get_section_by_name(".symtab").iter_symbols()
                          if s["st_info"]["type"] == "STT_FUNC" and s["st_size"]}
        body = objs[unit].get(name)
        return None if body is None else [w >> 16 for w in struct.unpack(f"<{len(body) // 4}I", body)]

    def there(lo, hi):
        return [word(a) >> 16 for a in range(lo, hi, 4)]

    known = [anchors.get(a) for a, _ in usa]
    how = ["placed" if n in placed else "called" if k is not None else "" for (a, n), k in zip(usa, known)]
    extra = []           # (maniax start, index of the USA function it follows)
    tally = Counter(how)
    i, n = 0, len(usa)
    while i < n:
        if known[i] is not None:
            i += 1
            continue
        j = i
        while j < n and known[j] is None:
            j += 1
        left, right = i - 1, j
        if left < 0 or right >= n:
            i = j
            continue
        run = list(range(i, j))
        if known[right] - known[left] == usa[right][0] - usa[left][0]:
            for t in run:
                known[t], how[t] = usa[t][0] + known[left] - usa[left][0], "rigid"
        else:
            cand = None
            lname = usa[left][1]
            first = (known[left] + size_of[lname] + 7) & ~7 if placed.get(lname) == known[left] else None
            for strict in (True, False):
                c = starts(known[left], known[right], strict, first)
                if len(c) == len(run):
                    cand = c
                    break
            if cand is not None:
                for t, s in zip(run, cand):
                    known[t], how[t] = s, "gap"
            else:
                c = starts(known[left], known[right], False, first)
                ends = c[1:] + [known[right]]
                # Monotone pairing of USA functions with Maniax starts by similarity.
                score = [[0.0] * len(c) for _ in run]
                for x, t in enumerate(run):
                    want = compiled(usa[t][1])
                    for y, (s, e) in enumerate(zip(c, ends)):
                        score[x][y] = 0.3 if want is None else \
                            difflib.SequenceMatcher(None, want, there(s, e), autojunk=False).ratio()
                best = [[(0.0, None)] * (len(c) + 1) for _ in range(len(run) + 1)]
                for x in range(1, len(run) + 1):
                    for y in range(1, len(c) + 1):
                        opts = [(best[x - 1][y][0], ("u", x - 1, y)), (best[x][y - 1][0], ("c", x, y - 1))]
                        if score[x - 1][y - 1] >= 0.5 or usa[run[x - 1]][1] not in unit_of:
                            opts.append((best[x - 1][y - 1][0] + score[x - 1][y - 1], ("m", x - 1, y - 1)))
                        best[x][y] = max(opts, key=lambda o: o[0])
                x, y, pairs = len(run), len(c), {}
                while x and y:
                    step = best[x][y][1]
                    if step is None:
                        break
                    if step[0] == "m":
                        pairs[x - 1] = y - 1
                    x, y = step[1], step[2]
                used = set(pairs.values())
                for x, t in enumerate(run):
                    if x in pairs:
                        known[t], how[t] = c[pairs[x]], "similar"
                    else:
                        how[t] = "absent"
                for y, s in enumerate(c):
                    if y not in used:
                        after = max([run[x] for x, yy in pairs.items() if yy < y], default=left)
                        extra.append((s, after))
        i = j

    rows = []
    for (a, name), m, h in zip(usa, known, how):
        rows.append((a, name, f"0x{m:08X}" if m is not None else "absent" if h == "absent" else "unknown", h or "unknown"))
    out = ROOT / "config/mnax/funcs.tsv"
    with out.open("w") as fh:
        fh.write("name\tusa\tmnax\thow\tafter\n")
        for a, name, m, h in rows:
            fh.write(f"{name}\t0x{a:08X}\t{m}\t{h}\t\n")
        for s, after in sorted(extra):
            fh.write(f"func_{s:08X}\t-\t0x{s:08X}\tmaniax only\t{usa[after][1]}\n")
    tally = Counter(h for _, _, _, h in rows)
    print(", ".join(f"{v} {k}" for k, v in tally.most_common()), f"; {len(extra)} Maniax-only starts")


if __name__ == "__main__":
    main()
