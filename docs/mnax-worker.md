# Matching Japanese-only functions (Maniax, `SLPM_654.62`)

The Maniax build (version key `mnax`) is built from the USA source in `src/smt3`.
About 500 functions compile from the USA C but differ in Maniax: the Japanese
font, text, name entry, save and menu code. Each one is a small job: change the
C so that, when compiled as Maniax, it gives the Maniax bytes, while the USA
build stays exactly as it is.

## The loop for one function

1. Read the USA C of the function in `src/smt3/<unit>.c`.
2. Read the Maniax assembly: `python tools/mnax_asm.py <function>`.
   `python tools/mnax_asm.py <function> --m2c` gives an m2c draft with the unit's
   context. Usually the USA C is the better starting point: find what Maniax
   does differently and change only that.
3. Put the Maniax version next to the USA one:

   ```c
   #ifdef VERSION_MNAX
   s32 frExample(s32 a)
   {
       /* Maniax version */
   }
   #else
   s32 frExample(s32 a)
   {
       /* USA version, byte for byte as it was */
   }
   #endif
   ```

   Keep the guard as tight as possible. When only a line or two differ, guard
   just those lines inside the function. Declarations a Maniax version needs
   (a different struct field, an extern, a string table) get their own
   `#ifdef VERSION_MNAX` block.
4. Check it: `python tools/mnax_check.py <unit> <function>`. It compiles the
   unit as Maniax and prints `MATCH`, or both instruction streams with the
   differing lines marked `!!`. Relocated fields (addresses, call targets) are
   masked, so a call to a function under another name still matches.
5. Repeat 3 and 4 until it prints `MATCH`.

`mnax_check.py` compares `.text` only. If the function has string literals or
other constant data, the Maniax bytes are in the `.rodata` part at the top of
the `mnax_asm.py` output; your literals must produce exactly those bytes.
Japanese text is Shift-JIS: write it as octal escapes (`"\202\240"`), copying
the bytes from the assembly, not as UTF-8.

## Rules

- **Only edit the units you were given.** Other workers edit other files at the
  same time.
- **Never change what the USA build compiles.** Everything you add for Maniax is
  under `VERSION_MNAX`; the `#else` branch keeps the original text exactly.
  Check it with `.venv/bin/python tools/mnax_land.py --us <unit>`: it must say
  `USA build unchanged` for every unit you edited before you finish.
- Follow `docs/CONTRIBUTING.md`: ordinary, readable C. No fakematches, no
  register pinning, no `asm` in C files, no dummy uses. A function you cannot
  match honestly stays as it is.
- Do not run `ninja`, `configure.py` or the other `tools/mnax_*.py` scripts, and
  do not commit. Only `mnax_asm.py`, `mnax_check.py`, `mnax_land.py --us` and
  `tools/cc.sh`.
- **Never copy, patch or wrap the project's scripts** (`tools/cc.sh` included)
  and never run `mv`, `rm -r` or `rsync` on anything under the repository. A
  patched copy of cc.sh once moved the whole repository away. If you need
  compiler dumps or other output, ask the coordinator in your report.
- Names: a Maniax symbol with no USA counterpart appears as `D_<address>` or
  `func_<address>` (Maniax addresses) in the assembly. Declare it under
  `VERSION_MNAX` with that name.
- Time-box each function. If it still does not match after a few honest
  attempts, put it back exactly as you found it and move on.

## What earlier workers found (use it)

- `include/newdata.h` already has the Maniax layout of `dds3GlobalWork_t`
  (`DDS3_GBWK`) under `VERSION_MNAX`: the header is 4 bytes shorter and the
  name table 0x244C bytes shorter. Field accesses get Maniax offsets for free.
  Never add per-unit offset compensation for those shifts.
- Source that uses a raw byte offset into the global work (`(u8 *)DDS3_GBWK +
  0xA54`) does not follow the header. Rewrite it as a field access, or as
  `((u32)&((dds3GlobalWork_t *)0)->field)` (ee-gcc has no `offsetof`), when
  that keeps the USA object unchanged; otherwise guard the literal.
- Maniax `frSetFontString` takes 5 arguments and `frGetCNameFRQ` takes 3.
  Declare and call them that way under `VERSION_MNAX`.
- Many Japanese-only differences are screen coordinates, table sizes or row
  strides (24-byte instead of 26-byte rows, a 0x30-byte save record): small
  `#ifdef VERSION_MNAX` constants.
- A function that is a different algorithm in Maniax (new code paths, much
  longer or shorter) can be written as its own Maniax version from the
  assembly and the m2c draft; give it a real try before giving up.

- Party/demon records (`unitwork[]`, `party[]`, `akuma[]` in `DDS3_GBWK`): when
  Maniax computes `index*0xEC + 0xA50` before adding the base, assign the byte
  offset to its own integer local first:
  `s32 ofs = i * sizeof(datUnitWork_t) + (u32)&((dds3GlobalWork_t *)0)->unitwork;`
  then use `((datUnitWork_t *)((u8 *)DDS3_GBWK + ofs))->flag`. When the
  displacement split is wrong (0xA40 + 0x10 instead of 0xA50 + 0), reach the
  array through a view struct whose member sits at a 16-aligned top-level offset.

## Report

End with one line per function you worked on: `MATCH`, or `NO` with a short
reason (what still differs).
