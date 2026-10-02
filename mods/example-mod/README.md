# example-mod — a template for your first mod

This folder is a copy-paste starting point. It ships with **only this
README**, so the loader auto-discovers it and applies nothing (README files
are skipped). Duplicate the folder, rename it, and add real files.

There are two ways to make a mod. You can mix both.

## 1. Drop-in (no JSON) — easiest

Mirror the runtime asset tree inside your mod folder. Any file you place at the
same relative path overrides that asset:

```
mods/my-mod/
  gfx/gfx_215e0_32x32_4bpp_uncompressed.bin   # overrides assets/gfx/<same name>
  palettes/palette_1234.bin                   # overrides assets/palettes/<same name>
```

The loader walks every regular file under your folder and registers it as a
replacement for the matching `assets/<path>`. It skips `mod_manifest.json`,
`README*`, dotfiles, and anything under `assets-src/` (treat `assets-src/` as
your raw editing source — PNGs, .aseprite, etc. — that never ships).

See the real `buttons-gba`, `buttons-xbox`, `buttons-ps-grey` mods next to this
folder for working drop-in examples.

## 2. Manifest (`mod_manifest.json`) — for renamed/shared files

Use a manifest when your replacement file lives somewhere other than the
mirrored path, or several mods share one file. Copy this into
`mod_manifest.json`:

```json
{
  "name": "my-mod",
  "description": "what this mod changes",
  "replace": {
    "gfx/gfx_215e0_32x32_4bpp_uncompressed.bin": "files/my_buttons.bin",
    "palettes/palette_1234.bin": "palettes/warmer.bin"
  }
}
```

- Keys are runtime asset paths (relative to `assets/`).
- Values are your replacement files, resolved first against the parent `mods/`
  directory, then against your mod folder.
- `replacements` is accepted as an alias for `replace`.

## Activating mods

- By default every folder under `mods/` is auto-loaded, alphabetically; on a
  file collision the **first** mod wins.
- To pick an explicit, ordered set (leftmost wins, others disabled):

  ```bash
  TMC_MODS=my-mod,buttons-xbox ./tmc_pc
  ```

## Checking it worked

The loader prints `[MOD] ...` lines to stderr at startup and now reports
problems instead of failing silently — unknown manifest keys, wrong value
types, missing replacement files, and a per-mod `N replacements loaded`
summary. Run from a terminal and read those lines if a mod doesn't apply.

See `../README.md` for the full reference.
