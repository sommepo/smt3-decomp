# Matching a unit's Japanese data (Maniax, `SLPM_654.62`)

Read `docs/mnax-worker.md` first: same project, same rules (only your units,
USA build unchanged, everything for Maniax under `#ifdef VERSION_MNAX`, no
ninja/configure/git). This brief is for units whose *code* already matches
Maniax but whose *data* does not: translated strings, Japanese glyph tables,
different table sizes, coordinates in data tables.

## The loop for one unit

1. `.venv/bin/python tools/mnax_datacheck.py <unit>` compiles the unit as
   Maniax and compares every data section (.data, .rodata, .sdata, .lit4) with
   the Maniax executable. It prints each differing object with the C bytes and
   the Maniax bytes, both as C string literals.
2. Give each differing object its Maniax contents under `#ifdef VERSION_MNAX`,
   keeping the USA definition in `#else` exactly. Strings: copy the Maniax
   bytes as octal escapes (Shift-JIS), not UTF-8. Arrays that are shorter or
   longer in Maniax change size under the guard too.
3. Anonymous spans are string literals and constants that functions use
   inline; change the literal in the function, guarded the same way.
4. Repeat until it prints `ALL DATA MATCH`, then make sure the code still
   matches: `.venv/bin/python tools/mnax_check.py <unit> <function>` for any
   function whose literals you touched, and
   `.venv/bin/python tools/mnax_land.py --us <unit>` must say `USA build unchanged`.

When the tool reports that objects "disagree on placement", the layout differs
before the named object: an earlier object changed size. Fix the earliest
differing object first.

## Report

One line per unit: `DATA MATCH`, or `NO` with what still differs.
