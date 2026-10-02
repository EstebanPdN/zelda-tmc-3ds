# GBA Accuracy Audit — PC Port (2026-06-15)

Goal: make `tmc_pc` behaviorally identical to the GBA game. Method: multi-agent
workflow comparing handwritten GBA ASM ground truth (`asm/src/*.s`, `asm/lib/*.s`)
against the PC C/C++ reimplementations (`port/*.c`, `port/*.cpp`, `#ifdef PC_PORT`
paths in `src/`). Each candidate divergence was checked by two adversarial verifiers
(a behavioral-equivalence skeptic and a hardware-necessity skeptic).

## ⚠️ Coverage caveat — this run is PARTIAL

The workflow hit API rate-limiting and then the session token limit (resets 21:00
Europe/London). **Only 3 of 12 planned subsystems actually completed a finder pass**,
the completeness-critic and synthesis agents never ran, and 4 verdict agents died
mid-flight. This document is the recovered, hand-synthesized result of what *did*
run. The remaining 9 subsystems are **unaudited** (see below) and must be re-run.

| Subsystem | Finder status | Findings |
|---|---|---|
| GBA BIOS math (Div/Sqrt/Affine/CpuSet) | ✅ done | 5 |
| Trig / fixed-point / distance / direction | ✅ done | 0 (verified clean) |
| Entity update & draw / fade | ✅ done | 6 |
| Collision engine | ❌ rate-limited | — |
| Tile & collision-data lookups | ❌ rate-limited | — |
| Player movement physics | ❌ rate-limited | — |
| RNG / `Random()` | ❌ rate-limited | — |
| Audio / m4a | ❌ rate-limited | — |
| Interrupt dispatch & frame timing | ❌ rate-limited | — |
| Save / checksum / flags | ❌ rate-limited | — |
| Struct offset / pointer-width alignment | ❌ session limit | — |
| PPU rendering accuracy | ❌ rate-limited | — |

Run artifacts (for resume): workflow `wf_768d1856-0f0`, journal at
`.claude/projects/-home-sian-tmc-build-pc/<sid>/subagents/workflows/wf_768d1856-0f0/journal.jsonl`.

---

## Executive summary

- **8 findings survived adversarial verification; 3 were dismissed/unverified.**
- **1 HIGH-severity** must-fix (entity-update iteration order) — independently
  re-verified against source in this document.
- **2 low-severity** must-fix visual bugs (shoes/grass + water-wading overlays).
- **3 ObjAffineSet findings** collapse into one low/cosmetic item (affine sprite
  rotation doesn't use the BIOS algorithm).
- **2 acceptable** divergences (BgAffineSet rotation unused; fade clamp).
- The whole **trig/fixed-point/distance** subsystem was checked and is **faithful**
  — notably `CalcDistance` does NOT substitute `sqrtf` for the GBA integer approx
  (that classic trap was avoided).

### Top fixes, priority order — STATUS
1. ✅ **Entity `next`-pointer recompute timing** (HIGH) — fixed `port_draw.c`.
2. ✅ **Per-entity `gEnemyTarget` clear missing** (HIGH) — verified (r7=&gEnemyTarget
   via gUnk_080026A4, `code_08001A7C.s:863-873`); fixed `port_draw.c`.
3. ✅ Tall-grass overlay reads fractional byte (low, visual) — fixed `port_draw.c`.
4. ✅ Water-wading overlay table stride/size (low, visual) — fixed `port_draw.c`.
5. ✅ ObjAffineSet → BIOS 256-step Q1.14 sin LUT (low, cosmetic) — fixed `port_bios.c`.
6. ⬜ Base-Y shift + gOAMControls[0x12] for water/0x19 — DEFERRED: `r7` identity is
   ambiguous (it's a `>>6`-unpacked coord; needs `sub_080B2874` + prologue analysis to
   map to base vs overlay Y safely). Current water-overlay +2 behavior preserved.

All five applied fixes built clean (USA, multi_region) and boot-verified headless
(20s/~1200 frames, exit 137, no crash/`ENT-GUARD`/sanitizer). **Uncommitted.**
These touch only `port/` (PC_PORT) — the GBA ROM target is unaffected.

---

## Must-fix accuracy bugs

### 1. Entity update advances `next` from a stale pre-update pointer — HIGH ✅ FIXED
- **Status:** fixed in `port/port_draw.c` (single late `current_entity->next` read,
  mirroring `ldr r0,[r11,#8]` / `ldr r4,[r0,#4]`); NULL-corruption guard retained.
  Built clean (USA, multi_region) and boot-verified headless (~1200 frames, exit 137 =
  full run, no crash/`ENT-GUARD`). Uncommitted. Root cause confirmed via the
  `UnlinkEntity` trace below.
- **asmRef:** `asm/src/intr.s:742-747` · **cRef:** `port/port_draw.c:258-285`
- **GBA:** after the entity update returns, reloads `r0 = gUpdateContext.current_entity`
  and computes `next = current_entity->next` (`ldr r4,[r0,#4]` at `_080B2230`) —
  **recomputed after the update, from `current_entity`**.
- **Port:** captures `Entity* next = entity->next;` **before** the update (line 262),
  then `entity = next;` (line 285) — a pointer snapshotted before the entity (or the
  update context) could mutate it.
- **Impact:** when an entity relinks itself, deletes itself, or changes
  `current_entity` during its own update, the GBA follows the *fresh* successor while
  the port follows a stale one → entities skipped or double-processed within a frame.
  That perturbs same-frame interactions and **RNG consumption order**, i.e. desync vs
  console. Both lenses rated must-fix/HIGH; re-confirmed here against source.
- **Fix:** drop the pre-update `next` capture; after the collision check compute
  `next = gUpdateContext.current_entity->next` (mirror `ldr r4,[r0,#4]`). Validate
  against `DeleteEntity`'s handling of `current_entity` so self-deletion still walks
  the list correctly (that is *why* the GBA reads `next` late).

### 2. `gUpdateContext`/`gEnemyTarget` not cleared per entity — HIGH (UNVERIFIED)
- **asmRef:** `asm/src/intr.s` (`mov r0,#0; str r0,[r7]` per `next_entity` iteration)
  · **cRef:** `port/port_draw.c:258-285`
- **GBA:** zeroes a context pointer (`[r7]`, finder identified as `gEnemyTarget`) at
  the top of **every** entity iteration, before the update runs.
- **Port:** the per-entity loop has no equivalent clear.
- **Impact:** if real, a stale enemy-target/context value leaks from one entity's
  update into the next → wrong enemy AI targeting/behavior. The ASM clearly contains
  the per-iteration store; **both verifier agents for this finding died to rate-limits
  before returning a verdict**, so severity is the finder's (high) estimate, not yet
  adversarially confirmed. **Re-verify first, then fix.**

### 3. Tall-grass overlay frame uses fractional position byte — low (visual)
- **asmRef:** `asm/src/intr.s:~1397` · **cRef:** `port/port_draw.c:927-929`
- **GBA:** selects the grass-rustle frame from the **integer** position byte
  (`x.HALF.HI` / byte 0x2e). **Port:** reads byte 0x2d (`x.HALF.LO`, the Q16.16
  *fractional* part) → wrong frame pattern.
- **Fix:** `u8 xb=(u8)entity->x.HALF.HI; u8 yb=(u8)entity->y.HALF.HI; frame=(u8)((xb^yb)&6u);`

### 4. Water/shallow-water "wading" overlay table stride & size wrong — low (visual)
- **asmRef:** `asm/src/intr.s:1397-1399` (32-entry byte-offset table; `linker.ld:209-210`
  confirms `0x80` = 32×4 bytes) · **cRef:** `port/port_draw.c:49,1006,1073`
- **GBA:** indexes a **32-pointer** table by byte offset `(shoeState&0x30) + 2*frame`.
  **Port:** uses a 16-entry array with the wrong stride → the water case never renders.
- **Fix:** size `sShoesOverlayPtrs[32]`, load 32 entries (guard
  `gRomSize > kShoesTableRomOffset + 128`), index by the byte offset.

### 5. `ObjAffineSet` doesn't reproduce the BIOS algorithm — low (cosmetic)
Three findings (`objaffineset-trig-precision-table`, `-rounding-truncation`,
`-zero-scale-guard`) reduce to: the port computes affine sprite matrices with
full-precision libm `cos/sin` over all 65536 angles, truncates toward zero, and forces
zero scale → 1; the GBA BIOS uses a 256-step Q1.14 sin LUT (`idx=(θ>>8)&0xFF`),
`asr #14` (floor), and no zero-scale guard.
- **cRef:** `port/port_bios.c:1024-1057` (guard at `1038-1039`). Reaches real callers
  (e.g. `octorokBoss` via `SetAffineInfo`, `src/code_0805EC04.c:48`), so rotated affine
  sprites land on slightly different pixels than console. No state/RNG/save impact.
- **Fix:** add `static const s16 sBiosSinLut[256]` (Q1.14, `round(sin(i·2π/256)·0x4000)`);
  per entry `idx=(θ>>8)&0xFF; cos=LUT[(idx+0x40)&0xFF]; sin=LUT[idx];
  pa=(s16)((sx*cos)>>14); pb=(s16)((sx*sin)>>14); pc=(s16)((-sy*sin)>>14);
  pd=(s16)((sy*cos)>>14);` and delete the zero-scale guard.

---

## Acceptable / necessary divergences (consciously accept)

- **`BgAffineSet` ignores the rotation field** (`port_bios.c:996-1005`). Both in-game
  callers pass `alpha=0`, so output is bit-identical today. Latent only — would matter
  if a rotated BG2 affine is ever used. Optional hardening, not a bug.
- **Fade brightness index is clamped** in C but read unbounded in ASM
  (`src/fade.c:62-63` vs `intr.s:664-668`). Only reachable brightness values are 0–2;
  identical outputs in practice. Clamp is safe.

## Dismissed (false positives)

- **`CpuFastSet` 32-byte granularity** — the BIOS rounds the word count up to a
  multiple of 8; the port does not. **Dead code: no callers in TMC**, so zero
  observable impact. Both lenses: false-positive. (Optionally harden anyway.)

## Verified-accurate (checked, no action)

- **Trig / fixed-point / distance / direction** — `CalcDistance` (`port_math.c:35`)
  reproduces `Sqrt((dx²+dy²)<<8)` with the integer floor-sqrt (`port_bios.c:941`),
  matching SWI 8; **no `sqrtf` substitution**. Sine-table indexing and direction
  quantization match.
- **Scalar BIOS arithmetic** — `Div`/`DivAndModCombined`/`Sqrt`: truncation, remainder
  sign, `INT_MIN/-1` special-case, and floor-sqrt over the full `u32` range all match.
- **Fade buffer 256-color blend** (`Port_MakeFadeBuff256`) and OAM/sprite attribute
  packing (`RenderSpritePieces`/`ResolveOamDrawPriority`) — faithful.

---

## Inline audits completed after the workflow run (7 of the 9)

**Interrupt dispatch & frame timing — ✅ CLEARED (necessary Tier-3, no fixable gap).**
The game enables only `VBLANK | VCOUNT | GAMEPAK` (`interrupts.c:48`). Handler table
(`data/const/interrupts.s`): VBLANK→`VBlankIntr`, VCOUNT (scanline 80)→`HBlankIntr`. Crucially
`HBlankIntr` is NOT a visual split — it's `REG_DISPSTAT` re-arm + `m4aSoundMain()`, i.e. the
**audio mix tick**. So: VBlank DMA/OAM/scroll is handled on PC (`PerformVBlankDMA` PC_PORT
branch — and the GBA's redundant double-copy is correctly dropped); the scanline-80 audio tick
is handled behaviorally by the PC audio backend (can't replicate exact scanline timing without
cycle accuracy — necessary Tier-3). No HBLANK-proper or Timer interrupts are enabled. No fix.


**Tile & collision-data lookups — ✅ CLEARED.** `WorldToTilePos` matches `arm_GetTileType*`
exactly: origin subtract (`gRoomControls` +6/+8), `(roomX<<22)>>26` = `(px>>4)&0x3F` per axis,
`tileX + (tileY<<6)` stride; `GetTileTypeAtTilePos` does `mapData[idx]`, `>=0x4000` early-out,
`tileTypes[idx]` — same as the ASM two-step lookup. `NormalizeTilePos & 0xFFF` is a no-op for
the computed range (PC OOB-segfault guard). `GetMapLayerForLayer` (`layer==2?top:bottom`) is
correct since `LAYER_BOTTOM=1`/`LAYER_TOP=2` are the only values used (no layer 0/3 lookups).
No fix.


**Player physics — ✅ audited; velocity add/clamp faithful + 1 table fix.**
`AddPlayerVelocity` (`port_gameplay_stubs.c:476`) matches the ASM (`vx+=x`/`vy+=y` then clamp
each), and `ClampPlayerVelocityAxis` clamps to `±0x180` identically to `ClampPlayerVelocity`
(player.s `0x180`/`0xFFFFFE80`, boundary-inclusive on both). `sVelocities1` (±3) and
`sIceVelocities` (±10) match the ASM tables byte-for-byte.
- ✅ **FIXED — `sVelocities3` was truncated to 8 of the ASM's 16 bytes.** Indexed by
  `animationState & ~1` (0..14), so `animationState >= 8` on the jump/`diff>4` path read past
  the array (garbage velocity). Restored the second half `{19,18,18,16,16,17,17,19}` from
  `player.s velocities3`; builds + boots clean. 6th fix this session.


**Collision — mostly faithful; ONE candidate divergence flagged (not fixed).**
Verified matching: `ram_CollideAll`'s filter sequence vs `arm_CollideAll` (disable-flag,
`collisionMask&(1<<m)` reducing to the low byte, `collisionLayer`, the `0x40` iframe gate,
downward OTHER iteration); `IsColliding`'s X/Y/**Z+depth** bbox math vs `arm_CalcCollision`
379–421 (default depth 5, `BB.depth` under the `0x10` 3D flag); and `PortCalcCollision`'s
matrix lookup (`34*hitType+hurtType`, ×12, `0xFF` redirect) + knockback vs the `_080B1E74`
path.
- ✅ **CANDIDATE RESOLVED — false alarm, no divergence.** `arm_CalcCollision`'s internal
  `BBOX_SOLID (0x20)` dispatch (intr.s:422–432 → table `_080B1FEC`) calls one of
  `sub_080B1F30`/`F5C`/`F88` — all three are **pure computations with no stores** (they
  compute a position delta into r0/r1 and `bx lr`). Execution then **falls through to the
  damage path** `_080B1E74`, which overwrites r0/r1 via `arm_CalcCollisionDirection`. So the
  solid dispatch is a **dead computation whose result is discarded** — observably a no-op.
  `ram_CollideAll` running only the damage path (via `PortCalcCollision`) is therefore
  **correct**. (The real solid-pushback system is the separate `CalcCollisionStaticEntity`
  @0x08004484, called from movement code — present and used in the port.) **Collision fully
  cleared.**
- Minor: outer loop iterates `gCollidableList` + a player/item/`spritePriority.b2` filter to
  approximate the GBA's registered-hitbox list `gUnk_02018EA0`; iteration *order* may differ,
  affecting same-frame multi-collision tie-breaking. Already a known approximation in-code.


**Save / checksum / flags — ✅ CLEARED.** `CalculateChecksum` (`checksum += *data ^ size`),
the flag get/set functions (`flags.c`), and the save addresses/sizes are shared `src/`
code with no PC_PORT guard → identical to GBA. `CheckBits` is bit-exact (verified above).
The one divergence — `ParseSaveFileStatus` PC branch `checksum2 == (u16)(-checksum1)` vs GBA
`checksum1 + checksum2 == 0x10000` — is the correct #89 fix: the GBA form spuriously rejects
a valid save when `checksum == 0` (1/65536), and the sibling `VerifyChecksum` (line 360,
unguarded) already used the correct `(-temp>>0x10)` form. The native save writer preserves the GBA save format.


**RNG / `Random()` — ✅ CLEARED, fully GBA-accurate, no fix.**
`Port_Rng_Advance` = `(state*3 >> 13) | (state*3 << 19)` = `ROR(state*3,13)`,
`Port_Rng_Output` = `state>>1` — bit-exact to the ASM (`code_08000E44.s:18`:
`lsls/adds` ×3, `rors #13`, `lsrs #1`). Seed `gRand = 0x1234567` (`src/main.c:83`) is
**unguarded** → shared by GBA and PC at the same boot point; nothing re-seeds divergently;
`rng_golden_test` passes. Conclusion: the desync risk was never the RNG algorithm — it was
*consumption order*, which finding #1 fixed.

**Struct-offset guards — ✅ tractable subset closed; no active bug found.**
124 pre-existing `PORT_STATIC_ASSERT_OFFSET` guards all pass (build succeeds → those
structs verified). Found **19 enemy structs with the `+4` "Enemy::child PC growth" pad but
NO assert** (armos, beetle, businessScrub, crow, enemy4D, enemy50, eyegore, madderpillar,
mazaalHead, mazaalMacro, miniFireballGuy, miniSlime, moldorm, moldworm, sensorBladeTrap,
torchTrap, vaatiTransfigured, wallMaster, wallMaster2). Added a guard to each (first custom
field, `PC = GBA + 0x2C`); **all 19 compile clean → padding confirmed correct**, now
regression-locked. The *remaining* struct-offset risk class — structs that need padding but
have **none** — was audited and closed. Found **20 enemy structs** with `Entity base` followed by a generic `filler`
array that were missing `#ifdef PC_PORT` growth padding for the 8-byte `child` pointer.
All 20 were patched with `+ 4` PC padding and protected with `PORT_STATIC_ASSERT_OFFSET`s.

**Player `sub_08008AA0` sine-velocity path — ✅ CLEARED.**
The C port uses `gPlayerState.vel_x = gSineTable[dir * 8]` and `dir * 8 + 64`. This is mathematically
identical to the GBA assembly's `lsls r2, r2, #4` and byte-addressing. The `dir*0x10` notation
in previous notes referred to the *byte offset*, which corresponds exactly to `dir*8` array elements
for an `s16` array. Verified bit-exact.

## Unaudited areas — remaining sweeps (2 of 9, both deep Tier-3)

1. **Audio / m4a** — channel cap, voice-stealing order, ADSR/timing (behavioral reimpl;
   hard to verify accuracy without listening — workflow's domain).
2. **PPU rendering accuracy** — priority/blend/window/mosaic/mode2 latch.

(The collision `BBOX_SOLID` candidate was investigated and **resolved as a false alarm** —
see Collision section.)

**Resume:** re-run `Workflow({scriptPath: ".../gba-accuracy-audit-wf_768d1856-0f0.js",
resumeFromRunId: "wf_768d1856-0f0"})` after the 21:00 reset — the 3 completed finders
and 15 verdicts return cached; only the 2 missing dimensions + critic + synthesis run.

## Recommended fix order (dependency-aware)
1. Re-verify finding #2 (per-entity context clear), then fix #1 and #2 together
   (both in `ram_UpdateEntities`, the update-loop core).
2. Visual overlay fixes #3, #4 (independent, safe, quick wins).
3. ObjAffineSet BIOS LUT (#5) — self-contained.
