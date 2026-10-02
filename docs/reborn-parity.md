# Minish Cap Reborn — parity status in tmc_pc

Cherry-pick port of features from
[Admentus64/The-Minish-Cap-Reborn](https://github.com/Admentus64/The-Minish-Cap-Reborn).

All runtime-toggleable features are at **F8 → Reborn** ribbon tab.

## Implemented (12 active / 15)

| # | Feature | Toggle | Default | Hook |
|---|---|---|---|---|
| 1 | Shells cap 9999 | `REBORN_FEAT_SHELLS_9999` | ON | `src/itemUtils.c::ModShells` |
| 2 | L + R → Pegasus Boots | `REBORN_FEAT_EQUIP_LR_BOOTS` | ON | `src/game.c::GameMain_Update` |
| 3 | L + SELECT → Ocarina | `REBORN_FEAT_EQUIP_LSELECT_OCARINA` | ON | same handler |
| 4 | L + A/B → secondary item | `REBORN_FEAT_SECONDARY_LAB` | ON | `src/playerUtils.c::UpdateActiveItems`. Fires the items bound to the port's two soft slots (port-config storage; no save-format change). |
| 5 | SELECT-hold in item menu → equip secondary | `REBORN_FEAT_SELECT_HOLD_EQUIP` | ON | `src/menu/pauseMenu.c` |
| 6 | Skip Ezlo hint on resume | `REBORN_FEAT_NO_EZLO_ON_RESUME` | ON | `src/game.c::GameMain_InitRoom` + just-resumed flag |
| 7 | Library reopen after Four Sword | `REBORN_FEAT_LIBRARY_REOPEN` | ON | `src/roomInit.c` |
| 8 | Hero Mode (2× damage) | `REBORN_FEAT_HERO_MODE` | OFF | `src/gameUtils.c::ModHealth`. Surfaced via F8 toggle instead of Reborn's file-select-L. |
| 9 | Skip Ezlo tutorials | `REBORN_FEAT_SKIP_EZLO_TUTORIALS` | OFF | `src/manager/ezloHintManager.c` |
| 10 | Figurine 1-shell odds floor at 20% | `REBORN_FEAT_FIGURINE_MIN_RAISED` | ON | `src/object/figurineDevice.c::sub_0808826C` — remaps the base 1-shell curve 100%→0% to 100%→20% |
| 11 | ~~Rupee Like overhaul~~ — **removed** | `REBORN_FEAT_RUPEE_LIKE_OVERHAUL` (reserved) | — | removed; enum slot kept for config-bitmask stability |
| 12 | Mirror Shield earlier | (no toggle — additive) | always | `data/scripts/veilFalls/script_BigGoron3.inc` — flag swap GAMECLEAR → EZERO_1ST |
| 13 | Oracle left-behind also offers charm | (no toggle — additive) | always | `data/scripts/hyruleTown/script_{Nayru,Farore,Din}Alone.inc` — branch into MovedIn charm paths |

## Not implemented (2)

### Joy Butterfly fusion earlier
**Status:** *Couldn't locate the change.* Diffed `src/kinstone.c`,
`data/scripts/kinstoneFusion/`, and `data/scripts/sanctuary/` — no
clear fusion-content diff found. Possibly buried in a fusion table
that lives outside the script tree. Would need targeted investigation.

### Improved text boxes
**Status:** Cosmetic; large scope. Reborn rewrote multiple recurring
text strings — those live in compiled text banks (`data/strings.s`
indirects into per-language `.bin` files). Diffing the asm shows only
build-system simplification, not content. A proper port would replace
specific entries in the text bank; not attempted.

### EU language restoration (excluded from the count)
Reborn re-introduced French / German / Italian / Spanish text + the
file-select language picker. That's a separate localization project
(~1+ week of data work). Out of scope.

## Notes

### License posture
These quality-of-life features are **ported from / modeled on**
Admentus64/The-Minish-Cap-Reborn (GPL-3.0). Some are reimplemented against the
zeldaret/tmc decompilation and the port's own subsystems (input, soft-slots,
config); the additive ones (features 12–13) are small control-flow edits to the
decomp's own scripts. Either way, they derive from the upstream Reborn feature
set.

Project Picori is licensed **GPL-3.0**, so these features are distributed under
the GPL-3.0 with attribution to the upstream — consistent with `LICENSE` and
`THIRD-PARTY-LICENSES.md`. (Earlier revisions of this file claimed a "clean-room
/ first-party / no-GPL-obligation" posture; that was incorrect and has been
removed in favor of honoring the copyleft.)

### Save format
No save format change. The L+A/B secondary items live in the port's own
soft-slot store (the port config file), not the GBA save, so legacy saves
load unchanged and nothing is written to `gSave`. Until you bind a soft
slot via SELECT-hold equip, L+A/B simply falls back to the normal A/B item.

### Differences from upstream Reborn
- **Hero Mode** is toggleable mid-run via F8 instead of being chosen
  on file select. Functionally equivalent; the file-select-L trigger
  in Reborn relies on a save-format flag we didn't need to add.
- **Skip Ezlo tutorials** is a runtime gate on
  `EzloHintManager_Action2` rather than removing the hint entries
  from `data/map/entity_headers.s`. Same observable behavior;
  reversible at runtime.
- **L + A/B secondary items** reuses the port's soft-slot store (the same
  X/Y/L2/R2 binding system) rather than touching the GBA save at all.

## How to test

1. Build: `xmake build -y tmc_pc`
2. Launch tmc_pc
3. F8 → "Reborn" tab — each feature is a checkbox with `(?)` tooltip
4. **Mirror Shield**: enter Veil Falls + interact with Big Goron
   right after meeting Ezlo (instead of needing GAMECLEAR)
5. **Oracle charm**: with only one oracle moved into the house, talk
   to the others "left behind" — they'll now offer the charm
6. **L + A/B**: in the pause menu, hold SELECT while pressing A/B on
   an item to bind it to soft slot 0/1. In-game hold L + tap A or B to use.
