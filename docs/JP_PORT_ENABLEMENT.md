# JP (BZMJ) version support on the PC port

*Status (2026-06-13): **booting and core-correct.** A JP build runs against the
retail JP ROM with all data tables resolving correctly — verified by boot test.
Remaining for full speedrun parity: Japanese text rendering and JP script-address
tables (see "Remaining gaps").*

The Minish Cap speedrun scene runs the **Japanese** version (RNG manipulations are
version-exclusive and were authored for JP). The decomp supports JP at the source
level (`#ifdef JP` throughout `src/`); this port now wires JP through the build and
runtime, and supplies the JP ROM data-table offsets.

## Build & run a JP port

1. **Provide a legal JP baserom** (`BZMJ`, SHA-1 `6c5404a1effb17f481f352181d0f1c61a2765c5d`,
   see `tmc_jp.sha1`) as `baserom_jp.gba` in the repo root. (Gitignored — never committed.)
2. **Build:** `python build.py --jp` (or `xmake f --game_version=JP -y && xmake build -y tmc_pc`).
3. **Run:** point the binary at the JP ROM. It auto-detects `JP (BZMJ)` and selects the
   JP offset table. Verified boot log:
   ```
   ROM region detected: JP (BZMJ)
   Using offsets for JP (game code: BZMJ)
   gMapData loaded (13482224 bytes from ROM offset 0x324710).
   Area data tables loaded (0x90 areas, 2-level pointers resolved).
   Entering AgbMain...
   ```

## What's wired (all committed, USA build unaffected)

- `ROM_REGION_JP`, `kRomOffsets_JP` (real values), `BZMJ` detection, JP/USA offset
  selection, JP-aware region messages — `port/port_config.h`, `port/port_rom.c`
- `port/port_offset_JP.h` (asset-blob offsets) + `#if defined(JP)` include — `port/port_main.c`
- `JP`/`JAPANESE` in `pc_versions` — `xmake.lua`; `--jp` + JP version entry — `build.py`
- JP-only symbol guards so the JP port links (`sub_0807FC24`, `sub_08088658` are
  USA-only in the decomp) — `port/port_script_funcs.c`
- `-I port` on the decomp ROM build's preprocess so committed `src/` port-includes
  resolve (header-resolution only; no PC code enters the ROM build) — `xmake.lua`

## How `kRomOffsets_JP` was derived (content-anchoring)

This tree's decomp ROM build is **non-matching** (port edits shift symbols ~0xC44),
so `build/JP/tmc_jp.map` is NOT a valid source — its addresses don't match the retail
ROM the port actually loads. Instead the offsets come straight from the **retail USA +
JP ROMs**: the USA addresses are known-correct, so each USA table is located in the JP
ROM. Per-field method (all 28 address fields, see the comment block in `port_rom.c`):

- **direct content-anchor** — unique 64-byte signature of the table start (version-stable
  tables: gfx/palette/map data);
- **pointer-table dereference / shift-search** — for tables of `0x08xxxxxx` pointers,
  find the single shift under which the whole pointer array is internally consistent;
- **text/translations cluster** — uniform `-0x33C` shift, fixed by 4 independent anchors.

Regional shifts grow monotonically with address (`0 → -0x260 → ~-0x338 → -0x33C →
-0x3D4`), each value corroborated by neighbours. The whole table is validated by the
boot test above (2-level area-pointer resolution across all 0x90 areas cannot succeed
with wrong offsets). `/tmp` scratch scripts that produced these aren't committed; the
final values + provenance live in `port_rom.c`. Count/size fields are content-invariant
(identical USA==EU) and carried over.

## Remaining gaps (JP runs but is not yet speedrun-faithful)

The game boots and core gameplay — rendering, RNG, rooms, movement, area data,
and scripted cutscenes/NPCs — is correct.

Resolved since the original writeup:

- **Script addresses (was: `port/port_scripts.h` hardcoded USA).**
  `Port_TranslateScriptAddr` (`port/port_script_addrs.c`) now remaps the
  **entire** script bytecode section — all 576 data scripts, USA→EU/JP by exact
  symbol lookup in the retail maps — not just the ~100 `GBA_script_*` macros.
  This covers scripts referenced only by raw entity-data blobs in
  `port/data_const_stubs.c` (e.g. `script_ZeldaOutsideLinksHouse`, the prologue
  Business Scrub orchestrators). Those were previously untranslated and ran
  garbage bytecode on JP/EU — the cause of the intro Zelda "wrong position" and
  the prologue scrub not spitting.
- **`port/port_script_funcs.c` native-call table.** Has per-region tables
  (`sScriptFuncTable_JP` / `_EU`, selected at runtime via `REGION_IS_*`); the
  two USA-only functions are excluded for JP.

Still needing JP treatment:

1. **Japanese text rendering.** Basic kana render (the intro `こっちよ` textbox
   is correct), but the full JP text *system* (kanji 2-byte encoding, font
   widths/layout) is unverified — audit menus/dialogue against this JP build.

This is the remaining path from "JP boots" to "JP speedrun-faithful". Pair a JP
build with `--console-parity` for hardware-equivalent JP runs.

## Related

- Console-Parity mode (`--console-parity`) — run-integrity switch.
