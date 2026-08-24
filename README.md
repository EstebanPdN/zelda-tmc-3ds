# The Minish Cap 3DS

<img width="1672" height="941" alt="The Minish Cap 3DS" src="https://github.com/user-attachments/assets/db99e777-12a2-4222-86c3-7c8f14062586" />

Nintendo 3DS dual-screen port of *The Legend of Zelda: The Minish Cap*, based on the open-source Minish Cap decompilation, Project Picori, and the dual-screen Android port.

Made with the help of Codex.

This project is based on open-source work from:

* [samyost1/tmc-android](https://github.com/samyost1/tmc-android) — dual-screen Android source base
* [Project Picori](https://github.com/999sian/tmc) — native Minish Cap engine and port infrastructure
* [zeldaret/tmc](https://github.com/zeldaret/tmc) — original decompilation

No ROM or extracted Nintendo game assets are distributed with this project. You must provide your own legally obtained compatible Game Boy Advance ROM.

## Community

Join my Discord for updates, support, bug reports, testing builds, suggestions, and other Nintendo 3DS homebrew projects:

https://discord.gg/SMW49UMkw

## Features

* Native Nintendo 3DS port with full dual-screen support.
* Supports both USA and European ROMs with automatic region detection.
* True widescreen gameplay on the 400x240 top screen, plus Original and Stretch display modes.
* Bottom-screen interface with live map, dungeon information, quest status, touch item controls, and settings.
* New 3DS enhancements including 804 MHz mode, L2 cache, multi-core rendering, and optional 2x–5x turbo using the C-Stick.
* Multiple display styles including Pixel Perfect, Scaled, Bilinear, and Blur.
* On New Nintendo 3DS, the existing Wide + Pixel Perfect combination enables
  experimental Full View: compatible outdoor rooms render at 400x240, while
  supported interiors use a centered 200x120 view scaled exactly 2x.
* Built-in Project Picori Randomizer support with separate normal and randomized save data.
* Native stereo audio, persistent settings, reliable save handling, FPS tools, and diagnostic dumps for bug reports.

## Performance

The port automatically selects a performance profile for the detected console.

**New Nintendo 3DS** keeps the full-presentation path with 804 MHz mode, L2 cache, multi-core rendering, and C-Stick turbo.

**Old Nintendo 3DS** uses an optimized renderer, lower-overhead audio path, and adaptive presentation-skip profile. When rendering cannot sustain 60 visual FPS, the engine can continue advancing near its 60 Hz target instead of slowing the entire game down. Input, touch, audio, and lifecycle handling continue to run on skipped presentation ticks.

The bottom-screen worker avoids redundant static redraws while keeping touch input sampled on every engine tick.

## Installation

1. Install the CIA with FBI, or use the 3DSX build with the Homebrew Launcher.

```text
tmc-3ds-v1.2-E6.cia
```

2. Create this folder on your SD card:

```text
sdmc:/3ds/The Minish Cap 3DS/
```

3. Place your legally obtained USA or European `.gba` ROM inside that folder.

The ROM can have **any filename** as long as it uses the `.gba` extension.

### Recommended ROM

Both the **USA** and **European** versions are supported, but I **highly recommend using the European ROM**.

The European version is the ROM I used throughout the development and testing of this port, so it has received by far the most testing.

The USA version should be compatible, but it behaves slightly differently in some parts of the game, so you may encounter bugs that are not present when using the European version.

The European ROM also includes multiple languages, **including English**, so there is generally no disadvantage to using it.

Expected clean ROM SHA-1 values:

```text
USA:    b4bd50e4131b027c334547b4524e2dbbd4227130
Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7
```

The ROM stays on your SD card and is never included in the CIA.

### Audio

Audio requires a working Nintendo 3DS DSP firmware setup.

If homebrew audio is not working, open the Luma3DS Rosalina Menu and use:

```text
Miscellaneous options > Dump DSP firmware
```

## Diagnostics

If you encounter a crash, graphical bug, performance problem, or anything unusual, press:

```text
L + R + A
```

The port will pause and create a diagnostic dump containing screenshots, memory information, runtime state, performance data, and other information that can help identify the problem.

Dumps are saved under:

```text
sdmc:/3ds/The Minish Cap 3DS/dumps/
```

Please send the dump when reporting bugs whenever possible.

## Releases

Every GitHub release includes:

* Installable CIA
* Homebrew Launcher 3DSX
* FBI QR code
* Source code archive

Latest release:

https://github.com/EstebanPdN/zelda-tmc-3ds/releases/latest

## Building

Requirements:

* devkitPro
* devkitARM
* libctru
* Citro2D
* Citro3D
* CMake
* `makerom` and `bannertool` for CIA packaging

Build with:

```sh
chmod +x platform/3ds/build.sh
./platform/3ds/build.sh
```

Builds are generated under:

```text
build-3ds/game/tmc-3ds-v1.2-E6.cia
build-3ds/game/tmc-3ds-v1.2-E6.3dsx
```

The build does not include or embed a ROM.

## Credits

* [samyost1/tmc-android](https://github.com/samyost1/tmc-android) — dual-screen Android source base used for this port
* [Project Picori](https://github.com/999sian/tmc) — native Minish Cap engine and port infrastructure
* [Raekwon1603/tmc-android](https://github.com/Raekwon1603/tmc-android) — Android packaging and platform work behind the dual-screen fork
* [zeldaret/tmc](https://github.com/zeldaret/tmc) — original decompilation
* Esteban PDN — Nintendo 3DS port and release maintenance

## License and Legal Notice

Source code is distributed under GPL-3.0. See [LICENSE](LICENSE).

Third-party components retain their respective licenses as listed in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

Nintendo owns *The Legend of Zelda*, *The Minish Cap*, and all associated game content.

This is an unofficial fan-made project and is not affiliated with or endorsed by Nintendo.

No ROM, extracted Nintendo game assets, save data, or firmware is distributed with this project.
