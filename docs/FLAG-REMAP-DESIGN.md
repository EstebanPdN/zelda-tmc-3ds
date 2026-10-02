# M8 design — cross-region flag-ordinal remap (save portability)

Status: **design, not yet implemented.** Unblocks M8 in `docs/MULTI_REGION.md`.
M5 (region-isolated saves) already prevents cross-region *wipes*; this is what a single
save needs to *load and play correctly under any ROM*.

## The problem (recap)

A local-flag's save-bit position is `offset + enum_ordinal`:

```c
// src/flags.c
u32  CheckLocalFlagByBank(u32 offset, u32 flag) { return CheckBits(gSave.flags, offset + flag, 1); }
void SetLocalFlagByBank  (u32 offset, u32 flag) { if (flag) WriteBit(gSave.flags, offset + flag); }
```

The `LocalFlags1` (and other bank) enums in `include/flags.h` have **different sets/counts of
enumerators per region** — first divergence at `flags.h:192` (USA `MIZUUMI_00_CAP_0` = 1 enum;
EU/JP `HIKYOU_00_T0/T1` = 2), cascading so every flag after it sits at a **different ordinal**
in EU/JP than USA. The fat binary is compiled with USA ordinals; a loaded EU/JP ROM's *scripts*
and *area data* reference flags by that region's ordinals. Any flag touched by both C code and a
script desyncs.

## Key insight: C code and scripts use DIFFERENT entry points

- **C gameplay** calls `CheckLocalFlag(FLAG_CONSTANT)` (`flags.c:12`) with a **compile-time USA
  ordinal** (the enum value baked into the binary).
- **Scripts** call through the opcode handlers `ScriptCommand_CheckLocalFlag` /
  `ScriptCommand_SetLocalFlag` / `ScriptCommand_ClearLocalFlag` (+ `…ByBank`) in `src/script.c`
  (table at `script.c:553-579`), which read the flag value **out of the ROM bytecode** — i.e. in
  the **active region's ordinal space**.

Both funnel into the same `flags.c` functions, but with different ordinal conventions. The fix
is to translate **only the script path**, leaving C code and the save layout untouched.

## Design: canonical (USA-superset) ordinal space

1. **Canonical space** = USA ordinals, extended with the EU/JP-only flags appended after the
   USA maximum of each bank (so no USA ordinal moves; the save stays back-compatible). The save
   (`gSave.flags`) is always stored in this canonical layout. C code already speaks it.
2. **Per-region remap tables** (`region ordinal → canonical ordinal`), one per flag bank, for EU
   and JP. USA is identity. Generated at build time by compiling each bank's enum twice (USA and
   EU/JP) and diffing — same machine-derived approach as `gen_script_addrs.py`. EU/JP-only flags
   map to their appended canonical slot; USA-only flags simply never appear in the EU/JP table.
3. **Translate in the script handlers only.** In `ScriptCommand_CheckLocalFlag` /
   `SetLocalFlag` / `ClearLocalFlag` / `…ByBank`, before calling the `flags.c` function:
   ```c
   #ifdef MULTI_REGION
   flag = Port_RemapLocalFlagOrdinal(bank, flag);  // region ordinal -> canonical; identity on USA
   #endif
   ```
   C-code call sites are unchanged (already canonical). The save bit is therefore always written
   and read at the canonical ordinal regardless of which ROM is loaded → a save is portable.

## Why this is correct

- **Within one region** (incl. M5-isolated saves): C code (canonical) and translated scripts
  (region→canonical) agree → self-consistent, and byte-compatible with existing USA saves.
- **Cross-region** (the M8 win): load a USA save under EU. C code uses canonical ordinals
  (correct); EU scripts translate EU→canonical (correct). Both hit the same canonical bits.
- **Area data** that references flags by ordinal (not just scripts) must be audited too — if any
  area-data flag reads bypass the script handlers, they need the same translation at their
  resolve point. **This is the main open risk** and the first implementation task: enumerate all
  flag-ordinal consumers, confirm scripts + area-data are the only region-ordinal sources.

## Implementation plan

1. **Audit** every reader of `gSave.flags` ordinals: confirm the only region-ordinal sources are
   the `ScriptCommand_*LocalFlag*` handlers and any area-data flag fields. (Grep `CheckLocalFlag`,
   `SetLocalFlag`, `*ByBank`, plus area/room property flag use.)
2. **Generate** `port_flag_ordinals.c` (`tools/gen_flag_ordinals.py`): compile each divergent
   bank's enum for USA and EU/JP, emit `kFlagRemap_EU[bank][regionOrdinal] = canonicalOrdinal`
   (and JP). Decide canonical = USA-superset; append EU/JP-only flags.
3. **Add** `u32 Port_RemapLocalFlagOrdinal(u32 bank, u32 flag)` (identity unless EU/JP) and the
   `#ifdef MULTI_REGION` translation calls in the four `ScriptCommand_*` handlers.
4. **Extend the save layout** if the canonical superset exceeds the current `gSave.flags` size
   (it must already hold the EU/JP-only flags — verify `SaveFile` flag-bank sizing).
5. **Verify**: per region, run a script that sets a flag then have C code read it (and vice
   versa); confirm persistence across save/reload; confirm a USA save loaded under EU/JP reads
   the same logical flags. Add to the multi-region regression suite (M7).

## Effort / risk

Multi-day. The mechanism is small; the **risk is completeness** (missing a region-ordinal
consumer → silent flag corruption) and **verification breadth** (flags drive quest progression).
Gate everything on `MULTI_REGION`; single-region builds keep compile-time ordinals and are
untouched. Do NOT ship without the verification suite.
