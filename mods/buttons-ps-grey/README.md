# buttons-ps-grey

PlayStation-grey-style button prompts for tmc_pc. Replaces TMC's
in-game A/B/R and pause-menu L/R indicators with the greyscale-PS
controller variants from the user's button-pack `ps style/`.

Mapping:

- **TMC A** → PS **X** (cross, top-left of `face buttons white greyscale.png`)
- **TMC B** → PS **○** (circle, top-right)
- **TMC R** (in-game prompt) → PS **R1** (top-right shape of `shoulder buttons grey.png`)
- **Pause-menu L** → PS **L1** (top-left shape)
- **Pause-menu R** → PS **R1** (top-right shape)

Same override targets and pipeline as `buttons-xbox` — see
`../README.md` for switching with `TMC_MODS` and `../buttons-xbox/README.md`
for the full asset-pipeline explanation.

## How to load

```sh
TMC_MODS=buttons-ps-grey ./build/pc/tmc_pc
```

## Source PNGs

- `assets-src/face buttons white greyscale.png` (32×32 — 2×2 grid of square frames with X/O/□/△)
- `assets-src/shoulder buttons grey.png`         (53×23 — L1 top-left, R1 bottom-left, R2/L2 right)

## Rebuild procedure

```sh
cp build/USA/assets/palettes/gPalette_1.pal /tmp/
cp scratch/asset_runs/release_linux_assets/palettes/gPalette_2220.pal /tmp/

# Face buttons: 32x32 with 2x2 layout. X = top-left, O = top-right
convert "tmc buttons/ps style/face buttons white greyscale.png" -crop 16x16+0+0  +repage /tmp/ps_A.png
convert "tmc buttons/ps style/face buttons white greyscale.png" -crop 16x16+16+0 +repage /tmp/ps_B.png
python3 tools/png_to_tmc4bpp.py /tmp/ps_A.png /tmp/a.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/ps_B.png /tmp/b.bin --palette /tmp/gPalette_1.pal

# Shoulders for in-game R prompt — use R1 region
convert "tmc buttons/ps style/shoulder buttons grey.png" -crop 24x10+0+12 +repage /tmp/ps_R_raw.png
convert /tmp/ps_R_raw.png -filter point -resize 24x16! /tmp/ps_R_24x16.png
convert /tmp/ps_R_24x16.png -crop  8x16+0+0 +repage /tmp/ps_RL.png
convert /tmp/ps_R_24x16.png -crop 16x16+8+0 +repage /tmp/ps_RR.png
python3 tools/png_to_tmc4bpp.py /tmp/ps_RL.png /tmp/rl.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/ps_RR.png /tmp/rr.bin --palette /tmp/gPalette_1.pal

# Splice gfx_215e0
python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').read_bytes()
mod = bytearray(orig)
mod[0:128]   = pathlib.Path('/tmp/a.bin').read_bytes()
mod[128:256] = pathlib.Path('/tmp/b.bin').read_bytes()
mod[256:320] = pathlib.Path('/tmp/rl.bin').read_bytes()
mod[320:448] = pathlib.Path('/tmp/rr.bin').read_bytes()
pathlib.Path('mods/buttons-ps-grey/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"

# Pause-menu L/R tabs (32x32 each) — L1 top-left, R1 bottom-left
convert "tmc buttons/ps style/shoulder buttons grey.png" -crop 22x10+0+1  +repage /tmp/ps_L_raw.png
convert "tmc buttons/ps style/shoulder buttons grey.png" -crop 22x10+0+12 +repage /tmp/ps_R_raw32.png
convert /tmp/ps_L_raw.png   -filter point -resize 32x32! /tmp/ps_L_32.png
convert /tmp/ps_R_raw32.png -filter point -resize 32x32! /tmp/ps_R_32.png
python3 tools/png_to_tmc4bpp.py /tmp/ps_L_32.png /tmp/lb.bin --palette /tmp/gPalette_2220.pal
python3 tools/png_to_tmc4bpp.py /tmp/ps_R_32.png /tmp/rb.bin --palette /tmp/gPalette_2220.pal

# Splice gfx_35cb00 (tiles 16..47)
python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_35cb00_64x56_4bpp_uncompressed.bin').read_bytes()
mod = bytearray(orig)
mod[16*32:16*32+512] = pathlib.Path('/tmp/lb.bin').read_bytes()
mod[32*32:32*32+512] = pathlib.Path('/tmp/rb.bin').read_bytes()
pathlib.Path('mods/buttons-ps-grey/gfx/gfx_35cb00_64x56_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"
```
