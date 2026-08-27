# Build Instructions

PC port build instructions. The recommended path is `python3 build.py` from
the repo root — it handles dependency checks, ROM detection, asset
extraction, compilation, and staging into `dist/<VERSION>/`. This document
covers the manual paths and build options for when you want finer control.

## Supported ROMs

A copy of the original game is required. Place it in the repository root
using the filename below:

| Version | Filename         | SHA1                                       |
|---------|------------------|--------------------------------------------|
| USA     | `baserom.gba`    | `b4bd50e4131b027c334547b4524e2dbbd4227130` |
| EU      | `baserom_eu.gba` | `cff199b36ff173fb6faf152653d1bccf87c26fb7` |
| JP      | `baserom_jp.gba` | `6c5404a1effb17f481f352181d0f1c61a2765c5d` |

This repository does **not** ship the ROM.

## Dependencies

See the [README](../README.md#dependencies) for per-platform package lists. The
short version:

- **xmake** (build system)
- **SDL3** (window/input/audio)
- **libpng**, **fmt**, **nlohmann-json** (asset tools + game code)
- **libomp** (macOS only, for VirtuaPPU's parallel scanline path)
- **git**, **python3**

xmake auto-downloads any C/C++ library not found on the system, but installing
them via your package manager is faster and reuses the system copy.

## One-shot build (recommended)

```sh
python3 build.py # interactive: choose USA, EU, JP, or all
python3 build.py --usa # non-interactive, USA only
python3 build.py --eur # non-interactive, EU only
python3 build.py --jp  # non-interactive, JP only
python3 build.py --slim # minimal dist (binary only; assets self-extract)
```

The script:

- Checks dependencies and offers to install missing ones.
- Initializes git submodules (`libs/VirtuaAPU`, `libs/agbplay_core`). The software PPU is vendored in-tree at `port/ppu/` (no submodule).
- Scans the repo root, parent directory, and `~/Downloads` for ROMs and
  verifies SHA1 against `tmc.sha1`, `tmc_eu.sha1`, or `tmc_jp.sha1`.
- Compiles the multi-region `tmc_pc` and `asset_extractor`.
- Extracts a runtime asset cache.
- Stages the result under `dist/<VERSION>/`.

Run it:

```sh
cd dist/USA
./tmc_pc
```

`tmc.sav` is written to the working directory.

## Manual xmake invocation

For incremental builds during development:

```sh
xmake f -y --game_version=USA            # configure (USA build)
xmake build tmc_pc                       # compile the game
xmake build asset_extractor              # standalone asset extractor
```

Build artefacts land in `build/pc/`. The binary resolves `baserom.gba`,
`sounds.json`, and the asset trees relative to its own path (with cwd as a
fallback), so `cd build/pc && ./tmc_pc` works as well as a packaged
`dist/USA/` install.

Configure flags worth knowing:

| Flag                   | Effect                                                |
|------------------------|-------------------------------------------------------|
| `--game_version=USA`   | Asset/offset baseline for the build (default).       |
| `--game_version=EU`    | EU baseline (per-region builds only).                 |
| `--game_version=JP`    | JP staging defaults (per-region builds only).         |
| `--pc_avx2=y`          | Enable AVX2 in the PPU renderer (auto on x86-64).     |
| `--multi_region=y`     | One binary that selects USA/EU/JP at runtime (**default**). `=n` builds the classic per-region binary. |

On Linux, set `XMAKE_USE_SYSTEM_SDL3=1` before configuring to use the
distro's SDL3 instead of letting xmake build its own. `build.py` does this
automatically when `pkg-config --exists sdl3` succeeds.

## Slim builds

`python3 build.py --slim` produces `dist/<VERSION>/` containing only
`tmc_pc`. The binary embeds the asset extractor fallback and `sounds.json`,
so bare `tmc_pc` plus any supported ROM is enough — first launch
self-extracts assets next to the binary in ≈3–5 s, subsequent launches
instant.

CI uses this path for the published release tarballs.

## Loose vs. packed assets

By default the runtime asset tree is packed into `.pak` archives by category
(`gfx.pak`, `animations.pak`, etc.). To force loose files instead — useful
when editing assets by hand — pass `--loose-assets`:

```sh
./tmc_pc --loose-assets
```

The first run with this flag wipes any existing `.pak` files and re-extracts
into a loose tree.

## Notes for macOS

Use `python3 build.py` rather than invoking `xmake` directly — the script
sets up `XMAKE_ROOT=y`, the right toolchain hints, and handles the
Homebrew + libomp paths.

If `clang` is missing:

```sh
xcode-select --install
```

If Homebrew packages report as missing even after `brew install`, run
`brew --prefix <name>` to confirm the install location and check
`pkg-config --variable pc_path pkg-config` includes it.

## Troubleshooting

- **"Could not load baserom.gba"** — place the ROM next to the binary, or
  in the working directory you launch from. Supported names: `baserom.gba`,
  `baserom_eu.gba`, `tmc.gba`, `tmc_eu.gba`.
- **First launch is slow / shows EXTRACTING ASSETS** — expected. The asset
  cache is built once, then warm launches skip extraction.
- **Black window on launch** — check `stderr` for ROM-load errors. The port
  surfaces fatal load failures via SDL message box; if you see a black
  window with no message, the launch path likely couldn't write to its
  install directory.
- **"sounds.json was not found"** — the binary embeds a fallback so this is
  rarely fatal, but if you want the editable copy, place `sounds.json` next
  to `tmc_pc` (the build script does this automatically).
