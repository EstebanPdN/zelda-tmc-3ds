# Widescreen Phase 2 — design notes

**Status (2026-06-10, as of release 0.5.0):** Phase 2 is fixed and shipping.
The "half-black + garbage reveal" root cause was a one-line shadow-BG index bug
(fix detailed below). 0.5.0 release builds compile wide (`--widescreen_width=384`);
the feature is gated behind `config.json:widescreen_enabled` (default off,
runtime-toggleable via F8 → Display settings). The rest of this document is
retained as the design/history record — earlier "WIP"/"REVERTED" statuses are superseded.

Root cause: `Port_Widescreen_UpdateShadows` (port/port_linked_stubs.c) assigned
the shadow tilemaps to the wrong PPU BG index — hardcoded `shadow[1]=bottomMap`,
`shadow[2]=topMap`. But the engine renders the **bottom** map as **PPU BG2** and
the **top** map as **PPU BG1** (the map's `bgSettings->control` screen_base
selects the BG; `bottom.ctrl==bg2.ctrl`, `top.ctrl==bg1.ctrl`). So PPU BG1 (the
sparse overlay) read the dense bottom-map shadow → wrong tiles; PPU BG2 (the real
floor) read the sparse top-map shadow → mostly transparent → force-black. That is
exactly the "half-black + garbage reveal" every prior attempt fought. The earlier
residency-gate "fix" made it worse: it blacked reveal tiles whose index wasn't in
the engine's 32-col tilemap window, but the area tileset is resident in char-VRAM
regardless of that window, so it blacked legitimately-loaded tiles (the ~45%
black). No tile/palette loading subsystem was ever needed.

Fix (all in `port/port_linked_stubs.c`, gated `#if MODE1_GBA_WIDTH > 240`):
1. `Port_WidescreenPpuBgForControl()` resolves each map's PPU BG index from its
   control screen_base; `Port_Widescreen_UpdateShadows` assigns the shadow to
   that index (no hardcoded 1/2).
2. `Port_WidescreenShadow_Populate` indexing made exact vs the engine fill
   `ram_sub_080B197C_c`: shadow ROW index == VRAM buffer row `sr` holding map row
   `2*row16-1+sr` (was `world_row&31`, a camera-shifted wrong row); consumer base
   == VRAM tile_col of the first reveal column == `CLIP_X/8 + (BGHOFS>=8?1:0)`
   (was `ws_base_world_col & 31`, which drifted with the camera → far-edge wrap
   into stale cells). Verified: filled shadow == engine `gBG1Buffer` byte-for-byte.
3. Residency gate deleted. Hybrid signal simplified to a per-room width test —
   direct, per-room, flicker-free (no settle/measurement). *(2026-07-10: the
   signal is now `Port_Widescreen_FallbackNative`; the view width is capped at
   the ROOM width instead of falling back outright, so 256/272px rooms run
   true-wide at their full room width, and the fallback renders native 240
   presented pillarboxed at 3:2 — the stretched-240 fallback is gone.)*

**Update (2026-07-02):** the two worst runtime UX gaps are fixed:
- *Dialogue width-snap*: `Port_Widescreen_IsActive()` no longer gates on
  `MESSAGE_ACTIVE`/`CONTROL_DISABLED`. Dialogue stays wide; the PPU centers
  the BG0 textbox via the `virtuappu_mode1_ws_msg_*` rect knobs (published
  per-VBlank from `Port_Widescreen_UpdateShadows`, rect from
  `gMessage.textWindow*` + 1-tile border pad). The box's native columns are
  suppressed and redrawn shifted by `(W-240)/2`; the HUD right-anchor is
  suspended on box lines to avoid doubled fragments when a top-anchored box
  overlaps the rupee rows.
- *Overlay black-third (prologue storybook, pause)*: the present path now
  requires `Port_Widescreen_ShadowsLive()` (>=1 BG shadow registered this
  frame) before presenting wide. Overlay screens swap the BG control regs
  away from the maps, the shadow match fails, and the frame falls back to a
  native 240 crop.
- *Enemy cull literals*: cuccoAggr / gyorgChild / bombPeahat / takkuri
  edge tests now use `Port_Widescreen_EffectiveViewWidth()` (collapse to the
  GBA literals at 240).

Known limitations:
- ~~A *wide* room with *narrow content* stays in widescreen and shows a thin
  border edge at the far right.~~ **Resolved (2026-07-03):** the content-width
  signal exists — `Port_WidescreenScanContentPx` finds the rightmost
  non-empty tile column (both maps, room rect), ratcheted per room, and
  `Port_Widescreen_FallbackNative` (né ShouldStretch) falls back to native
  240 — presented pillarboxed, not stretched — when content < the room-capped
  view width.
  Survey result: the suspect rooms (field 0x03/0x08 = 480px, festival
  0x15/0x00 = 400px) actually paint edge-to-edge, so TMC may have no
  counterexample — the guard is a safety net. `TMC_WS_TRACE=1` logs
  width/content/mode per room. The *visible* artifact that motivated this
  item was actually stale-buffer leakage: the shadow fill clamped to the
  128-tile buffer instead of the room rect, sampling the previous room's
  tiles past the current room's extent — fixed alongside.
- ~~Widescreen is a fixed build-time width~~ **Superseded (2026-07-03) by
  true widescreen:** the presented view width now tracks the WINDOW's aspect
  every frame — `viewW = clamp(window_aspect x 160, 240, MODE1_GBA_WIDTH)`
  (`Port_Widescreen_TargetViewWidth`, fed by the present path via
  `Port_Widescreen_SetWindowPixels`). A 16:9 monitor fills exactly (288 px,
  zero letterbox), ultrawide caps at the framebuffer capacity (384 = 2.4:1),
  and a 3:2/4:3 window gets the authentic 240. Camera clamp, culling, HUD
  right-anchor, and textbox centering all key off
  `Port_Widescreen_EffectiveViewWidth()`, so world scale is CONSTANT across
  window sizes and room widths — no zoom jump at doors, no pillarbox flip.
  `--widescreen_width=N` now only sets the framebuffer CAPACITY (cap);
  native-240 builds still compile everything out. Headless captures pin the
  width with `TMC_WS_VIEW_WIDTH=<px>` (dummy-driver windows have no real
  monitor aspect).


**Historical status (2026-05-26 — superseded, see header):** ALL implementation work REVERTED to the
pre-Phase-2 state (commit `d3ff4c374`).  Reverted in-tree
hard-reset; the working code that was committed during attempts
(`564650014` .. `9be970693`) is recoverable from `git reflog` if a
future attempt wants to start from it instead of from scratch.

**This document is kept as the record of what was tried, what
broke, and why** — so the next attempt doesn't re-walk the same
dead ends.  See the "Progress log" at the bottom for the slice
history.

**TL;DR of lessons learned:**

- **Option A (port-side EWRAM shadow buffer + PPU patch)** is the
  cleanest path to BG widescreen.  Avoids TXT512x256 + VRAM-
  collision dead-end.  Got BG reveal working in normal gameplay.
- **Step F (clean black margin in non-game tasks)** required
  fixing `memset(bg_priority, 0xFF, ...)` in mode1.c AND mode2.c
  (was 0 — broke composite force-black check), plus clipping
  mode2's affine BG2 render at `MODE1_GBA_BG_CLIP_X`.
- **Step C-2 (OAM viewport extension)** is the hard one.  Bumping
  `MODE1_GBA_VIEWPORT_X` to `MODE1_GBA_WIDTH` exposes
  engine-parked off-screen sprites past col 240; the engine
  parking convention assumes y=0xA0 disable-bit-set but also
  scatter-positions some active entities at x≥240 that we can't
  easily distinguish from "real" widescreen sprites without
  rewriting the engine's entity culling.
- **Step C-3 (true 16:9 with MODE1_GBA_HEIGHT=180)** was attempted
  the same way — extend the PPU height + keep OAM y threshold
  pinned at 160 — but the engine's main loop fundamentally assumes
  160-line frames for timing and BG preload, so the extra 20
  lines pull in undefined buffer content.
- **TXT512x256 (attempt 1)** doesn't work for TMC: cb2 (VRAM
  blocks 16-23) is referenced by BG1 high-index tile reads via
  `hyruleTownTileSetManager` dest2, so relocating widescreen
  screen bases there destroys chardata.  No free 2-block pair
  exists in TMC's gameplay VRAM layout.

## Current state (Phase 1)

- `MODE1_GBA_WIDTH` overridable at compile time via xmake
  `--widescreen_width=N`. Default 240. Spike value 256.
- `MODE1_GBA_BG_CLIP_X` follows `MODE1_GBA_WIDTH` up to 256, then
  caps at 256.
- Post-render stretch in `port/port_ppu.cpp` only fires for >256;
  at ≤256 the BG renderer fills the wider buffer natively.
- OAM viewport stays at 240 unconditionally — bumping it leaks the
  engine's parked off-screen sprites into the visible widescreen
  area.

## Why >256 doesn't "just work"

The original PPU comment ("real widescreen needs a 64-tile sa2-style
BGCNT_TXT512x256 extension") implies the BG buffers are 32-tile wide
and need to grow. Survey of the actual code shows the situation is
more layered:

### BG buffer layout (`include/vram.h`)

```
gBG0Buffer: u16[0x400]   (32×32 = 1024 entries)
gBG1Buffer: u16[0x400]
gBG2Buffer: u16[0x400]
gBG3Buffer: u16[0x800]   (32×64 = 2048 entries — NOT 64×32)
```

**gBG3 is taller, not wider.** Confirmed by `src/enemy/waterDrop.c:76`
which addresses the bottom half as `(y & 0x1fU) * 0x20 + 0x400`. The
extra 0x400 entries are a vertical pre-load buffer used during
vertical room transitions, not a wider BG.

### Tile rendering pipeline

1. `RenderMapLayerToSubTileMap(gMapDataBottomSpecial, &gMapBottom)`
   in `src/beanstalkSubtask.c:1126` renders the full room layer into
   `gMapDataBottomSpecial` (`u16[0x4000]` = 128KB intermediate).
   Uses **0x80 row stride** = 128 u16 = 64 subtiles wide. This is the
   intermediate's layout, NOT any BG buffer.

2. The "scroll modes" in `src/scroll.c` (`Scroll0`..`Scroll5`) copy a
   32-tile window from `gMapDataTopSpecial` / `gMapDataBottomSpecial`
   into `gBGnBuffer` based on camera position. Each mode handles a
   different room transition pattern (continuous scroll, screen-by-
   screen, vertical-only, fade-in, etc.).

3. DMA pushes `gBGnBuffer` → VRAM at the configured screen-base.

### Hardware-side BG screen-size encoding

- BGCNT bits 14–15: `00` = 256×256 (1 screen block), `01` = 512×256,
  `10` = 256×512, `11` = 512×512.
- TMC sets BG0–3 to `00` at `src/common.c:647`. Title screen at
  `src/title.c:251` already overrides BG2 to `512×256` (`| BGCNT_TXT512x256`),
  proving the PPU + hardware path handles it.
- 512×256 mode requires 2 screen blocks in VRAM (left + right). The
  EWRAM-side buffer needs to mirror this: u16[0x800] with the right
  block at offset 0x400.

## The actual work for Phase 2

### Step A: BG buffer horizontal expansion

For each BG layer that should be widescreen (BG1, BG2, BG3 — leave
BG0 as 32-tile so UI offsets stay stable):

1. Grow `gBGnBuffer` from `u16[0x400]` to `u16[0x800]` in
   `include/vram.h`. For gBG3, expand from 32×64 to a layout that
   covers both horizontal (64) and vertical (64) — i.e.
   `u16[0x1000]`. (Or accept losing the vertical pre-load buffer in
   exchange for horizontal — needs to be evaluated.)
2. Update every `MemClear`/`MemCopy` `sizeof(gBGnBuffer)` / hardcoded
   `0x800` to track the new size. ~30 sites already mapped:
   - `src/debug.c:29-30`
   - `src/cutscene.c:263`
   - `src/gameUtils.c:972-975`
   - `src/common.c:628`
   - `src/menu/figurineMenu.c:121-122,577-578,648-650`
   - `src/staffroll.c:117,134,201-203`
   - `src/subtask.c:109`
   - `src/demo.c:69-70`
   - `src/ui.c:181`
   - `src/fileselect.c:398,410-411,800`
3. Update BGCNT init at `src/common.c:647-653` to OR in
   `BGCNT_TXT512x256` for each widened layer.

### Step B: Scroll-mode widening

Each scroll mode currently copies a 32-tile window. Update to copy
64 tiles, with the right half going to gBGnBuffer offset 0x400:

- `Scroll0` (most common — single-screen rooms with camera tracking)
- `Scroll1` (cont'd scroll between rooms)
- `Scroll2` (transition with fade)
- `Scroll4` (vertical scroll)
- `Scroll5` (special — boss rooms?)

Each `ScrollNSubM` variant also needs updating. The 0x80 stride in
the intermediates already covers 64 tiles, so the SOURCE side is fine
— it's the destination-write loop that needs widening.

### Step C: OAM viewport extension

`MODE1_GBA_VIEWPORT_X` in `libs/ViruaPPU/include/cpu/mode1.h` must
track `MODE1_GBA_WIDTH` so sprites in the widescreen area render.
Catch: the engine "parks" off-screen sprites by setting their OAM x
to 240+. Bumping the viewport without changing the engine's parking
position makes those parked sprites visible.

Either:
- Patch the engine's "park sprite" code to use a position past the
  widescreen viewport (e.g. `MODE1_GBA_WIDTH + 16`)
- Add a port-side OAM filter that hides sprites flagged as
  off-screen-parked
- Detect parked sprites by a sentinel attribute and clip them in the
  PPU

### Step D: UI offset audit

`src/ui.c`, `src/message.c`, `src/fileselect.c` use ~30 hardcoded
offsets like `&gBG0Buffer[0x219]`. If BG0 stays at 32-tile width,
none of these need to change. If BG0 widens too (to support widescreen
HUDs), every offset needs to be re-derived for the new stride.

**Recommendation**: keep BG0 at 32-tile, widen only BG1/BG2/BG3.

### Step E: Camera bounds

`UpdateCamera` / equivalent currently limits camera scroll so the
32-tile window stays inside the room. With a 64-tile window, the
limit changes. The room can also become too small for the wider
view — needs runtime check + fallback (centre the room with bars).

### Step F: Cutscene / dialogue blackout

Cutscenes often draw text/effects assuming a 240-wide playfield.
Either let the wider area show through (game world visible behind
cutscene text) or render black side-bars during cutscenes.

## Verification approach

Each step needs visual verification before moving to the next.
Recommend the loop:

1. Make a single isolated change.
2. Build with `xmake build -y tmc_pc`.
3. Run `./build/pc/tmc_pc` and visit a representative area:
   - Hyrule Town (BG2 + BG1 dense)
   - Minish Woods (BG3 dense forest)
   - Temple of Droplets (BG3 dynamic water effects)
   - File select (BG0 UI-heavy)
   - Title screen (already uses BG2 512×256)
4. F9 bug-report capture for any regression.
5. If clean, commit; otherwise revert and try a different approach.

## Scope estimate

- Step A: 1–2 sessions of mechanical edits + memcpy audit
- Step B: 2–3 sessions per scroll mode (5 modes + sub-variants)
- Step C: 1 session
- Step D: 0 sessions if BG0 stays 32-tile
- Step E: 1 session
- Step F: 1 session

Total: ~10–15 focused sessions of engineering + verification.

## Progress log

### ✅ Slice 1 — `564650014`: ViruaPPU per-BG render-width clipping

`port/patches/viruappu-widescreen.patch` extended to:
- `render_text_bg_line` honours BGCNT bit 14: 32-tile BGs clip at
  `MODE1_GBA_BG_CLIP_X` (240); 64-tile BGs (TXT512x256) render up
  to `MODE1_GBA_WIDTH`.
- Composite force-black past col 240 is now conditional on "no BG
  drew here" so widescreen BG content passes through.

### ✅ Slice 2 — `25e0ae997`: BG right-half buffers + this doc

`gBG0BufferRight` / `gBG1BufferRight` / `gBG2BufferRight` declared at
`gEwram[0x36000..0x37800]` (otherwise-unused EWRAM tail).  Each is a
`u16[0x400]` (32×32 tilemap) for cols 32..63 of a TXT512x256 BG.

Two-buffer layout chosen over extending gBG[012]Buffer in-place
because `gBG0Buffer` at `0x34CB0` and `gBG2Buffer` at `0x344B0` are
EWRAM-adjacent — a naive `0x400 → 0x800` bump would overlap either
adjacent buffer or `gMapBottom`.

### ✅ Slice 3 — `e3778b4e6`: `sub_08016CA8` DMA wiring

The per-VBlank DMA routine now takes a `void* right_half` argument
(PC_PORT only).  When `BGCNT size_flag == 1` (TXT512x256), it issues
a second 0x800-byte DMA from `right_half` to VRAM base+0x800.
Callers in `src/interrupts.c::UpdateDisplayControls` and
`src/gameUtils.c::RestoreGameTask` pass the appropriate
`gBG[012]BufferRight` (or NULL for BG3).

### ❌ Step B-2 attempt 2 — VRAM dead-end (reverted)

**Approach tried**: keep the engine's per-scroll-mode routines
unchanged.  After every `UpdateScrollVram`, run port-side
`Port_PopulateBG_TXT512x256` that fully repopulates both halves
from the intermediate with mod-64 alignment + OR's BGCNT bit 14
(TXT512x256) + relocates the screen base to a "free" pair of
blocks so the right-half DMA (`base + 0x800`) doesn't collide with
another BG's screen base.

**Why it failed**: in Hyrule Town,
`src/manager/hyruleTownTileSetManager.c` loads tileset chardata
to `BG_SCREEN_ADDR(16/18/20)` (blocks 16-21, in the cb2 region),
each load 0x1000 bytes (= 2 blocks).  These chardata blocks ARE
referenced by BG1: BG1 cb1 = blocks 8-15, but BG1 tile indices in
the room can reach `0x200+` whose pixel data sits at `cb1_base +
0x200 * 32 = 0x4000 + 0x4000 = 0x8000` = block 16 = cb2 region.
So even though no BG sets its BGCNT.charbase to 2, BG1 indirectly
USES cb2 for high-index tile chardata.

Relocating BG1+BG2 screen bases into blocks 16-19 (the only
"free" pair we could find) writes screen tilemap entries on top
of this chardata.  Result: BG1 then renders my screen tilemap
entries as if they were tile pixel data → whole-screen corruption.
Bug report `bugreport_20260525_212401` is the canonical capture.

**Searched but no truly-free 2-block pair exists in TMC's gameplay
VRAM layout**:
- Blocks 0-7: cb0 (BG2/BG3) — actively loaded with tilesets.
- Blocks 8-15: cb1 (BG1) — actively loaded.
- Blocks 16-23: cb2 — loaded by HTTSM dest2 + indirectly used by
  BG1 tile indices ≥ 0x200.
- Blocks 24-27: cb3 (BG0) — BG0 chardata (tiles < 0x100 only,
  so blocks 28-31 are screen bases inside the same cb region).
- Blocks 28-31: BG0/1/2/3 screen tilemaps.

The standard 4-BG layout fills 64 KB of BG VRAM tightly; there's
no slack to insert a second screen block for a widened BG.

**Reverted commit**: `0f0c68556` (rolled back in [this commit]).
What we kept:
- `xmake.lua` `widescreen_width` option still wired to emit
  `-DMODE1_GBA_WIDTH=N` for the tmc_pc target — useful for future
  attempts.
- `port_ppu.cpp` Phase-1 uniform-stretch fallback is back in
  place — so a build at `--widescreen_width=320` again shows a
  stretched 240-px frame (no visual regression vs main).

### ✅ Step B-2 — Option A landed (port-side shadow buffer)

Implementation:

1. `libs/ViruaPPU/include/cpu/mode1.h`: declare
   `virtuappu_mode1_ws_shadow[MODE1_GBA_BG_COUNT]` (per-BG u16 tile-
   entry table) + `virtuappu_mode1_ws_shadow_base_tile[]`
   (per-BG "where in PPU tile_col space does shadow[i=0] live?").
   Define `MODE1_WS_SHADOW_COLS = 12` (covers 320 wide + scroll
   headroom) and `MODE1_WS_SHADOW_ROWS = 32` (mirrors mod-32 row
   rolling).

2. `libs/ViruaPPU/src/mode1.c::render_text_bg_line`: when a 32-tile
   BG has a non-NULL shadow registered, render up to
   `MODE1_GBA_WIDTH` instead of clipping at `MODE1_GBA_BG_CLIP_X`.
   For `x >= MODE1_GBA_BG_CLIP_X` it reads `tile_entry.raw` from
   `shadow[local_row * 12 + shadow_idx]` instead of VRAM.
   `shadow_idx = (tile_col - shadow_base + 32) % 32` handles
   BGHOFS-wrap so sub-tile scroll past a mod-32 boundary still
   indexes the right cell.  Chardata lookup + palette + flip are
   unchanged.

3. `port/port_linked_stubs.c::Port_Widescreen_UpdateShadows`:
   per-frame populate (called from `UpdateDisplayControls`).  Reads
   23 BG-tile rows × 12 cols from `gMapData{Bottom,Top}Special` at
   world cols `[cam_bg_col + 30, cam_bg_col + 30 + 12)`, writes to
   `sWsShadowBG[12]`.  Sets the PPU's pointer + base_tile globals.
   `#if MODE1_GBA_WIDTH > 240` gated, no-op at native 240.

4. `port/port_ppu.cpp`: removed the Phase-1 uniform-stretch fallback
   (was clobbering real Phase-2 BG content with stretched 240-px).

VRAM is untouched — the shadow lives entirely in port-side EWRAM.
Cost: 1.5 KB of statics + ~700 u16 writes per frame.  Trivial.

Known follow-ups:
- File-select / cutscenes / menus repurpose `gMapData{T,B}Special`
  for HUD content; my shadow reads that as if it were world tiles
  and shows HUD-tile bleed past col 240.  Step F should detect
  these modes and either disable shadow or render black bars.
- Rooms narrower than (visible cols + widescreen reveal) show
  tile-0 → composite force-black past room width.  Step E should
  bound camera scroll so the reveal area stays inside the room,
  or render side bars when it can't.
- OAM still clips at `MODE1_GBA_VIEWPORT_X = 240` (step C-2).
  Sprites stop at the native viewport edge.

### Reference: alternative approaches not taken

**Option B (VRAM reorganization)** — move BG0's chardata to a
tighter region to free blocks 28-31 for widened-BG screen bases.
Many touch-points (every `gScreen.bg0.control` writer), high risk
of breaking non-gameplay modes.  Skipped in favour of Option A.

**Option C (Phase-1 stretch)** — uniform 240→320 stretch.  Was the
fallback before Option A landed.  No longer applicable.

### 🔲 Step C-2 — OAM viewport extension

Currently OAM clips at `MODE1_GBA_VIEWPORT_X = 240` (parked off-screen
sprites live there).  For widescreen, either bump the OAM clip with
the BG width OR move the engine's "park" position past the new
viewport.

### 🔲 Step C-3 — mode-transition robustness

`Port_PopulateBG_TXT512x256` only re-asserts BGCNT bit 14 when
`UpdateScrollVram` fires.  Engine code that overwrites
`gScreen.bg[12].control` on mode transitions (cutscenes, menus,
file-select — see the bg-control grep in the commit message)
clears the bit until the next gUpdateVisibleTiles.  In the gap
window the right half stays in VRAM with its last content but
isn't read by the PPU (32x32 mode), so the user sees the BG
shrink back to 240-wide momentarily.  Fix: re-assert the bit in
`UpdateDisplayControls` instead of (or in addition to)
`Port_PopulateBG_TXT512x256` so it survives every frame regardless
of scroll-update timing.

### 🔲 Steps D / E / F — UI offsets, camera bounds, cutscene bars

Per the original design above.

### 2026-05-30 re-attempt (reverted again) — new finding: room-transition scroll distance

Re-did Option A from scratch — direct `gMapData{Bottom,Top}Special` reads in
`render_text_bg_line` for `x >= MODE1_GBA_BG_CLIP_X` (no separate shadow buffer),
camera centering + right-clamp keyed to the view width (`src/scroll.c`), sprite
visibility widened (`CheckOnScreen` / `CheckRectOnScreen` / `RenderSpritePieces`),
non-gameplay **stretch** in `port_ppu.cpp`, OAM viewport = `MODE1_GBA_WIDTH`.
Verified at 384: gameplay BG fills the full width, seamless at the col-240 split,
camera centered. All reverted to the documented baseline (option present, unwired).

Re-confirmed the **Step C-2 dead-end**: OAM/entity flicker is the blocker. Widescreen
reveals cols 240..W where the engine parks/animates sprites it treats as off-screen,
and `CheckOnScreen`-gated AI runs there too → occasional 1-frame sprite flash near the
edges. No clean global fix; it is per-entity (several enemies/objects use hardcoded
`scroll_x + 0x108`-style bounds, e.g. gyorgChild/gyorgMale/bombPeahat).

**NEW finding (not previously documented): overworld room transitions hardcode a 240px
scroll distance.** `src/scroll.c::Scroll2Sub2` scrolls horizontal room crossings until
`unk_18 == 0x3c` (60 steps × 4px = 240 = native width). With a wider camera the
destination camera position is one *view* width away, so the transition under-scrolls by
`W-240` and leaves a **permanent black band** on the leading edge after entry
(reproduced walking into Lon Lon Ranch, area 0x03 room 0x05). Working fix: gate the
horizontal cases on `viewwidth/4` (= 96 at 384) instead of `0x3c`; vertical stays `0x28`
(height unchanged). Caveat: the leading edge still goes black *during* the ~1.5s scroll,
because the wide view spans the room being left while `gMapData*Special` is already the
destination room (clips to backdrop). Seamless transitions need both rooms' maps live
during the cross-fade.

Also: non-gameplay screens (title via the GBA-mode-1 → ViruaPPU-mode-2 path, menus, boot
logos) want a 240→W **stretch** (fills the screen, no bars). Pillarbox bars were
explicitly rejected during review.


### 2026-06-01 re-land — BG reveal working again, correct shadow indexing

Re-landed Option A (port-side shadow) on top of the new DISPLAY_WIDTH-parametric
culling foundation (`port/port_widescreen.h`, parametric `CheckOnScreen` /
`CheckRectOnScreen`). Behind the default-off `--widescreen_width` flag.

**Root-caused the prior shadow misalignment.** The 97efcdd18 template indexed the
shadow as if `BGHOFS` carried the full camera scroll. It does NOT: the engine
(`src/scroll.c::UpdateScreenShake`) sets `BGHOFS = (scroll_x-origin_x) & 0xF` and
`BGVOFS = ((scroll_y-origin_y) & 0xF) + 8` — i.e. ONLY the sub-16px offset. Coarse
scroll is done by re-filling the 32-tile buffer aligned to 16px blocks
(`ram_sub_080B197C_c`: `col16 = xdiff>>4`, sub-tile stride **128 u16**, buffer cell
`(br,bc)` holds world sub-tile `(2*row16-1+br, 2*col16+bc)`).

Correct shadow population (`port/port_linked_stubs.c::Port_WidescreenShadow_Populate`):
- `base_col = 2*(xdiff>>4) + MODE1_GBA_BG_CLIP_X/8`  (world sub-col at display 240)
- `base_row = 2*(ydiff>>4) - 1`  (world sub-row of buffer/shadow row 0)
- `virtuappu_mode1_ws_shadow_base_tile = MODE1_GBA_BG_CLIP_X/8` (**constant 30** —
  BGHOFS excludes coarse scroll, so the PPU's `(tile_col - base + 32) % 32` lands
  shadow_idx 0 at display col 240 regardless of camera position)
- stride 128 u16; fill `shadow[br]` (br = PPU `tile_row`) with the SAME world row
  the VRAM buffer row br holds, so the reveal is continuous with VRAM at the seam.
- `MODE1_WS_SHADOW_COLS` now scales with width: `((MODE1_GBA_WIDTH-240)/8)+4`.

**Verified at 384 (Festival Town, headless `TMC_REPRO_LITAREA` capture):**
- Native columns [0,240) are **byte-identical** between the 240 and 384 builds
  (0/38400 px differ) — the change is provably additive, zero native regression.
- Reveal columns [240,384) are 72% real world tiles (28% force-black: room edge /
  transparent), seamless at the col-240 seam.
- `port/port_bugreport.cpp` screenshot capture made width-aware (`kFrameW =
  MODE1_GBA_WIDTH`) — it hard-coded 240 and sheared widescreen captures.
- `port_ppu.cpp` stretch now gated to non-GAME tasks only (`gMain[2] != 2`);
  gameplay presents the real wide composite.
- OAM clip `MODE1_GBA_VIEWPORT_X` now tracks `MODE1_GBA_WIDTH`.

**Still outstanding for a finished feature (unchanged from prior analysis):**
- Camera centering + right-clamp (reveal is currently right-side only; player not
  centered). `src/scroll.c`.
- `RenderSpritePieces` (`port/port_draw.c`) still clips sprite pieces at x>=240 and
  masks OAM X to 9 bits — sprites don't yet render in the reveal. Needs the
  EXTENDED_OAM-style wide-X path + the per-entity parking pass (flicker).
- Room-transition scroll distance (`Scroll2Sub2` → viewwidth/4) + the transient
  leading-edge black during cross-fades.

### 2026-06-01 (cont.) — camera centering, sprite reveal, and the chardata wall

Landed (all gated `#if MODE1_GBA_WIDTH > 240`, native 240 re-verified byte-identical):
- **Camera centering** (`src/scroll.c::sub_080809D4`, `sub_08080974`): the hardcoded
  `120` (240/2) player-centering offset + room clamp are now parametric on
  `MODE1_GBA_WIDTH`. Clamp rewritten as `scroll_x = clamp(target - W/2, origin,
  max(origin, origin + width - W))` — reduces EXACTLY to the GBA-original at 240
  (kept verbatim in `#else`), centers the player in wide rooms, and pins narrow
  rooms (width < W) to the left.
- **Sprite reveal** (`port/port_draw.c::RenderSpritePieces`): clip widened from
  `x >= 240` to `x >= PORT_VIEW_WIDTH`. No OAM-width change needed after all —
  9-bit OAM X covers 0..511 and the PPU only sign-flips at `obj_x >= MODE1_GBA_WIDTH`
  (`mode1.c:462`), so reveal sprites at x in [240, W) draw correctly and genuine
  off-left sprites (X 384..511) still wrap. EXTENDED_OAM is unnecessary for W<=512.

**Root-caused the reveal garbage (the real remaining blocker): CHARDATA, not indexing.**
Instrumented `shadow[br][c]` vs the engine's own `gBG1Buffer` (VRAM) at the overlap
columns: they are **byte-identical** (`shadow[br][0]==gBG1[br*32+30]`, `[1]==[31]`
for every row) — the shadow tilemap indexing is provably correct. The garbage
(uniform magenta `0x7C1F` tiles) is from reveal tiles whose **tile chardata is not
loaded in VRAM**: the engine only DMAs chardata for the tile set used in the visible
240-wide area. Areas whose tileset fully fits VRAM (Hyrule Town, Festival Town)
reveal correctly; areas with partial/streamed chardata (Hyrule Field) show stale
VRAM for reveal-only tile variants.

→ Finishing universal BG widescreen requires **chardata streaming for the wider
viewport** (load the reveal region's tiles, not just the visible set) — a
substantial engine change. Until then, BG reveal is correct only where the area's
full tileset is resident. Camera-centering + sprite-reveal are in place and waiting
on it. Per-entity parking pass (flicker) and `Scroll2Sub2` (transition) still open.

### 2026-06-01 (cont.) — the chardata blocker is per-area tileset streaming

Confirmed *why* the chardata isn't resident: TMC streams BG tile graphics by camera
region via per-area **tileset managers**, because GBA BG char VRAM can't hold a whole
area's tileset at once:
- `src/manager/hyruleTownTileSetManager.c` — `*_UpdateLoadGfxGroups` swaps gfx groups
  by the camera's town quadrant.
- `src/manager/minishVillageTileSetManager.c` — explicit `{ROM src, 0x0600x000 VRAM
  char slot}` tables, reloaded as you cross region boundaries.

Implications for universal BG widescreen:
- Areas with a **fixed, fully-resident** tileset already reveal correctly (the BG
  mechanism + centering + sprites are done and correct for them).
- Areas with a **streaming** tileset (Hyrule Town/Field, Minish Village, …) show stale
  VRAM for reveal-only tile variants until their manager loads that region.
- Making the reveal correct there means widening each manager's resident region by the
  reveal extent — area-by-area, and bounded by the **GBA VRAM char budget** (the very
  reason streaming exists; naively loading more overflows the char slots). On the PC
  port, expanding `gVram` alone does NOT help: the engine DMAs to fixed GBA char
  addresses and decides what to stream based on its own budget, not the buffer size.

So "universal true widescreen" needs a deliberate VRAM-char-budget + streaming rework
(per area, or a port-wide expansion of the engine's char-slot model) — a distinct major
effort, not a continuation of the rendering pipeline. The pipeline (culling, BG read,
camera, sprites) is complete and verified; it is gated and native-safe at 240.

### 2026-06-01 (cont.) — the chardata "working set" + a shippable 16:9 everywhere

Built the foundation for reveal re-indexing (`port/port_widechar.cpp`: per-gfx-group
BG-char cache via a `Port_LoadGfxGroupFromAssets` hook; region->group rect harvest via
a `CheckRegionsOnScreen` hook). All gated `MODE1_GBA_WIDTH > 240`; native 240 re-verified
byte-identical.

Using it, instrumented the reveal in Hyrule Field (a NON-region-streamed area —
`CheckRegionsOnScreen` is never called there; it streams chardata incrementally as the
camera scrolls). Findings, with data:
- Magenta is NOT at the col-240 seam and is NOT uniform — it **increases with reveal
  distance** (per-column: x240-288 mostly clean, peak magenta at x320-352). Classic
  chardata **working-set** signature: tiles near the visible edge are resident; far ones
  aren't. The indexing is correct.
- The garbage is **stale VRAM**: reveal tiles reference char indices the current area's
  loaded tileset didn't write, so they read leftover graphics from a prior room/area
  (a magenta `0x7C1F` palette entry), not black.
- Consequence (measured): at width 288 (=160*16/9, standard 16:9) the reveal is mostly
  real content; **Festival Town = 0% magenta (fully clean)**, **Hyrule Field ≈ 5-6%**
  magenta confined to the far-right ~8px (the working-set boundary). Narrowing to 272
  does not fix Hyrule Field's edge (6%) — it's inherent to that area's tile density.

Net, honest position:
- **Standard 16:9 (≈288) "true widescreen everywhere" is largely achieved**: real world
  content (not a stretch) in every area, fully clean in most, with a thin stale-VRAM edge
  artifact in the most tile-dense streamed areas (Hyrule Field). Recommended supported
  width: `--widescreen_width=288`.
- **Cinematic ultra-wide (e.g. 384) everywhere is NOT achievable** without the
  re-indexing subsystem (blocked by the GBA char budget + scroll-dependent index meaning:
  the same tile index denotes different graphics at different scroll positions, so the
  reveal cannot recover "future" tiles without per-area streaming replication).
- Clean-edge options for 288 in dense areas (future, each needs care): clear the
  area-tileset BG-char region on area load so unloaded reveal indices read transparent
  (force-blacked) instead of stale magenta — must exclude the persistent HUD char region;
  or complete the residency tracker (hook ALL VRAM char writes, not just LoadGfxGroup)
  and force-black non-resident reveal tiles in render_text_bg_line.

### 2026-06-02 — residency gate for the broken edge (partial)

Reported: broken tiles on the reveal edge (stale-VRAM garbage), worst in tile-dense
streamed areas. Added a port-side **residency gate** in the shadow populate
(`port/port_linked_stubs.c::Port_WidescreenShadow_Populate`): a reveal tile is kept
only if its index appears in the engine's on-screen buffer (`gBG1Buffer`/`gBG2Buffer`)
— those tiles' chardata is provably resident; everything else is forced transparent so
the composite force-blacks it (clean black instead of garbage). Gated `>240`, native
240 byte-identical.

Result (Hyrule Field, 288): garbage largely replaced by real content + clean black
(content 46%, black 48%), but a **~5% scattered residual** survives. Diagnosed:
- Not OAM (magenta unchanged with all OAM disabled) — it's BG.
- The gate is too permissive: `gBG1Buffer` is the full 32x32 buffer including the
  off-screen MARGIN entries, whose chardata the engine may not have loaded. Those
  margin indices get marked "resident", so a few stale tiles slip through.

To fully clean it, the resident oracle must be the **actually-rendered visible window**
(cols 0-240), not the whole buffer — or move residency tracking into the PPU (per-tile
chardata-loaded test). Alternatively, limit the reveal to the engine's reliable 256-px
BG buffer (≈16-px reveal, guaranteed clean, no shadow) for a clean-but-minimal widescreen.
The width/quality tradeoff (wide+imperfect vs narrow+clean) is now the open decision.