# Dynamic Widescreen — Implementation Status (2026-05-30)

Status: **parked / reverted out of the tree.** Gameplay BG widescreen worked;
two gameplay artifacts remained (sprite edge flicker, transient transition
black). The WIP was **backed out** so the default build is clean (the macOS
SDL_GPU work was left intact). This doc captures the approach, decisions, and
exact changes so the work can be re-applied. See "Re-applying" at the bottom.

## Goal

Render TMC at a wider-than-GBA internal viewport (e.g. 384x160) — true dynamic
widescreen with real extended background, **not** a stretch of the 240x160 frame.
Height stays 160. BG0/HUD mostly stays in the original 240 safe area; the later
2026-06-04 WIP toggle moves right-side HUD tiles/buttons to the wide right edge
during normal gameplay.

## Build & run

```
# Widescreen (example 384 wide), GPU backend:
xmake f -y --game_version=USA --gpu_renderer=y --widescreen_width=384
xmake build -y tmc_pc

# Native (default): widescreen code paths compile out / no-op at 240.
xmake f -y --game_version=USA --gpu_renderer=y --widescreen_width=240
```

`--widescreen_width=N` injects `-DMODE1_GBA_WIDTH=N`. Everything keys off that:
`MODE1_GBA_WIDTH == 240` ⇒ stock behavior; `> 240` ⇒ widescreen paths active.

Headless smoke/visual capture used the env-gated harness in
`port/port_repro_mazaal.c` (TMC_AUTOPLAY=1, TMC_REPRO_MAZAAL=1, plus temporary
TMC_WARP / TMC_FRAME_DUMP / TMC_WS_WALK hooks). Those temporary hooks were
reverted; the file is back to its original repro-only content. Run headless with
`SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy`.

## What's implemented

- **Compile-time viewport** — `port/port_widescreen.h` (new): `PORT_WIDESCREEN_VIEW_WIDTH`
  = `MODE1_GBA_WIDTH`, native 240/160 constants, half-extents. `xmake.lua` adds the
  `widescreen_width` option and the `MODE1_GBA_WIDTH=` define.
- **Camera** — `src/scroll.c`: `GetMaxScrollXForViewport` / `GetCenteredScrollXForViewport`
  decouple the follow-camera centering and right-clamp from the hardcoded 0x78/0xf0
  (used in `Scroll1`, `sub_08080974`, `sub_080809D4`). Link is centered in the wide view.
- **Overworld room transition distance** — `src/scroll.c` `Scroll2Sub2`: horizontal
  transitions now scroll `ROOM_VIEWPORT_WIDTH / 4` steps instead of `0x3c` (240px).
  This is what fixes the Lon Lon Ranch / room-entry **permanent black band**.
- **BG extension** — `libs/ViruaPPU` (applied via `port/patches/viruappu-widescreen.patch`):
  `virtuappu_mode1_set_widescreen_bg` / `clear_widescreen_bgs`; `render_text_bg_line`
  renders columns ≥240 from a room-backed `map_special` source (128-stride subtile map);
  OAM viewport widened (`MODE1_GBA_VIEWPORT_X = MODE1_GBA_WIDTH`). `MODE1_GBA_BG_CLIP_X`
  stays 240 (split point between VRAM-buffer native cols and map_special extension).
- **Per-frame bind + non-gameplay handling** — `port/port_ppu.cpp`:
  `Port_PPU_UpdateWidescreenBgs` binds `gMapBottom`/`gMapTop` room maps each frame
  (reads `gScreen`/`gRoomControls`/`gMap*` via local mirror structs — these C structs
  can't be included into this C++ TU). Non-gameplay frames (title/menus, affine-only
  screens) are **stretched to fill** (`Port_PPU_StretchNativeToWide`) — no pillarbox,
  no stale columns.
- **Sprite visibility** — widened to the view width: `port/port_draw.c` `CheckOnScreen`
  + `RenderSpritePieces` clip; `port/port_linked_stubs.c` `CheckRectOnScreen`.
- **Docs** — `README.md` native-widescreen build note.

## Verified working (headless capture at 384)

- Gameplay BG fills the full 384 with real distinct room tiles; **not** a stretch
  (numerically confirmed); seamless at col 240 (no vertical/horizontal step; BG
  animation in sync; clean during horizontal scroll).
- Camera centers Link; narrow rooms clip correctly at the room edge.
- Title (boot logos + the affine "Legend of Zelda" title) and menus fill the screen
  via stretch — no black bars, no garbage.
- Overworld room transitions (e.g. into Lon Lon Ranch, area 0x03 room 0x05) now
  complete correctly and the destination room fills the screen.
- Builds: GPU + software, native (240) + widescreen (384) all compile and boot;
  6000-frame headless run with no render-path crash.

## Known issues / remaining work

1. **Sprite edge flicker while walking** (the "walking causes flickering" report).
   Root cause: true widescreen reveals cols 240–384, which the GBA treats as the
   *off-screen margin* and where the engine briefly parks/animates sprites it assumes
   are invisible → occasional 1-frame sprite flash near the edges. No clean global
   fix; the visibility math is per-entity. Options weighed:
   - A. Leave culling wide (current): full-width sprites, occasional 1-frame edge flash.
   - B. Cull at native distance: far fewer flashes, but sprites pop ~80px before the edge.
   - C. Per-entity bounds pass: correct fix, large (touches many enemy/object files;
     several use hardcoded `scroll_x + 0x108`-style checks — e.g. gyorgChild, gyorgMale,
     bombPeahat — plus whatever overworld objects/dig-spots use).
   **Decision pending.**

2. **Transient black during room transitions.** During the ~1.5s transition scroll the
   leading edge is briefly black: the wide view overlaps the room being left, whose
   `map_special` is already swapped to the destination room (so the overlap clips to
   backdrop). Self-corrects the instant the transition ends. A seamless fix needs both
   rooms' map data available during the cross-fade, or buffer-sourced extension columns
   during transitions.

3. **HUD** right-side gameplay widgets were later anchored to the wide right edge
   (2026-06-04); dialogue/hidden-HUD states still use the native 240 safe area.

4. **Affine (VPU mode 2) gameplay rooms** (rolling-barrel, etc.) are treated as
   non-gameplay and stretched — the BG extension hook is mode1-only.

5. `.glslp` shader presets unchanged (still SPIR-V/Vulkan only).

## File inventory (all reverted; this is what to recreate)
These were the widescreen edits, now backed out of the working tree:
- `port/port_widescreen.h` (new) — viewport constants.
- `xmake.lua` — `widescreen_width` wired to `-DMODE1_GBA_WIDTH`.
- `src/scroll.c` — camera centering/clamp + horizontal transition distance.
- `port/port_ppu.cpp` — per-frame BG-source bind, mirror structs, non-gameplay stretch.
- `port/port_draw.c` — `CheckOnScreen` + `RenderSpritePieces` clip widths.
- `port/port_linked_stubs.c` — `CheckRectOnScreen` width.
- `port/patches/viruappu-widescreen.patch` — added the BG-source hooks + OAM viewport
  on top of the existing parallel-render patch (regenerated; clean-applied on a fresh
  submodule worktree).
- `libs/ViruaPPU` (`src/mode1.c`, `include/cpu/mode1.h`) — produced by that patch.
- `README.md` — a "Native widescreen builds" section.

Only this doc remains. The macOS SDL_GPU/Metal effort was untouched
(`port/port_gpu_renderer.{cpp,h}`, `port/port_gpu_msl_shaders.inl`,
`port/port_imgui_menu.cpp`, `port/port_main.c`, `port/port_glslp_runtime.cpp`,
plus the GPU/Metal notes in `README.md`).

## Re-applying

The changes were reverted with `git checkout` (sources + patch), `rm`
(`port_widescreen.h`), a submodule `reset --hard`, and surgical edits to
`xmake.lua`/`README.md`. To resume, recreate the edits from the sections above
(they are described by symbol, not line number, so they survive drift), starting
with `port/port_widescreen.h` + the `xmake.lua` define wiring, then the
ViruaPPU BG hook, then `port_ppu.cpp`, camera, transition, and sprite clips.

## Technical notes for resuming

- Split model per scanline: native cols `0..MODE1_GBA_BG_CLIP_X-1` (240) come from the
  engine's 32-tile VRAM BG buffer + HOFS as stock; cols `≥240` come from `map_special`
  with `map_x = relative_scroll_x + x + wide_delta_x`, where `relative_scroll_x =
  scroll_x - origin_x` and `wide_delta_x = signed_delta(BGxHOFS, relative_scroll_x & 0xF)`
  (the shake/sub-tile correction). The `+8` Y bias of the engine BG buffer cancels in the
  Y delta; verified seam-aligned.
- `map_special` (`gMapDataBottomSpecial`/`gMapDataTopSpecial`) is the full-room 128-wide
  subtile map; it is kept current on tile changes (RenderMapLayerToSubTileMap), so
  rendering BG from it reflects door/tile edits and graphics-swap animations.
- Transition distance: `Scroll2Sub2` horizontal cases use `ROOM_VIEWPORT_WIDTH / 4`
  (= 60 at 240, 96 at 384). Vertical stays `0x28` (height unchanged). The player drift
  nudge (0.25/step) is left as-is (ends the player ~9px further into the new room at 384;
  acceptable, revisit if it matters).
