# buttons-gba

GBA-style button prompts for tmc_pc. Replaces TMC's in-game A/B/R and
pause-menu L/R indicators with the GBA-controller-look variants the
user supplied in `gba style/` of their button-pack.

Same override targets and pipeline as `buttons-xbox` — see
`../README.md` for switching with `TMC_MODS` and `../buttons-xbox/README.md`
for the full asset-pipeline explanation. Quick recap:

- `gfx/gfx_215e0_32x32_4bpp_uncompressed.bin` — 448 B with the **A, B,
  R** in-game prompt tiles, quantised against `palettes/gPalette_1.pal`.
- `gfx/gfx_35cb00_64x56_4bpp_uncompressed.bin` — 1792 B with **L/R**
  pause-menu tab indicators in tiles 16..47, quantised against
  `palettes/gPalette_2220.pal` (the pause-menu OBJ palette 5). Font
  glyphs in tiles 0..15 and 48..55 are bit-identical to the original.

## How to load

```sh
TMC_MODS=buttons-gba ./build/pc/tmc_pc
```

## Source PNGs

- `assets-src/face buttons.png`     (28×31 — 2×2 grid of round GBA-style buttons)
- `assets-src/shoulder buttons.png` (53×23 — L/R curvy GBA shoulders + ZL/ZR)

## Rebuild procedure

```sh
cp build/USA/assets/palettes/gPalette_1.pal /tmp/
cp scratch/asset_runs/release_linux_assets/palettes/gPalette_2220.pal /tmp/

# Face buttons: 28x31 → crop 14x16 each → upscale to 16x16
convert "tmc buttons/gba style/face buttons.png" -crop 14x16+14+15 +repage -filter point -resize 16x16! /tmp/gba_A.png
convert "tmc buttons/gba style/face buttons.png" -crop 14x16+0+15  +repage -filter point -resize 16x16! /tmp/gba_B.png
python3 tools/png_to_tmc4bpp.py /tmp/gba_A.png /tmp/a.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/gba_B.png /tmp/b.bin --palette /tmp/gPalette_1.pal

# Shoulders for in-game R prompt (24x16 → 8x16 + 16x16):
convert "tmc buttons/gba style/shoulder buttons.png" -crop 24x10+29+1 +repage /tmp/gba_R_raw.png
convert /tmp/gba_R_raw.png -filter point -resize 24x16! /tmp/gba_R_24x16.png
convert /tmp/gba_R_24x16.png -crop  8x16+0+0 +repage /tmp/gba_RL.png
convert /tmp/gba_R_24x16.png -crop 16x16+8+0 +repage /tmp/gba_RR.png
python3 tools/png_to_tmc4bpp.py /tmp/gba_RL.png /tmp/rl.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/gba_RR.png /tmp/rr.bin --palette /tmp/gPalette_1.pal

# Splice gfx_215e0
python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').read_bytes()
mod = bytearray(orig)
mod[0:128]   = pathlib.Path('/tmp/a.bin').read_bytes()
mod[128:256] = pathlib.Path('/tmp/b.bin').read_bytes()
mod[256:320] = pathlib.Path('/tmp/rl.bin').read_bytes()
mod[320:448] = pathlib.Path('/tmp/rr.bin').read_bytes()
pathlib.Path('mods/buttons-gba/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"

# Shoulders for pause-menu L/R tabs (32x32 each), quantised against palette 5
convert "tmc buttons/gba style/shoulder buttons.png" -crop 24x10+0+1  +repage /tmp/gba_L_raw.png
convert /tmp/gba_L_raw.png -filter point -resize 32x32! /tmp/gba_L_32.png
convert /tmp/gba_R_raw.png -filter point -resize 32x32! /tmp/gba_R_32.png
python3 tools/png_to_tmc4bpp.py /tmp/gba_L_32.png /tmp/lb.bin --palette /tmp/gPalette_2220.pal
python3 tools/png_to_tmc4bpp.py /tmp/gba_R_32.png /tmp/rb.bin --palette /tmp/gPalette_2220.pal

# Splice gfx_35cb00 (tiles 16..47)
python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_35cb00_64x56_4bpp_uncompressed.bin').read_bytes()
mod = bytearray(orig)
mod[16*32:16*32+512] = pathlib.Path('/tmp/lb.bin').read_bytes()
mod[32*32:32*32+512] = pathlib.Path('/tmp/rb.bin').read_bytes()
pathlib.Path('mods/buttons-gba/gfx/gfx_35cb00_64x56_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"
```
