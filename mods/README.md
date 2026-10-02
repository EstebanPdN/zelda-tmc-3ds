# mods/

Sample mods for the tmc_pc Tier-1 mod loader. Each subdirectory is a
self-contained mod that overrides one or more runtime asset files.

**Making your own mod?** Start from `example-mod/` — a copy-paste template
with a step-by-step walkthrough of both the drop-in and manifest workflows.

## Available button-prompt mods

| Mod | What it replaces | Style |
|---|---|---|
| `buttons-xbox` | A/B/R in-game prompts (gfx_215e0), L/R pause-menu tabs (gfx_35cb00) | Xbox face-buttons-white + shoulder grey |
| `buttons-gba` | Same as above | GBA-style face buttons + GBA-style L/R shoulder |
| `buttons-ps-grey` | Same as above | PS face buttons greyscale (X/O for A/B) + PS L1/R1 shoulders |

All three mods override the **same two files** (`gfx/gfx_215e0_*.bin`
and `gfx/gfx_35cb00_*.bin`), so **only one button-style mod can be
active at a time**.

## How to switch styles

The mod loader auto-discovers every subdirectory of `mods/` and loads
them in alphabetical order. On file collisions, the first loaded mod
wins. With the sample button mods present, `buttons-gba` wins
alphabetically unless you choose a specific active set with `TMC_MODS`:
comma-separated, leftmost wins, and unlisted mods are disabled.

```sh
# Use Xbox style:
TMC_MODS=buttons-xbox ./build/pc/tmc_pc

# Use GBA style:
TMC_MODS=buttons-gba ./build/pc/tmc_pc

# Use PS-grey style:
TMC_MODS=buttons-ps-grey ./build/pc/tmc_pc

# Disable all mods (no TMC_MODS, all mods/ folders renamed/removed):
./build/pc/tmc_pc
```

When `TMC_MODS` is unset the loader falls back to deterministic
alphabetical auto-discovery. That is useful for local testing, but
explicit `TMC_MODS=...` is clearer when multiple mods touch the same
asset.

## What Tier-1 mods can change

Tier 1 mods are runtime asset replacements only. They can replace files
the asset loader asks for under `assets/`: `gfx/`, `palettes/`,
`animations/`, `sprites/`, `tilemaps/`, `maps/`, `room_props/`,
`data/`, `misc/`, and text JSON-derived runtime files. They do not add
new engine code, script opcodes, entities, rooms, or asset IDs.

Lookup order for a requested asset is:

1. active mod replacement;
2. mounted `assets/*.pak`;
3. loose `assets/` file;
4. ROM fallback in the caller, if that subsystem has one.

## Mod layouts

The simplest mod mirrors the runtime asset tree:

```text
mods/my-mod/
  gfx/gfx_215e0_32x32_4bpp_uncompressed.bin
  palettes/palette_1234.bin
```

Auto-discovery registers every regular file below the mod directory as a
replacement for the same relative asset path. It skips `mod_manifest.json`,
`README*`, dotfiles, `.git/`, and `assets-src/`.

For renamed/shared files, add `mod_manifest.json`:

```json
{
  "name": "my-mod",
  "replace": {
    "gfx/gfx_215e0_32x32_4bpp_uncompressed.bin": "files/buttons.bin",
    "palettes/palette_1234.bin": "palettes/warmer.bin"
  }
}
```

`replacements` is accepted as an alias for `replace`. Keys are runtime
asset paths. Values are replacement files, normally relative to the mod
directory. If a value is not found inside the mod, the loader also checks
the parent `mods/` directory so several mods can share one file.

## Stderr log when a button-style mod is active

```
[MOD] active: buttons-xbox -> /…/mods/buttons-xbox
[MOD] 1 mod registered
[MOD] override gfx/gfx_215e0_32x32_4bpp_uncompressed.bin ← …   (in-game prompts, at title-screen load)
[MOD] override gfx/gfx_35cb00_64x56_4bpp_uncompressed.bin ← … (pause-menu L/R, when pause opens)
```

## How each mod was built

See the individual `README.md` inside each subdirectory for the exact
PNG cropping + 4bpp quantisation + tile splicing pipeline. All three
follow the same recipe with different source PNGs.

## Adding your own button style

1. Copy any `buttons-*/` directory as a starting point.
2. Replace the PNGs in `assets-src/` with your style.
3. Re-run the rebuild commands in that mod's `README.md` to regenerate
   the two `.bin` files.
4. Test with `TMC_MODS=your-mod-name ./build/pc/tmc_pc`.

## DMCA / copyright

The `.bin` files contain raw 4bpp tile data derived from the user's
own art (the PNGs in `assets-src/`). The mods do **not** include any
Nintendo-copyrighted graphics. The asset loader reads these files at
runtime from the user's mods/ folder; the binary ships zero modded
pixel data.
