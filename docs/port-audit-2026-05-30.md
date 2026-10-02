# TMC PC-Port Codebase Audit — 2026-05-30

Full latent-bug & port-correctness audit of the GBA→x86-64 port. Method: 15
hazard-class finders fanned out over `src/` (619 C files) + `port/`, each
candidate then adversarially verified against the real code (default-to-not-a-bug,
checking for `#ifdef PC_PORT` guards, the `IsColliding` host-pointer range guard,
same-alias-only reads, and reachability).

**Totals: 106 candidates verified → 39 confirmed, 67 rejected** (misread / already
guarded / unreachable). The 63% rejection rate is the point — in this codebase most
candidates are *latent, not live*, and the audit's value is the separation.

| Severity | Count | Live | Latent |
|---|---|---|---|
| Critical | 0 | – | – |
| High | 8 | 4 | 4 |
| Medium | 15 | 7 | 8 |
| Low | 16 | 1 | 15 |

**Dominant classes:** HDMA-cleanup leaks (one shared root cause), cross-process
quicksave stale pointers (one family), `#98` enemy-subclass padding, and the
NULL-deref/`#136`-sibling family.

---

## Fix status — all findings addressed (2026-05-30, build-verified)

Every actionable finding now has a code change (each builds clean via
`xmake build -y tmc_pc`); the remainder are confirmed-safe no-ops the verifier
itself flagged, kept as-is to avoid regressing working code. All GBA paths stay
byte-identical (`#ifdef PC_PORT` guards).

**Code changes applied:**

| Finding(s) | File | Fix |
|---|---|---|
| HDMA-leak root cause + lightManager:42 + whiteTriangleEffect:128 | `src/common.c:987` | `sub_0801E104` now calls `port_hdma_unregister(0)` under `PC_PORT` — tears down the leaked HBlank channel at the source, fixing the whole `sub_0801E104`-only-teardown class. |
| Live cannonball crash | `src/projectile/cannonballProjectile.c:84` | Index real `AngryStatueManager::field_0x20[]` + NULL-guard parent. |
| Vaati Evaporate DMA | `src/manager/vaatiAppearingManager.c:144` | `DisableVBlankDMA()` on Action2 exit, mirroring Action1. |
| #136 male-eye | `src/enemy/gyorgMaleEye.c:28` | `parent==NULL` early-return. |
| #136 vaati-arm | `src/object/vaati3Arm.c:60` | guard `parent`/`parent->myHeap` before `heap[4]`. |
| #91 interactable scan | `src/playerUtils.c:1426` | `Port_IsValidHostPtr` guard (new shared helper in `port/port_rom.h`). |
| Quicksave family (scroll.c:103 + port_quicksave.c:158 + player.c:556) | `port/port_quicksave.c`, `src/player.c` | `Snapshot_Restore` re-resolves stale `camera_target` and player hitbox on cross-process load; new `Port_RestorePlayerHitbox`. |
| Crash-handler safety (port_bugreport.cpp:529/601) | `port/port_bugreport.cpp` | write `backtrace.txt` first (before heavy steps); drop `SDL_ShowSimpleMessageBox` from the signal handler. |
| #98 padding | `src/enemy/torchTrap.c`, `src/enemy/sensorBladeTrap.c` | `+4` filler (eyegore template). |
| lilypad rails | `port/port_rom.c` | ROM-resolve `gLilypadRails[0..2]` (USA-gated). |
| NULL/Create-fail guards | `gleerokProjectile.c`, `pullableMushroom.c` (×2), `madderpillar.c`, `picolyteBottle.c`, `wizzrobeWind.c` (×6) | guard parent/child/Create* derefs. |
| OOB clamp | `src/enemy/gyorgFemaleMouth.c:87` | clamp pointer-table index to [0,7]. |
| unused-subsystem guard | `src/npc/npc5.c:967` | NULL-guard `messageData` (empty `gZeldaFollowerText` stub). |
| pak bounds | `port/port_asset_pak_loader.cpp` | validate every entry name-span at mount (`Open()`). |
| map-asset bounds | `src/beanstalkSubtask.c:239` | clamp copy length to real `assetSize`; LZ77 header guard. |
| audio data race | `port/port_audio.c:141` | relaxed atomic load of `gMain.muteAudio`. |

**Deliberate no-ops (verified safe — changing them would risk regression for no benefit):**

- `octorokBossObject.c:149` — half-pointer write is type-stable latent (the slot is only ever re-read via the same `*(s32*)&` integer alias on type-8 entities, never as a pointer). Adding a struct field risks layout/assert regressions.
- `chuchu.c:692` — OOB index is passed as a value (never dereferenced) and lands in a mapped 800-byte stub; clamping would *diverge* from GBA's own OOB behavior.
- `port_asset_loader.cpp:1267` — room-property extent is acceptable for the checksum-validated ROM (consumer length unknown here).
- `mode2.c:16` — DISPCNT/OBJ_1D per-frame-vs-per-scanline divergence has no observable effect (TMC never HDMA-mutates DISPCNT on the title path); a speculative change to the PPU per-scanline hot loop (a managed layered patch) isn't worth the perf/regression risk.
- Already-guarded (preserve across upstream syncs): `talon.c:163`, the four `#102` EWRAM-bridge managers, `phonograph.c:202`, `rollingBarrelManager.c:61`.

---

## Tier 1 — Live crashes & corruption (fix first)

### 1.1 `src/projectile/cannonballProjectile.c:84` — HIGH, **the one reachable gameplay crash**
`sub_080AB634` does `Entity** entities = (Entity**)&this->parent->zVelocity;` then
iterates `entities[0..3]` and derefs them. The parent is an `AngryStatueManager`
whose four statue pointers live in `field_0x20[4]`. On GBA `&parent->zVelocity` ==
`parent+0x20` == `field_0x20[]` and the 4-byte slots line up exactly. On PC the
pointer-widened `Manager` base shifts: `&parent->zVelocity` == `parent+0x28`
(= `Manager.child`), **0x10 bytes before** the real `field_0x20[]` at `0x38`, read as
8-byte slots — `entities[1..3]` splice unrelated fields into non-NULL wild pointers
that pass the `!= NULL` check, reach `IsColliding`, which derefs `that->collisionLayer`/
`that->hitbox` **before** its range guard → SIGSEGV.
- **Reachable:** normal play — deflecting a cannonball to destroy AngryStatues (`action==2` after reflection).
- **Fix:** guard `this->parent != NULL` and, under `#ifdef PC_PORT`, use
  `Entity** entities = ((AngryStatueManager*)this->parent)->field_0x20;` (correct base+stride). Keep the `&parent->zVelocity` form GBA-only.

### 1.2 Cross-process quicksave stale-pointer family — HIGH
F6-loading a `state_*.bin` written by a **prior process** (different ASLR base) crashes,
because `FixupEntityPointers` (`port/port_quicksave.c:158`) only relocates words whose
value lands **inside the `gEntities` window**. Everything pointing elsewhere stays stale:
- **`src/scroll.c:103`** — `gRoomControls.camera_target == &gPlayerEntity` (a static global, below `gEntities`) is never fixed; `Scroll1` derefs `camera_target->x` **every frame** → SIGSEGV on the first post-load frame. Same staleness hits `screenTileMap.c`, `script.c`, `message.c` camera_target derefs.
- **`port_quicksave.c:158`** — the structural gap: pointer-to-`.rodata` (hitbox), pointer-to-other-pool, and the separately-memcpy'd regions (`gPlayerEntity`/`gRoomControls`) are never passed to the fixup.
- **`src/player.c:556`** (LOW) — `Player->hitbox = &gPlayerHitbox/&sMinishHitbox` (`.rodata`) stale; unguarded tile-probe (`playerUtils.c:2586`) & interactable-scan (`1426`) derefs crash. (This one is the documented cross-process quicksave limitation.)
- **Fix:** in `Snapshot_Restore`, after the memcpy loop, (a) re-assign `gPlayerEntity.base.hitbox` from live `.rodata` per `gPlayerState` form, and (b) record the saved base addresses of the fixed-global regions and shift any restored pointer landing in a saved region's range to the new base — `camera_target` is the most reliably-hit instance. A `scroll.c` range-guard fallback to `&gPlayerEntity` is a cheap belt-and-suspenders.
- Same-process F5→F6 is the documented no-op early-return and stays safe.

### 1.3 `src/common.c:987` — MEDIUM, **HDMA-leak root cause (one-line fix kills a class)**
`sub_0801E104` sets `gScreen.vBlankDMA.ready = FALSE` but **never calls
`port_hdma_unregister(0)`**, so the PC HBlank channel (`s_channels[0].active`) stays
armed and `port_hdma_step_line` keeps firing the stale src table into the last dest
register every scanline until some later `DisableVBlankDMA` runs. (On GBA this was
benign — `VBlankIntr` does `DmaStop(0)` every frame; on PC `DmaStop` is a no-op.)
- **Fix:** add `#ifdef PC_PORT port_hdma_unregister(0); #endif` inside `sub_0801E104`. This neutralizes the whole family below at the source: `vaatiAppearingManager.c:144`, `lightManager.c:42`, `whiteTriangleEffect.c:128`, and any other `sub_0801E104`-only teardown. Safe for the iris-fade-end site which wants exactly this.

The dependent leaks (visual BG/window corruption in the next scene, no crash):
- **`vaatiAppearingManager.c:144`** (MED, live) — Evaporate/Action2 leaves BG3HOFS HDMA armed; Apparate/Action1 already has the `DisableVBlankDMA()` guard (line 113), Action2 was missed. Palace-of-Winds cutscene, no fade transition to rescue it.
- **`lightManager.c:42`** (LOW, latent) — mid-room dark-room-end leaves WIN0H light-circle HDMA armed.
- **`whiteTriangleEffect.c:128`** (MED, latent) — screen-wipe leaves WIN0H armed.

Fixing `common.c:987` covers all three; targeted `DisableVBlankDMA()` at each site is the alternative.

### 1.4 `port/port_bugreport.cpp:529` & `:601` — MEDIUM, **the crash handler can hide other crashes**
The SIGSEGV/SIGABRT/SIGBUS handler runs `std::filesystem`, libpng+`fopen`/`fprintf`,
`std::ofstream`, `std::malloc`, **and `SDL_ShowSimpleMessageBox`** — none async-signal-safe.
If the original fault was inside `malloc`/glibc-stdio/SDL (very common — the present path
is SDL-heavy) while a lock is held, the handler self-deadlocks or re-enters the corrupt
allocator → **no bundle, no core dump**. This is meta-severe: it blinds you to the very
crashes the audit is meant to catch.
- **Fix:** write `backtrace.txt` (Crash IP / fault addr / frame chain) via raw `open()`/`write()` syscalls **first**, then attempt the heavy PNG/save/state steps best-effort. Drop `SDL_ShowSimpleMessageBox` from the signal path (gate behind an env var or restrict to the F9 manual path). Keep `std::raise(sig)` reachable so the OS core dump still fires.

---

## Tier 2 — Live functional / visual bugs (no crash)

| File:Line | Class | Issue | Fix |
|---|---|---|---|
| `src/object/lilypadSmall.c:67` | empty-native-array | `gLilypadRails` (`port_linked_stubs.c:1639`) is a zero-init `void*[32]` never populated; on GBA it was a 3-entry ROM pointer table. `type2>=0x80` small lilypads + kinstone-fused rails get NULL → platform never moves along its rail (no crash — `UpdateRailMovement` NULL-guards). | Populate from ROM in `port_rom.c` (resolve `0x080FEDA4/DDA/DF8` via `Port_ResolveRomData`), like `gFigurines`/`gPalette_549`. Also fixes the `kinstone.c:344` reader. |
| `src/enemy/torchTrap.c:47` | `#98` padding | No `+4` pad for `Enemy::child`; reads `unk_82/unk_84` (yPos/spritePtr params) from PC `0xAA/0xAC` but framework wrote them at `0xAE/0xB0`. Every torch trap mis-positions / fires wrong direction. 27 spawns across dungeons. | `eyegore.c` template: `#ifdef PC_PORT u8 filler[0xc + 4]; #else u8 filler[0xc]; #endif`. Add a `PORT_STATIC_ASSERT_OFFSET`. |
| `src/enemy/sensorBladeTrap.c:41` | `#98` padding | Same — `unk_84/unk_86` read from `0xAC/0xAE` instead of `0xB0/0xB2`; seeds launch target from `xPos/yPos` instead of params → fires wrong distance. | `#ifdef PC_PORT u8 unused1[16 + 4]` per the `eyegore.c` template + assert. |
| `libs/ViruaPPU/src/mode2.c:16` | mode1/mode2 divergence | mode2 (title/affine path) captures `DISPCNT` + `OBJ_1D` once per frame; mode1 re-reads per scanline. A mid-frame HDMA-DISPCNT change isn't honored on the title path. Low impact (TMC doesn't HDMA DISPCNT on the title). | Move the `DISPCNT`/`obj_1d` read inside the per-line loop after the pre-line callback, mirroring `mode1.c:965-968`. |

> The two `#98` cases are the only ones that surfaced from a *sampled* sweep of 256
> `Entity base;` subclasses — see Coverage gaps for the systematic follow-up.

---

## Tier 3 — Latent crashes (guard now; one dispatch-change from live)

These do **not** crash today (guarded, unreachable, or read only via the same alias),
but each is a thin invariant away from a live SIGSEGV. Fix shape is the standard
`#ifdef PC_PORT` NULL-guard / clamp / unpack.

| File:Line | Class | Why latent / when it goes live | Fix |
|---|---|---|---|
| `src/playerUtils.c:1426` | null-deref (HIGH) | Interactable-scan derefs `entity->hitbox->width` with **no NULL check and no range guard** — the exact unguarded path that re-crashed cat #91 three weeks after the collision-path fix. Live the instant any interactable has a NULL/stale/spliced hitbox. | `if (iObject->entity->hitbox == NULL) continue;` + optional range-check vs `IsColliding`'s `[0x100000000000,0x800000000000)` window. This loop is the one interactable path lacking the shield. |
| `src/enemy/gyorgMaleEye.c:29` | `#136` sibling (HIGH) | Derefs `super->parent->next` with no `parent==NULL` guard — a **missed member** of the committed #136 Gyorg fix family (`gyorgFemaleEye.c:38-45` has it). | `#ifdef PC_PORT if (super->parent == NULL) return; #endif` at function top, mirroring `gyorgFemaleEye.c`. |
| `src/object/vaati3Arm.c:61` | `#136` sibling (HIGH) | `sub_080A0640` reads `((Entity**)parent->myHeap)[4]`; boss-death `vaatiArm.c:1159` sets `entities[4]->base.myHeap = NULL` while keeping it alive as the arm's parent → `((Entity**)NULL)[4]` → SIGSEGV on the death sequence. | `#ifdef PC_PORT if (this->parent == NULL || this->parent->myHeap == NULL) return; #endif`. |
| `src/object/octorokBossObject.c:149` | half-ptr write (HIGH) | `*(s32*)&this->helper = grid_index` truncates the 8-byte `HelperStruct*`; **latent** because type-8 entities only ever re-read it via the same `*(s32*)&` alias, never as a pointer (the pointer-using cases are disjoint by `super->type`). The documented canonical case. | No live fix; to eliminate cleanly, store the grid index in a dedicated `#ifdef PC_PORT s32` scratch field instead of aliasing the pointer slot. |
| `src/object/pullableMushroom.c:222` | null-deref (MED) | `(sub_0808B1F0(this, super->child) < 8) \|\| (super->child == NULL)` — calls the helper (which derefs arg2) **before** the NULL short-circuit. | Reorder to NULL-first. **Also guard `sub_0808AFD4` (line 319)** `(super->child)->direction` which faults earlier on the child-creation-failed path. |
| `src/enemy/madderpillar.c:117` | null-deref (MED) | Six `CreateEnemy` calls, then `entN->parent = super` with no NULL check; mid-chain slot exhaustion → NULL store. | NULL-check each return; bail+delete head if any fails. |
| `src/projectile/gleerokProjectile.c:52` | null-deref #51 (MED) | `super->parent->y - super->child->y` unguarded; `child` from `heap->entities[0]` can be NULL if `CreateEnemy` failed at boss init. | NULL-guard **both** parent and child at the `type != 3` block top. |
| `src/enemy/wizzrobeWind.c:126` | null-deref (MED) | Writes `super->parent->spriteSettings.draw` unguarded; `parent` is the wind's own created projectile — NULL when `EnemyCreateProjectile` fails on a full pool. | NULL-guard `super->parent` in Action1/2/3 + `sub_0802FA88` (also reached from Init before parent is set). |
| `src/npc/picolyteBottle.c:98` | null-deref (LOW) | Three unchecked `CreateNPC` returns in Init. Line 98 itself is safe (type-1 children always have parent). | NULL-check the Init `CreateNPC` returns before `npc->parent = super`. |
| `src/enemy/chuchu.c:692` | OOB index (LOW) | `gUnk_080CA2B4[frame & 0xf]` exceeds the 10-byte logical table; harmless (800-byte stub keeps it mapped, result never dereferenced). | Clamp index to 0..9 only if the jiggle offset looks visibly wrong; else leave. |
| `src/enemy/gyorgFemale.c:73` | OOB index (LOW) | `gUnk_080D28CC[tmp+1]` reaches index 8 of an 8-entry table; scalar over-read is harmless. The **pointer-table** read `gUnk_080D28AC[tmp]` at line 87 is the one to clamp. | `#ifdef PC_PORT` clamp `tmp` to [0,7], especially for line 87. |
| `src/npc/npc5.c:953` | empty-native-array (MED) | `gZeldaFollowerText` (`port_linked_stubs.c:1653`) zero-init, never populated → `messageData = NULL`. Subsystem appears unused. | If revived: resolve from ROM. Otherwise a `#ifdef PC_PORT if (messageData == NULL) break;` guard at line 967 suffices. |

---

## Tier 4 — Robustness (asset parsing / concurrency)

| File:Line | Class | Issue | Fix |
|---|---|---|---|
| `port/port_asset_pak_loader.cpp:203` | bounds (MED) | `CompareEntry` builds `string_view(base + name_table_offset + name_offset, name_length)` with no per-entry validation against the mapped size. A truncated/corrupt `.pak` → OOB read on every binary-search probe. | Validate the name span in `CompareEntry` with u64 math; on overflow return a deterministic ordering (treat as ordered-after) so the search stays well-defined. Pass `size_` in (currently `static`). |
| `src/beanstalkSubtask.c:239` | bounds (MED) | `MemCopy(src, dest, dataDefinition->size)` trusts the JSON `size`; the real asset byte count (`assetSize`, already fetched line 220) is discarded. Truncated `.bin` → OOB heap read; LZ77 branch worse (length in header). | Mask flags → `cleanLen`, copy `min(cleanLen, assetSize)` + warn/continue on mismatch; require `assetSize >= 4` before `LZ77UnComp*`. Mirrors the palette guard at `port_asset_loader.cpp:1679`. |
| `port/port_asset_loader.cpp:1267` | bounds (LOW) | gRomData room-property fallback checks `offset < gRomSize` but not `offset + extent`; consumer can walk past the ROM end. Low risk (ROM is checksum-validated). | Cap room-property walkers at `gRomData + gRomSize`. |
| `port/port_audio.c:141` | data race (LOW) | `gMain.muteAudio` read on the SDL audio thread without `sStateMutex`. Init-only write makes it nearly benign. | Route mute through `sState` under the lock, or make it `_Atomic`. |

---

## Verified clean — no action (false-positive class confirmations)

These were flagged by finders but verification confirmed they are **already guarded** —
documented here so future syncs don't "re-fix" or accidentally regress the guard:

- **EWRAM-bridge `#102` family, all guarded:** `horizontalMinishPathBackgroundManager.c:67`, `bigGoron.c:204`, `verticalMinishPathBackgroundManager.c:56` (`VMP_ClampBgDelta`), `rollingBarrelManager.c:252`. **Preserve these `#ifdef PC_PORT` branches across upstream syncs.**
- **`talon.c:163`** (`*(u32*)&unk_84` ptr-arith) — already has its PC_PORT branch.
- **`phonograph.c:202`** — static `Font` with verbatim GBA addresses, neutralized by the `text.c` resolve-on-use path (same pattern: `figurineMenu.c:453/559`, `gameUtils.c:123`).
- **`rollingBarrelManager.c:61`** — BG2PA affine HDMA, has the PC_PORT `OnExitRoom DisableVBlankDMA`.

---

## Coverage & gaps — what a human should still sweep

1. **`#98` padding — only sampled.** Two cases surfaced (`torchTrap`, `sensorBladeTrap`) from a *sampled* pass over 256 `Entity base;` subclasses. A **systematic** pass should compute every enemy subclass's post-`0x7c` field offsets vs. its `PORT_STATIC_ASSERT_OFFSET`s (or flag the absence of asserts) — this class hides silently (wrong behavior, no crash) so it won't show up in bug reports.
2. **The `struct-offset-asserts` dimension returned nothing confirmed** — that means *either* good existing coverage *or* under-checking. Worth a dedicated pass: enumerate structs with embedded pointers after non-pointer fields that lack any `PORT_STATIC_ASSERT`, and spot-check a sample of existing asserts for staleness.
3. **Other unguarded EWRAM-bridge `#102` siblings.** The fixes for the *guarded* references named `templeOfDropletsManager.c:563`, `lightRayManager.c:252`, and a `steamOverlayManager` arming site as needing the same treatment — these weren't independently confirmed as findings. Targeted check recommended.
4. **HDMA sites believed covered by the `Subtask_FadeOut` net** (`bossDoor.c:229`, `enterPortalSubtask.c:161`, `fade.c:128/303`) — assumed safe but not exhaustively traced. The `common.c:987` root-cause fix makes them moot regardless.
5. **PPU mode1/mode2 mirroring beyond the DISPCNT case** — only the DISPCNT/OBJ_1D capture divergence surfaced; a fuller diff of window/blend/mosaic/clip logic across the two files would confirm parity.
6. **67 rejected candidates** were not re-examined; verification was single-pass adversarial. If a specific subsystem matters, a second-opinion pass on its rejections is cheap.

---

## Recommended fix order

1. **`common.c:987`** — one line, kills the whole HDMA-leak class (Tier 1.3 + the `lightManager`/`whiteTriangle`/`vaati` dependents).
2. **`cannonballProjectile.c:84`** — the only live reachable gameplay *crash*. ~5 lines.
3. **`port_bugreport.cpp`** reorder — restores visibility into all other crashes.
4. **Quicksave family** (`scroll.c:103` + `port_quicksave.c:158` + `player.c:556`) — one `Snapshot_Restore` change covers all three.
5. **Latent HIGH guards** — `playerUtils.c:1426`, `gyorgMaleEye.c:29`, `vaati3Arm.c:61` (each a small `#ifdef PC_PORT` guard, mirrors existing committed patterns).
6. **`#98` padding** — `torchTrap.c`, `sensorBladeTrap.c` (visible-broken in-game), then the systematic sweep (gap #1).
7. **`lilypadSmall.c:67`** — ROM-resolve the rails (visible-broken).
8. Remaining medium/low NULL-guards and asset-bounds hardening as a batch.
