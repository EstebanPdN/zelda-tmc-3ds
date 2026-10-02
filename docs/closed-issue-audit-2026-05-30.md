# TMC PC-Port — Closed-Issue Regression & Completeness Audit (2026-05-30)

_94 closed issues triaged; each fix located in git/CHANGELOG and verified against current HEAD; similar-bug leads adversarially noted. Read-only — no issues were commented on._

## Summary

A REGRESSION + completeness audit of 94 closed issues in the Minish Cap PC port. Each fix was located in current HEAD code; at-risk fixes were adversarially re-verified against the live source.

- **Verified fix-present: 81 / 94.** The vast majority of closed-issue fixes are still present and correct at HEAD.
- **No-code-fix (environmental / external-only): 11.** Resolved by build-side or CI changes, or are purely environmental (no source fix to regress).
- **Cant-determine: 2.** Require a reporter retest on the current binary.
- **CONFIRMED regressions / missing fixes against HEAD: 0.** The provided confirmed-regression list is empty; every spot-check of an at-risk fix held.

Dominant clusters by issue count: **render-texture (26)** and **crash (18)** together account for nearly half of all closed issues, followed by missing-entity-asset (13), gameplay-softlock (11), and enemy-behavior (9).

Although no closed fix has regressed, the audit surfaced a substantial set of **similar-unfixed leads** — same-class hazards that are latent, intentionally deferred, or unverified at adjacent sites. Two of these are concrete, code-confirmed latent bugs (not regressions of the closed issue, but live defects in sibling code): the **#101 Action3 child==NULL NULL-deref** in `mandiblesProjectile.c`, and the **#128 GoronMerchantShopManager `itemActive[]` offset mismatch** in `itemForSale.c`. The most systemic recurring risks are the **#98 enemy-subclass +4 padding** family (many un-audited subclasses), the **~900-animation extractor-loss** class (salvage reverted; only the runtime padding net remains), the **`RegisterRoomEntity` single-mirror** fragility (#12/#14), and **merge-regression-prone** fixes that have already been clobbered by upstream syncs once (#34/#36/#108).

## Regressions & missing fixes

**None confirmed.** The list of regressions/missing fixes verified against HEAD is empty. Every at-risk closed-issue fix that was adversarially re-checked was found still present and correct in the current source. The spot-checks held:

- The merge-regression-prone palette/ROM-memcpy fixes (#34 Mt Crenel colors, #36 Cave of Flames platform, #108 Mt Crenel BG memcpy in `Port_LoadRom`) — each previously dropped by an upstream sync — are present at HEAD.
- The `RegisterRoomEntity` `kind != ENEMY` mirror that #12 and #14 depend on is present.
- The Type3-door `context == NULL` guard (#37/#114, `houseDoorExterior.c`) is present at HEAD.
- The #142 `GetFuserId` `u32` declaration restore is in place.
- The #98 padding template and the newly-fixed sibling sites (`torchTrap.c`, `sensorBladeTrap.c`, committed `0b03ccd49`) are present.

Note: several items below in *Similar-unfixed leads* are **live latent defects in sibling code** (notably #101-Action3 and #128-GoronMerchant), but they are not regressions of the closed issue they relate to — the closed fix itself remains intact. They are tracked as leads, not as regressions.

## Similar-unfixed leads

Deduped across the 60+ raw leads. Ordered roughly by concreteness/severity.

| Class | From issue(s) | Unfixed sibling to check | Why it matters |
|---|---|---|---|
| NULL-deref on detached child (no early-return after `DeleteThisEntity`) | #101, #33, #87, #88 | `src/projectile/mandiblesProjectile.c:136-141` `MandiblesProjectile_Action3`: `entity = super->child; if (entity==NULL){DeleteThisEntity();}` then derefs `entity->next` with no `return` | **Code-confirmed live NULL-deref.** `DeleteThisEntity` (`entity.c:605`) does not return/longjmp — falls through, so `entity->next` derefs NULL in the same detached-mandibles scenario. #101 fix only covered Action1/`sub_080AA270`/OnCollision. Matches upstream zeldaret exactly (unguarded there too). |
| Widened-Manager field offset mismatch (`u8 before[0x20]` to reach a field that moved on PC) | #128, #98 | `src/object/itemForSale.c:158` writes `((ModifiedParentEntity*)super->parent)->unk_20[subtimer]=0xff`; parent is `GoronMerchantShopManager`, whose `itemActive[3]` is at GBA 0x20 but PC 0x38 (widened `Manager` base) | **Code-confirmed wrong-but-safe write.** The 0xff sold-item write misses `itemActive[]`, lands in the manager's widened pointer block. Loop at `goronMerchantShopManager.c:89` never sees `itemActive<0` → Goron-Kakera sold-state/flag tracking broken on PC. No `#ifdef PC_PORT` / `PORT_STATIC_ASSERT`. |
| `field_0x86` flag-misalignment shortcut loss (intentionally reverted siblings) | #89 | `HittableLever`, `PushableGrave` — flag-restore reverted in `99c7038bc`/`1260954ad`, only the rock re-applied (`e1aff8093`) | Lever/grave likely **still lose their shortcut flag on room reload** via the same `field_0x86` misalignment. Dropped because the lever fix caused side effects; underlying bug class remains for those two types. |
| Scripted Type2 door with no `context==NULL` guard | #37, #38 | `src/object/houseDoorExterior.c:210-213` `HouseDoorExterior_Type2` calls `ExecuteScript`/`sub_080868EC` guarded only by `ENT_SCRIPTED`, unlike sibling Type3 (line 228) | If a scripted Type2 door's script ptr fails `Port_ResolveRomData`, `context` stays NULL and `ExecuteScript` SIGSEGVs — the exact condition that motivated the Type3 fix. Currently unguarded. |
| Enemy-subclass +4 PC_PORT pad (Entity base + byte-counted `unused1[N]` aliasing Enemy fields ≥0x7c) | #98, #35, #119, #54, #63 | `darkNut.c` (`unused1[12]`→unk_74..7c), `doorMimic.c` (`[16]`→0x78/7a/7c/7e), `keaton.c` (`[12]`→0x74/76/78/7b), `leever.c`, `bladeTrap.c` (`[12]`→0x74), `helmasaur.c` (`[16]`→0x78/79); Fortress/Palace of Winds enemies (#63 video) | Strongest candidates whose named fields are actually read by AI. Symptom: "enemy ignores player / never wakes" or sprite-chunks. Class is open-ended; not verified per-enemy whether each is observably broken. |
| ~900-animation extractor-loss (salvage reverted; only runtime padding net) | #4, #8, #11, #32, #96, #124 | Any animation not through the two-pass runtime padding path, or `dataSize%4!=0` (skipped `port_asset_loader.cpp:1868`); Gyorg/Vaati cutscene/MazaalHand/mushroom-carry/teleport-icon/shop-top frames | Extractor salvage `28fbfa9ff` reverted as regression (`8731a8075`); no replacement landed. `#45` pad only covers anims ending on a loop frame — "trailing bytes"-flagged cutscene-only sprites may still extract short (CHANGELOG 267-269/275/303/325). |
| `RegisterRoomEntity` single mirror (`kind != ENEMY`, GenericEntity pre-union pad 0xAC-0xAF) | #12, #14 | ~30 subclass structs with a flag at GBA 0x86 all depend on this one mirror; the matheo sync already edits this file | A future upstream sync rewriting `RegisterRoomEntity` would regress #12 (boss reward not spawning) and #14 (Elder door) **together, silently, no crash**. Enemy-kind subclasses with a 0x86 flag are excluded by the guard and still mis-read. ROM offset `0xDF94C` is USA-only. |
| Merge-regression-prone fixes already clobbered once by upstream sync | #34, #36, #108 | `Port_LoadRom` palette/ROM memcpy (#108, dropped twice: PR#60, `5987bf9b1`); `gPalette_549+0xD0→gPalette_562` (#34); platform table (#36, dropped PR#60, restored `9a9158e7b`) | No test guards these. High-value re-verify targets after the next matheo/upstream merge — the documented fixed-then-regressed pattern. Class: any `gPalette_NNN + offset` cross-symbol read relying on sequential linker placement. |
| Packed 4-byte GBA pointer table declared as native `T*[]` in src/ but stored as `u8[]` in `data_const_stubs.c` (#91 follow-up) | #16, #36, #91 | Grep `extern (const) T* g[A-Z_]+[` in src/ vs `u8[]` definition in `port/data_const_stubs.c`; fn-ptr tables `void(*const[])(...)` (only 4 gleerok tables got native shadows) | 4→8 stride splices adjacent GBA pointers into a bogus 8-byte address. `gUnk_08111154` (#91) is the only hitbox-table instance found; fn-ptr tables and any remaining data tables are the open part of the class. |
| Unbounded fn-ptr dispatch by property-derived byte (root cause unfixed) | #29, #30 | `Foo_Actions[super->action]`/`_Types[super->type]` without bound check — e.g. `bigBarrel.c:45` `BigBarrel_Types[this->type]`, `beanstalk.c:89` `gUnk_08120E3C[super->type2]` | Maintainer states the ROOT cause (`EntityData` reading wrong byte for type2 high half) is **unfixed**; only `HouseDoorExterior` got the clamp. Any sibling dispatcher indexing a small static table by a garbage property byte crashes identically. |
| EWRAM cross-symbol bridge arithmetic (`gMapData…Special ± 0xNNNN`, `gUnk_020… ± 0xNNNN`) | #102, #103 | Grep `gMapData...Special\s*[+-]\s*0x[0-9a-f]{4,}` and `gUnk_020...\s*[+-]\s*0x[0-9a-f]{4,}` in any newly-ported BG-render code | Three confirmed sites fixed; class open to any future decomp. On PC the two symbols are separate allocations → bridged offset lands in unrelated/unmapped memory. |
| `SetVBlankDMA` without paired `DisableVBlankDMA` (HBlank-DMA cleanup, #103) | #6, #103 | `vaatiAppearingManager`, `steamOverlayManager`, `lightRayManager`, `pauseMenuScreen6`, the 4 iris/circle window effects in `common.c` — share the BG2PA DMA path | `9bdf7801f` (`port_hdma_unregister` in `sub_0801E104`) + `Subtask_FadeOut` net cover most; any future manager arming `SetVBlankDMA` on a path not routing through either re-opens the class. None found live at HEAD. |
| `gArea.filler6` ↔ `gUnk_020342F8` aliasing (delayed-entity spawn) | #82, #83, #104 | `physics.c:66` still `MemClear(&gArea.filler6[...])` (dead residue after the PC_PORT clear at :63); any NEW reader of `gArea.filler6` added during an upstream sync | All current readers (`whirlwind.c`, `pinwheel.c`, `cutsceneMiscObject.c`, `npc.c`) are PC-guarded. A new unguarded reader would silently re-break delayed-entity spawning. |
| Hardcoded GBA EWRAM scan address read without `Port_ResolveEwramPtr` | #57, #72 | Any `(u16*)0x02021f72`-style raw EWRAM deref; figurine showcase had a multi-path follow-up (#72, `049441ed2`) | Crashes identically. Surface had multiple sub-paths; worth a grep for other hardcoded `0x020…` casts. |
| Raw `gAreaRoomHeaders[area][room]` deref without `Common_GetAreaRoomHeaderSafe` | #56 | `subtaskFastTravel.c::sub_080A6EE0` reads it directly (noted in #53 commit); grep for other raw double-index derefs lacking NULL guard | Exposed if a future extractor change makes another area's header JSON absent. |
| `IsDuckingSfx` only recognizes `SFX_ITEM_GET` | #22 | heart-container fanfare, big-item-get, secret-found chime; other SFX on `MUSIC_PLAYER_1E` / high-priority `gSongTable` players | High-priority jingles that naturally starved BGM channels on GBA will NOT duck BGM on the port → same #22 symptom. None added at HEAD. |
| Read-only ROM hitbox + per-frame field write (no mutable clone) | #19 | Grep `super->hitbox->(offset_x|width|height) =` across `src/enemy` for enemies that set `super->hitbox = definition->ptr.hitbox` then mutate it | Same crash class as #19 South Hyrule loading-zone; sibling enemies not audited. |
| USA-only hardcoded ROM/IWRAM offsets (region portability) | #10, #24, #12, #108, #34 | IWRAM→ROM delta `0x050AC28C` (shadow #10 / shoes-overlay #24 / grass #24); `RegisterRoomEntity` `0xDF94C` (#12) | On EU/JP these translate to wrong/out-of-range pointers → silently NULL (shadows/overlay absent, no crash) — unhandled region case, not a USA bug. |
| `extern Dialog gFoo[]` / typed array in src/ but raw `u8[]` in `data_const_stubs.c` | #31 | The Dialog packed-vs-typed mismatch class; `gUnk_additional_*_<RoomName>` zeroed-BSS stub affects ~262 callsites in `roomInit.c` — only Library1F fixed | Other rooms referencing named `gUnk_additional_*` symbols may still load zeroed entity lists (wrong/blank dialog or missing entities). |
| RELOAD_ALL partial re-init can't rebuild decoration (#139 class) | #13 | Deepwood boss re-entry; `src/game.c::GameMain_Update` RELOAD_ALL vs full-re-init path | If #13 regresses, the #139 dark-room full-re-init gate (`gArea.lightType != 0`) is the established remedy. |
| Interrupted state-specific manager loading non-global gfx/palette (pause-menu return) | #131, #23 | Any manager that loads non-global gfx like the barrel did and gets interrupted by pause menu; only `GAMEMAIN_BARRELUPDATE` special-cased in `RestoreGameTask` | Same vanish symptom (#131). Latent class, not a confirmed live bug; `Subtask_FadeOut` net covers some but not all map/menu return paths (#23). |
| EntityData row carrying raw `{ NPC, 15/0x0F, ... }` pool/flags byte (the #135 typo shape) | #135 | `worldEvent19.c:24-25` (BIG_GORON ×2), `worldEvent23.c:13` (FOREST_MINISH) | Same 0x0F value that was a typo for the bookshelf NPC. May be intentional (position-only path); spot-check those NPCs aren't silently missing a Speak/cutscene trigger. No reported symptom. |
| GBA Div SWI r1-remainder side effect via `FORCE_REGISTER(u32 r1, r1)` | #7 | Grep `FORCE_REGISTER` `r1` elsewhere; any other BCD/digit encoder reusing the pattern | Uninitialized remainder on PC (no Div SWI). `DecToHex` (#7) was the BCD case; siblings would mis-encode digits. |
| Script stub resolving to zero-filled BSS (no PORT_SCRIPT routing) → softlock | #123, #55 | An Ezlo opening-cutscene script if stubbed in `port_linked_stubs.c` without PORT_SCRIPT routing | Same softlock class as `windTribespeople.c` (#55). Not verified for the Ezlo path. |
| Debug-warp center coords landing in geometry | #65 | `BuildWarpPage` entries using generic `(0x80,0xC0)` center, e.g. Hyrule Town line 231 | Usability annoyance only (re-warp if you land in a wall), not a crash; same "warp lands wrong spot" class as #65. |
| Kinstone-fusion-reveal object (not delayed-entity whirlwind class) | #104, #113 | The #104 missing kinstone and the #113 SW-castle rock if it's a fusion-reveal object rather than whirlwind-class spawn | Not covered by the whirlwind fix (`da0f95f58`); unverified whether it spawns at HEAD. |
| Cross-process v1 quicksave pointer-to-rodata fixup (unfixable-without-bigger-changes) | #126, #120 | `FixupEntityPointers` (`port_quicksave.c:158`) only relocates `gEntities`, not pointer-to-rodata fields | Entity-addr range guards mitigate but don't fully resolve cross-process list corruption; documented limitation. |
| Busy-wait frame limiter spins a CPU core | #26 | `while(SDL_GetTicksNS()<deadline){}` is the sole limiter on >60Hz and fast-forward paths | Latent power/thermal complaint; code path present and correct but a Windows reporter retest would fully confirm #26. |

## Cluster overview

| Category | Count | Status of the class |
|---|---|---|
| render-texture | 26 | Largest cluster. Mostly resolved (BG-priority sort, palette/ROM memcpy, HBlank-DMA teardown, EWRAM-bridge). **Recurring**: the ~900-animation extractor-loss (#32/#96), merge-regression-prone palette offsets (#34/#108), USA-only region offsets, interrupted-manager vanish (#131). |
| crash | 18 | Mostly resolved via NULL-guards / range guards / clamps. **Open root cause**: wrong-room-property-index dispatch (#29/#30) is symptom-fixed only. **Live latent**: #101 Action3, scripted Type2 door (#37). |
| missing-entity-asset | 13 | Split between asset-extractor loss (recurring, salvage reverted) and `RegisterRoomEntity` mirror fragility (#12/#14). Path (b) gRomData fallback recovers indexed cases; multi-chunk tables for un-indexed areas with old bundles still truncate (#28/#36/#40). |
| gameplay-softlock | 11 | Largely resolved. **Intentionally unfixed siblings**: HittableLever / PushableGrave shortcut-flag loss (#89). KeyValuePair terminator (#5) class — verify `gMapActTileToSurfaceType` carries its own `{0,0}`. |
| enemy-behavior | 9 | The #98 +4-padding family is the live recurring class; several un-audited subclasses (#119). GetNextFunction health==0 dispatch is a shared chokepoint that broke two ways (#20 vs #35) — regression-prone on future edits. |
| env-build | 6 | All no-code-fix / build-side or environmental (glibc floor via ubuntu-22.04 runner, FMT_HEADER_ONLY, MinGW static runtime, exe-dir asset probing, sounds.json tarball). No source regression risk. README still lacks Steam Deck guidance (#9). |
| feature-ux | 3 | Resolved. #7 (DecToHex remainder), #27 (tree-stump affine direction — symmetric re-regression risk if a contributor "fixes" by intuition again). |
| input-perf | 3 | Resolved (#26 fast-forward, #49 run-speed). #49 is a deletion with no positive guard → merge-regression watchpoint. #26 awaits a Windows retest. |
| audio | 2 | #22 (item-get duck — narrow `SFX_ITEM_GET`-only recognition, class open) and #115 (intro audio — no defect found, coefficients correct). |
| debug-menu | 2 | Both resolved (#52 max hearts 20, #65 Link's House coords — #65 is geometric-center usability only). |
| other | 1 | — |

**Fully-resolved classes:** env-build (all environmental/CI), HBlank-DMA teardown (#103 root-cause guard covers the family), the EWRAM cross-symbol bridge (3 confirmed sites, no live remainder), the #91 hitbox-table splice (only `gUnk_08111154`, converted).
**Recurring/open classes:** #98 enemy +4-padding, ~900-animation extractor-loss, `RegisterRoomEntity` single-mirror, merge-regression-prone palette/memcpy fixes, unbounded property-byte fn-ptr dispatch (#29/#30 root cause).

## Coverage & caveats

**No-code-fix (11)** — resolved without a source change, so nothing to regress; no re-test needed beyond environment confirmation:

- env-build: #2 (exe-dir asset probing — shared AssetSearchRoots/cwd fix), #9 (Steam Deck — purely environmental; README guidance still missing), #15 (libfmt — FMT_HEADER_ONLY + glibc floor), #17 / #50 / #59 (glibc version / sounds.json tarball / MinGW DLLs — all build/CI side).
- Plus the environmental/limitation items resolved by mitigation rather than a true fix: #126 (cross-process quicksave — documented unfixable-without-bigger-changes).
- #115 (intro audio — investigated, no defect found; high-pass coefficients and soft-clip correct).

**Cant-determine (2)** — require a reporter retest on a current build; no code site can be confirmed broken or fixed from source alone:

- **#26 (Windows fast-forward)** — fix was Linux-developed targeting the Windows 60Hz path; code path is present and correct, but only a Windows reporter retest fully confirms. The busy-wait limiter is a latent power/thermal note, not a correctness failure.
- **#96 (Vaati 2nd-stage disappears)** — if the disappearance was animation-data-driven (not transient), the still-open ~900-animation extractor gap could affect Vaati boss/cutscene frames. Worth a reporter retest on a current build with a fresh extract.

**What a human should manually re-test / act on:**

1. **Two code-confirmed live latent bugs** (not regressions, but real defects worth filing): #101 `mandiblesProjectile.c:136-141` Action3 child==NULL NULL-deref (add `return` after `DeleteThisEntity()`), and #128 `itemForSale.c:158` GoronMerchantShopManager `itemActive[]` offset (needs `#ifdef PC_PORT` field-offset fix + `PORT_STATIC_ASSERT`).
2. **Intentionally-deferred #89 siblings** — confirm HittableLever / PushableGrave shortcut-flag loss is still considered acceptable, or re-apply with the side-effect fix.
3. **After the next matheo/upstream sync** — re-verify the merge-regression-prone fixes (#34, #36, #108 `Port_LoadRom` memcpy; the `RegisterRoomEntity` mirror for #12/#14; the #49 run-speed deletion). These have the documented fixed-then-clobbered history.
4. **#98 +4-padding sweep** — audit the named enemy subclasses (#119: darkNut, doorMimic, keaton, leever, bladeTrap, helmasaur; #63: Fortress/Palace of Winds enemies) for "enemy ignores player / never wakes" or sprite-chunks symptoms.
5. **Region portability** — none of the USA-only hardcoded offsets (#10/#24/#12/#108/#34) are handled for EU/JP; flag if EU/JP support is ever in scope.
6. **#5 KeyValuePair terminator** — verify `gMapActTileToSurfaceType` (extern at `playerUtils.c:135`) carries its own `{0,0}` terminator rather than relying on linker adjacency.