# Dead-code candidates

Generated from the latest `xmake build -y tmc_pc` warning stream after the
maintainability refactor on 2026-06-13.

This is a review list, not an auto-delete list. The decompiled engine still has
many symbols whose names/uses are not fully understood, so each candidate needs
human confirmation before removal.

## Likely safe removals in port-only code

- `port/port_m4a_stubs.c:112` — `static void ResetPlayerInfo(MusicPlayerInfo* mplayInfo)`
  - Compiler warning: `defined but not used`
  - Port-only shim file. No known references in the PC build.

- `port/port_asset_loader.cpp:1650` — `static void LoadPalettesNative(const u8*, s32, s32)`
  - Compiler warning: `defined but not used`
  - Asset loader currently uses the extracted/ROM paths directly; this helper is
    orphaned after the loader refactor.

## Needs decomp / gameplay review before removal

- `src/fade.c:157` — `static void sub_08050024(void)`
  - Compiler warning: `defined but not used`
  - Shared engine code. Removing it may be correct, but this belongs to the
    decomp-side review flow, not blind port cleanup.

- `src/manager/hyruleTownTileSetManager.c:42` — `gHyruleTownTileSetManager_festivalRegions1`
  - Compiler warning: `defined but not used`
  - Shared gameplay/asset selection code. Review against the original ROM /
    upstream decomp before removing.

## Intentionally excluded from this list

The build also reports many `unused-variable` / `unused-but-set-variable`
warnings and several unused helper functions in legacy UI work. Those
were not included here because they are mixed with in-progress feature work and
would create churn without a clearer ownership decision.
