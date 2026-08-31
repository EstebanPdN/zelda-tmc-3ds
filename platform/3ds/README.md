# The Minish Cap 3DS Platform

This target builds the native dual-screen Nintendo 3DS frontend.

Outside gameplay, the bottom screen shows a procedural gold-framed Triforce;
touching it opens the existing Minish Cap Settings hierarchy.

## Console Installation

Install the universal CIA:

```text
tmc-3ds-v1.3-E3.cia
```

Then create this directory on the SD card:

```text
sdmc:/3ds/The Minish Cap 3DS/
```

Place a legally obtained clean ROM there. Any `.gba` filename is accepted.
The port detects USA and European game codes at runtime and activates the
matching internal data profile before boot. Expected SHA-1 values:

```text
USA:    b4bd50e4131b027c334547b4524e2dbbd4227130
Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7
```

The ROM is read locally from the SD card and is never copied into the CIA.

Audio requires a working 3DS DSP firmware setup. On Luma3DS, use Rosalina's
`Dump DSP firmware` option if homebrew audio is unavailable.

## Display

- Top screen: selectable Wide, Original, and Stretch aspect ratios.
- Display styles, in menu order: linearly filtered Blur, 2x sharp Bilinear,
  and centered one-pixel-per-source-pixel Pixel Perfect. Fresh installations
  start with Bilinear and Stretch; valid saved choices are restored on later
  launches.
- Experimental Full View: on New 3DS, selecting Wide + Pixel Perfect renders
  compatible outdoor rooms at 400x240 and supported interiors at a centered
  200x120 logical view scaled exactly 2x. Dialogues, transitions and unsupported
  effects fall back safely to the established presentation.
- Bottom screen: 320x240 map, dungeon/status information and touch item UI.
- Rendering: PICA200/Citro2D presenter fed by the software GBA PPU.
- Performance profile: selected automatically from the detected console model.
- New 3DS: requests 804 MHz, L2 cache and access to the extra application core.
- New 3DS turbo: hold the C-stick in any direction and select 2x through 5x
  game speed from the Gameplay settings.
- Old 3DS: experimental CPU-renderer and audio optimizations plus adaptive
  presentation skipping target 60 Hz engine timing when visual rendering falls
  behind. Input, touch, audio and lifecycle processing continue on skipped
  presentation ticks; visual FPS may be lower.
- Bottom-screen scheduling: touch state is sampled on every engine tick;
  interactive hitboxes are promoted with the physically visible buffer, and
  unchanged static pages avoid redundant paint/upload work.
- Settings: Minish Cap-themed Screen, Gameplay, Developer, and Randomizer submenus with
  persistent options, a manual memory-dump command, and a live diagnostics
  overlay.
- Randomizer: persistent Project Picori randomizer mode in its own submenu. Mode
  changes require confirmation, clear only the active profile and related
  state, keep the ROM, and restart with isolated normal/randomized saves.
- Show FPS: measured presentation cadence in a compact lower-left top-screen
  box.
- Diagnostics: press `L + R + A` to pause the game, display `DUMP SAVED`, and
  create `dumps/dump-*` with top and bottom physical-framebuffer BMP and raw
  captures, EWRAM, IWRAM, VRAM, palettes, OAM, I/O and game state, frame
  visual and engine cadence, adaptive-skip data, per-core PPU timings, GPU
  work, audio buffer health, save
  persistence state, memory availability, lifecycle state and complete input
  data.
- System lifecycle: HOME, sleep and application close events are handled by the
  regular 3DS applet loop.
- Console: the development boot console remains visible during startup. Once
  gameplay begins, later stdout/stderr logs are detached from the bottom
  framebuffer so they cannot flicker over the map or touch interface.

The CIA metadata uses a stable title ID and requests SD card access for local
ROM and save data.

## Requirements

- devkitPro with devkitARM, libctru, Citro2D and Citro3D
- CMake with the devkitPro Nintendo 3DS toolchain
- `makerom` and `bannertool` for CIA packaging

Run:

```sh
chmod +x platform/3ds/build.sh
platform/3ds/build.sh
```

The universal packages are written under `build-3ds/game/`. No ROM, extracted
asset package or save data is included in either package.
