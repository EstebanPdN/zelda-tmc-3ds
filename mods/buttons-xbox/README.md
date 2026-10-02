# buttons-xbox

Replaces TMC's button-prompt tiles with Xbox-style icons. **No palette
override** — every tile is quantised against TMC's existing palettes,
so dialog text and unrelated HUD sprites remain visually unchanged.

Two prompt sets are modded:

| Context | Sprite | Tiles | What the user sees |
|---|:---:|---|---|
| **In-game dialogs** (HUD prompts via `ButtonUIElement`) | 505 | `gfx_215e0` tiles 0..13 | Bare Xbox A / B letterforms + Xbox RB rectangle |
| **Pause menu** (L/R tab indicators via `sub_080A5128`) | 507 | `gfx_1d7e0` tiles 16..47 | Xbox LB (left tab) and RB (right tab) at 32x32 each |

Source PNGs:

- `face buttons white.png` for A and B (bare letterforms, no circle).
- `shoulder buttons grey.png` for RB (in-game) and LB/RB (pause menu).

## How it works

### In-game prompts (sprite 505, gfx_215e0)

`ButtonUIElement` in `src/ui.c` draws each in-game prompt via
`DrawDirect(spriteIndex=505, frameIndex=…)`. The OAM piece data is:

| UI element            | sprite | frame | OAM piece                       |
|-----------------------|:------:|:-----:|---------------------------------|
| `UI_ELEMENT_BUTTON_A` | 505    | 0     | 16x16 piece, tile=0x0, palette 1 |
| `UI_ELEMENT_BUTTON_B` | 505    | 1     | 16x16 piece, tile=0x4, palette 1 |
| `UI_ELEMENT_BUTTON_R` | 505    | 2     | 8x16 piece tile=0x8 + 16x16 piece tile=0xa, palette 1 |

`port_draw.c` adds a per-element `extra` of `0x0100` to every OAM tile
index, so `tile=0x0` means **OBJ tile 256**. OBJ tile 256 is `0x06012000`,
where `gfx group 16` loads `gfx_215e0_32x32_4bpp_uncompressed.bin` (448 B
= 14 tiles).

Tile layout in that 14-tile blob:

```
tiles  0..3  = A letterform   (16x16 in 2x2 1D mapping)
tiles  4..7  = B letterform
tiles  8..9  = R "R" letter column   (8x16, 2 tiles vertical)
tiles 10..13 = R shoulder shape       (16x16)
```

### Pause menu prompts (sprite 507, gfx_1d7e0)

`sub_080A5128` in `src/menu/pauseMenu.c` draws three sprite 507 frames
on every pause-menu screen:

| Frame | Position | OAM piece | Purpose |
|:-----:|----------|-----------|---------|
| 0 | top center | 2× 64x32 (tile 0x40 + 0x60) | Screen-title banner ("Items", "Map", etc.) |
| 1 | left middle | 32x32 at tile 0x20 | **L tab indicator** |
| 2 | right middle | 32x32 at tile 0x10 | **R tab indicator** |

`cmd._8 = 0x400` (priority=1, palette=0, tile_base=0), so the piece
tile fields land directly on OBJ tiles 0x10..0x60. These tiles are
loaded by `gfx group 23` (and others) from `gfx_1d7e0_128x128_4bpp_uncompressed.bin`
(8192 B, 256 tiles).

This mod replaces only tiles **16..47** of that 256-tile blob:

```
tiles 16..31  = L indicator ← xbox LB shoulder (32x32 from shoulder buttons grey.png top-left)
tiles 32..47  = R indicator ← xbox RB shoulder (32x32 from shoulder buttons grey.png bottom-left)
tiles 64..127 = banner — UNTOUCHED (preserves the original screen-title banners)
```

## How to load

Drop this folder at `<exe>/mods/buttons-xbox/`. The mod loader auto-
discovers any subdirectory of `mods/` (or set `TMC_MODS=buttons-xbox`
explicitly).

Stderr log when active:

```
[MOD] active: buttons-xbox -> /…/mods/buttons-xbox
[MOD] 1 mod registered
[MOD] override gfx/gfx_215e0_32x32_4bpp_uncompressed.bin ← …   (in-game prompts)
[MOD] override gfx/gfx_1d7e0_128x128_4bpp_uncompressed.bin ← … (pause menu prompts)
```

The `gfx_215e0` override fires at the title-screen demo loop. The
`gfx_1d7e0` override fires when the player opens the pause menu (Start
button) — gfx group 23 loads it then.

## Caveats

1. **Colours are TMC's palette, not Xbox green/red.** The in-game
   prompts use OBJ palette 1 (shared with dialog text), and the pause-
   menu indicators use OBJ palette 0 (loaded from a different palette
   group). No palette overrides are shipped — getting accurate Xbox
   colours would require per-context palette overrides that the asset
   loader doesn't currently support without recolouring dialog text.
2. **Other UI tiles preserved.** Only the 448 B of `gfx_215e0` and the
   tiles 16..47 of `gfx_1d7e0` change — the banner and 200+ other UI
   pieces in `gfx_1d7e0` stay bit-identical.
3. **Pause menu screens 9, 10, 11.** `sub_080A5128` returns early
   without drawing for these (file-select sub-screens); the L/R
   override won't show there.

## Rebuild procedure

```sh
# Pull TMC's original palette for quantisation
cp build/USA/assets/palettes/gPalette_1.pal /tmp/

# === In-game prompts (gfx_215e0) ===
convert "tmc buttons/xbox style/face buttons white.png" -crop 16x16+0+16  +repage /tmp/xbox_Aw.png
convert "tmc buttons/xbox style/face buttons white.png" -crop 16x16+16+16 +repage /tmp/xbox_Bw.png
convert "tmc buttons/xbox style/shoulder buttons grey.png" -crop 24x10+0+12 +repage /tmp/rb_raw.png
convert /tmp/rb_raw.png -filter point -resize 24x16! /tmp/rb_24x16.png
convert /tmp/rb_24x16.png -crop  8x16+0+0 +repage /tmp/rb_left8.png
convert /tmp/rb_24x16.png -crop 16x16+8+0 +repage /tmp/rb_right16.png

python3 tools/png_to_tmc4bpp.py /tmp/xbox_Aw.png    /tmp/Aw.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/xbox_Bw.png    /tmp/Bw.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/rb_left8.png   /tmp/rl.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/rb_right16.png /tmp/rr.bin --palette /tmp/gPalette_1.pal

python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').read_bytes()
a  = pathlib.Path('/tmp/Aw.bin').read_bytes()
b  = pathlib.Path('/tmp/Bw.bin').read_bytes()
rl = pathlib.Path('/tmp/rl.bin').read_bytes()
rr = pathlib.Path('/tmp/rr.bin').read_bytes()
mod = bytearray(orig); mod[0:128]=a; mod[128:256]=b; mod[256:320]=rl; mod[320:448]=rr
pathlib.Path('mods/buttons-xbox/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"

# === Pause menu L/R indicators (gfx_1d7e0 tiles 16..47) ===
convert "tmc buttons/xbox style/shoulder buttons grey.png" -crop 22x10+0+1  +repage /tmp/lb_raw.png
convert "tmc buttons/xbox style/shoulder buttons grey.png" -crop 22x10+0+12 +repage /tmp/rb_raw32.png
convert /tmp/lb_raw.png    -filter point -resize 32x32! /tmp/lb_32.png
convert /tmp/rb_raw32.png  -filter point -resize 32x32! /tmp/rb_32.png

python3 tools/png_to_tmc4bpp.py /tmp/lb_32.png /tmp/lb_32.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/rb_32.png /tmp/rb_32.bin --palette /tmp/gPalette_1.pal

python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_1d7e0_128x128_4bpp_uncompressed.bin').read_bytes()
lb = pathlib.Path('/tmp/lb_32.bin').read_bytes()
rb = pathlib.Path('/tmp/rb_32.bin').read_bytes()
mod = bytearray(orig)
mod[16*32:16*32+512] = lb   # tiles 16..31 = L indicator
mod[32*32:32*32+512] = rb   # tiles 32..47 = R indicator
pathlib.Path('mods/buttons-xbox/gfx/gfx_1d7e0_128x128_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"
```
