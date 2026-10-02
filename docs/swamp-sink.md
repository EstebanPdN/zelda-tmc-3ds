# Castor Wilds swamp-sink (per-pixel waterline clip)

**Status: shipped.** Depth/speed user-approved. One known issue open (spin-attack — see below).

Goal: on the Castor Wilds swamp, Link sinks GBA-accurately — upper body visible,
lower body hidden by a waterline that rises feet→head, slowly.

## Why the obvious approaches don't work

- **No "head overlay" sprite.** `Object70` (swamp = type 0 / frame 0xb, created at
  `SurfaceAction_Swamp`, `src/player.c`) has def `gfx=133, spriteIndex=167`; sprite 167
  frame 0xb is 12 pieces that all resolve to blank OBJ tile 133. It draws nothing — on
  GBA too. Its only effect is to set Link's body priority. (See memory
  `project_swamp_head_overlay_myth`.)
- **`flipY=3` (the GBA value) makes Link fully invisible on PC.** It sets OBJ priority 3;
  Castor Wilds BG2 (terrain incl. mud) is priority 2, so Link draws *behind* the opaque
  terrain and vanishes entirely — both on the swamp and on Link's-house stairs. The user
  confirmed (2026-06-24) that GBA does **not** fully hide Link here, so a blind `flipY=3`
  revert regresses. The port keeps `flipY=2` and hides the lower body explicitly instead.
- **Per-OAM-piece clip is too coarse** — Link is only ~2 piece-rows tall, so dropping
  whole pieces snaps the waterline. We clip per pixel-row instead.

## Mechanism (what ships)

A per-pixel vertical OBJ clip driven entirely by live engine state (no separate sink
timer, so no double-drown desync):

- **ViruaPPU** (`libs/ViruaPPU/src/mode1.c`, applied at build time via
  `port/patches/viruappu-obj-clip-y.patch`; do **not** commit the dirty submodule —
  see memory `project_viruappu_patches`): shared globals
  `virtuappu_mode1_obj_clip_mark[128]` / `_obj_clip_y` / `_obj_clip_enable`, and a guard in
  `virtuappu_mode1_render_obj_line` just before pixel-commit:
  `if (enable && mark[i] && line >= clip_y) continue;` (`i` = OAM index, `line` = scanline).
- **Port** (`port/port_draw.c`):
  - `DrawEntitySprites` sets `sRenderingPlayer` for **all** render paths (Link is the
    multi-part `renderMode==1` path; the old code only set it for `renderMode==0`, so the
    clip never ran for him).
  - `RenderSpritePieces` resets the mark once per frame (`updated==0`), then when
    `sRenderingPlayer && floor_type==SURFACE_SWAMP && jump_status==0 && z<=0`, marks every
    player OAM entry and sets the waterline `clip_y = baseY - sub`, `enable=1`.
  - **Tuning knob** (approved values): `s32 sub = 2 + surfaceTimer/48; if (sub>8) sub=8;`
    — `8` = max depth (bigger = deeper; `sub≈14` was head-only, `≈8` ≈ waist),
    `/48` = speed (bigger = slower).

## Known issue — spin-attack regression (OPEN)

When Link spin-attacks, he "first flies upwards." Affine OBJs render through a different
ViruaPPU path that this clip does not touch, so the most likely cause is the clip
engaging/disengaging across the non-affine spin wind-up/recovery frames (lower body pops
back into view), or the clip catching a frame it shouldn't.

Not yet pinned down — needs an on-display observation (headless ROOMCAP gives unreliable
player/BG state for this; the **Sword Techniques** debug-menu toggles in
`port/port_debug_actions.c` were added to grant Spin Attack and reproduce it interactively).

Likely fix once confirmed: gate `sSwampClipActive` to an idle/sinking state — skip while an
attack action is active, or skip affine frames (`flags & 0x300`). Confirm the exact
misbehavior (swamp-only vs dry-land; body-pop vs sprite-rotate) before choosing the guard.

## Repro

- On-display: warp to the swamp, F6 to load `state_quick.bin` (a quicksave at the sink
  spot), spin-attack to observe. F5/F6 = quicksave/quickload (disabled in Console-Parity).
- Headless visual: `TMC_ROOMCAP=1 TMC_ROOMCAP_WARP="0x04,0x00,0x70,0x70,0"
  TMC_ROOMCAP_OUT=x.png` (crop box (78,42,150,104) ×8). `TMC_ROOMCAP_SAVE=1` writes the
  quicksave instead of a PNG.
