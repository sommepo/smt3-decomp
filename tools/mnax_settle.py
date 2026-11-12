#!/usr/bin/env python3
"""Regenerate and build the Maniax target until it is byte-identical.

    python tools/mnax_settle.py [--rounds 40] [--jobs 6]

Each round runs mnax_config.py, configure.py and ninja. A unit that does not
compile, does not link, links at a different size than its place in Maniax, or
produces bytes that differ from the retail executable is written to
config/mnax/demote.txt with the reason, and goes back to being disassembly in
the next round. Stops when the image matches or a round changes nothing."""
from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DELTA = 0xFF000
CFG = ROOT / "config/mnax"
YAML = CFG / "SLPM_654.62.yaml"
IMAGE = ROOT / "build/mnax/SLPM_654.62"
RETAIL = ROOT / "orig/mnax/SLPM_654.62"
UNIT = re.compile(r"build/mnax/src/mnax/([\w/]+)\.o")


def run(*cmd):
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True)


def state():
    return hashlib.sha1(b"".join((CFG / n).read_bytes() for n in ("SLPM_654.62.yaml", "symbol_addrs.txt")
                                 if (CFG / n).exists())).hexdigest()


def entries():
    out = []
    for line in YAML.read_text().splitlines():
        m = re.match(r"\s*- \[(0x[0-9A-Fa-f]+), ([.\w]+), ([\w/]+)\]", line)
        if m:
            out.append((int(m.group(1), 16), m.group(2), m.group(3)))
    return out


def drift_culprits():
    want = {}
    for rom, kind, name in entries():
        sec = "text" if kind in ("c", "asm") else kind.lstrip(".")
        want[(name, sec)] = rom + DELTA
    text = (ROOT / "build/mnax/SLPM_654.62.map").read_text()
    prev, last_c, out = 0, None, {}
    for m in re.finditer(r"^ \.(rodata|data|sdata|lit4|text)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+) "
                         r"build/mnax/(src|asm)/mnax/(?:data/)?(\S+?)(?:\.(?:rodata|data|sdata|lit4))?\.o$", text, re.M):
        sec, addr, size, kind, name = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4), m.group(5)
        if not addr or not size or (name, sec) not in want:
            continue
        d = addr - want[(name, sec)]
        if d != prev:
            culprit = last_c or (name if kind == "src" else None)
            if culprit:
                out.setdefault(culprit, f".{sec} links at a different size than its place in Maniax")
            prev = d
        if kind == "src":
            last_c = name
    return out


def byte_culprits():
    a, b = IMAGE.read_bytes(), RETAIL.read_bytes()
    rows = entries()
    starts = [rom for rom, _, _ in rows]
    import bisect
    out, stray = {}, 0
    i, n = 0, min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            k = bisect.bisect_right(starts, i) - 1
            rom, kind, name = rows[k]
            if kind == "c" or kind.startswith("."):
                out.setdefault(name, f"{'.text' if kind == 'c' else kind} bytes differ from Maniax")
            else:
                stray += 1
            i = starts[k + 1] if k + 1 < len(starts) else n
        else:
            i += 1
    return out, stray


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=40)
    ap.add_argument("--jobs", type=int, default=6)
    args = ap.parse_args()
    py = sys.executable
    demote = CFG / "demote.txt"
    for rnd in range(1, args.rounds + 1):
        before = state()
        for _ in range(2):          # second pass picks up name clashes and object sizes
            r = run(py, "tools/mnax_config.py")
            if r.returncode:
                sys.exit(r.stdout + r.stderr)
            r = run(py, "configure.py", "mnax")
            if r.returncode:
                sys.exit((r.stdout + r.stderr)[-3000:])
        r = run("ninja", f"-j{args.jobs}", "-k", "0", "mnax")
        log = r.stdout + r.stderr
        found = {}
        for m in re.finditer(r"^FAILED: \[code=\d+\] (\S+)", log, re.M):
            u = UNIT.search(m.group(1))
            if u:
                found.setdefault(u.group(1), "does not compile for Maniax")
        for m in re.finditer(r"ninja: error: 'asm/mnax/nonmatchings/([\w/]+)/(\w+)\.s'", log):
            found.setdefault(m.group(1), f"no Maniax assembly for {m.group(2)}")
        for m in re.finditer(r"build/mnax/src/mnax/([\w/]+)\.o: in function `(\w+)':\n[^\n]*(undefined reference to `\w+')", log):
            found.setdefault(m.group(1), f"{m.group(2)}: {m.group(3)}")
        # A blob that cannot find its own jump table or data: a C unit's guessed
        # bounds swallowed it.
        rows = entries()
        for m in re.finditer(r"build/mnax/asm/mnax/[^\n]*\n(?:[^\n]*\n)?[^\n]*undefined reference to `(?:jtbl|D)_([0-9A-F]{8})'", log):
            rom = int(m.group(1), 16) - DELTA
            owner = [(r, k, n) for r, k, n in rows if r <= rom][-1]
            if owner[1].startswith("."):
                found.setdefault(owner[2], f"{owner[1]} bounds take in Maniax data the unit does not define")
        linked = "SLPM_654.62.elf" not in "".join(re.findall(r"^FAILED: .*", log, re.M))
        matched = r.returncode == 0
        # A gp-relative overflow is what a shifted layout looks like to the linker:
        # the map is still written, and it says which unit has the wrong size.
        if not found and not matched and (linked or "relocation truncated" in log):
            found = drift_culprits()
            if not found and linked and IMAGE.exists():
                found, stray = byte_culprits()
                if stray:
                    print(f"  {stray} differing ranges outside C units")
        size_changed = state() != before
        print(f"round {rnd}: {'byte-identical' if matched else 'no match'}; {len(found)} units to demote"
              + ("" if matched or found else "; " + "; ".join(l for l in log.splitlines() if "rror" in l)[:600]), flush=True)
        if matched:
            return
        if not found:
            if size_changed and rnd < args.rounds:
                continue
            sys.exit(1)
        with demote.open("a") as fh:
            for unit, why in sorted(found.items()):
                fh.write(f"{unit}\t{why}\n")
                print(f"  demote {unit}: {why}")
    sys.exit(1)


if __name__ == "__main__":
    main()
