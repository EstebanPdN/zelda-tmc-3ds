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
* Russian game text and a dedicated Cyrillic UI font when using a compatible USA ROM.
* Supports both USA and European ROMs with automatic region detection.
* True widescreen gameplay on the 400x240 top screen, plus Original and Stretch display modes.
* Bottom-screen interface with live map, dungeon information, quest status, touch item controls, and settings.
* New 3DS enhancements including 804 MHz mode, L2 cache, multi-core rendering, and optional 2x–5x turbo using the C-Stick.
* Multiple display styles including Pixel Perfect, Scaled, and Blur.
* Built-in Project Picori Randomizer support with separate normal and randomized save data.
* Native stereo audio, persistent settings, reliable save handling, FPS tools, and diagnostic dumps for bug reports.

## Performance

For now, this port is only really playable on **New Nintendo 3DS systems**.

The **Old Nintendo 3DS currently has significant FPS drops**, so I do not recommend using it there yet.

For the next version, I am going to focus heavily on performance optimization and try to improve Old 3DS support as much as possible.

## Installation

1. Install the CIA with FBI, or use the 3DSX build with the Homebrew Launcher.

2. Create this folder on your SD card:

```text
sdmc:/3ds/The Minish Cap 3DS/
```

3. Place your legally obtained USA or European `.gba` ROM inside that folder.

The ROM can have **any filename** as long as it uses the `.gba` extension.

### Recommended ROM

Both the **USA** and **European** ROMs are still accepted by the port. However, the Russian localization in the `rus` branch is currently enabled only for the **clean USA ROM (game code BZME)**.

Use the USA ROM if you want Russian game text. With a European ROM, the port can still start, but the Russian text/font override is intentionally skipped and the ROM's original language data is used.

Expected clean ROM SHA-1 values:

```text
USA:    b4bd50e4131b027c334547b4524e2dbbd4227130
Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7
```

For the Russian build, the **USA SHA-1 above is the recommended one**. Do not use a separately pre-patched/russified `.gba` file; the Russian translation and fonts are supplied by this port.

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

The 3DS build uses devkitARM/libctru through devkitPro and CMake. `platform/3ds/build.sh` always builds the `.3dsx`; when `makerom` and `bannertool` are available it also packages a `.cia`. No ROM is required for compilation and no ROM is embedded in either output.

### Supported host environments

The build script is a Bash script, so it needs a Unix-like shell. The practical options are:

* **Linux x86_64** — native devkitPro installation or Docker/Podman. The container workflow below has been tested with Podman on x86_64 Linux.
* **Linux arm64** — native devkitPro installation or the official devkitPro container. The current `devkitpro/devkitarm` image is published for both `linux/amd64` and `linux/arm64`.
* **macOS (Intel and Apple Silicon)** — native devkitPro installation or Docker Desktop/Podman. The container image has both amd64 and arm64 variants.
* **Windows 10/11** — use **WSL2** with Docker/Podman for the most reproducible setup, or use the devkitPro MSYS2 environment for a native Windows toolchain. Plain `cmd.exe`/PowerShell is not enough to run `platform/3ds/build.sh` directly.

devkitPro officially distributes its toolchains through pacman: Windows uses its MSYS2-based environment, while Linux/macOS can use devkitPro pacman. The `3ds-dev` package group installs the normal Nintendo 3DS development toolchain and libraries.

### Requirements

For a normal 3DS build:

* Git
* Bash
* CMake
* devkitPro / devkitARM
* libctru
* Citro2D
* Citro3D

For `.cia` packaging you also need:

* `makerom`
* `bannertool`

The build script searches for the CIA tools in this order:

1. `TMC3DS_TOOLS_ROOT` (`makerom` and `bannertool` inside that directory)
2. `makerom` / `bannertool` from `PATH`
3. `$DEVKITPRO/tools/bin/`

If the tools are missing, the build is still successful, but only the `.3dsx` is produced.

### Recommended: reproducible Docker/Podman build

The official `devkitpro/devkitarm` image already contains devkitARM, libctru, CMake and the normal 3DS development environment. `makerom` and `bannertool` are not included, so the commands below build them once into a repository-local `.3ds-tools/` directory.

From the repository root, start a container.

Linux with Podman (the `:Z` suffix handles SELinux labels):

```sh
podman run --rm -it \
  -v "$PWD:/work:Z" \
  -w /work \
  devkitpro/devkitarm:latest \
  bash
```

Docker Desktop / Docker Engine:

```sh
docker run --rm -it \
  -v "$PWD:/work" \
  -w /work \
  devkitpro/devkitarm:latest \
  bash
```

Inside the container:

```sh
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
mkdir -p /work/.3ds-tools/bin
```

#### Build and install makerom

This project has been tested with **makerom v0.19.0** from Project_CTR. Build the dependency target first and the program target second; do not run `deps` and `program` as parallel make targets.

```sh
rm -rf /tmp/Project_CTR

git clone \
  --depth 1 \
  --branch makerom-v0.19.0 \
  https://github.com/3DSGuy/Project_CTR.git \
  /tmp/Project_CTR

make -C /tmp/Project_CTR/makerom deps -j4
make -C /tmp/Project_CTR/makerom program -j4

install -m 755 \
  /tmp/Project_CTR/makerom/bin/makerom \
  /work/.3ds-tools/bin/makerom
```

Verify it:

```sh
/work/.3ds-tools/bin/makerom -help | head
```

#### Build and install bannertool

```sh
rm -rf /tmp/bannertool

git clone \
  --depth 1 \
  --recurse-submodules \
  --shallow-submodules \
  https://github.com/diasurgical/bannertool.git \
  /tmp/bannertool

make -C /tmp/bannertool -j4

BT="$(find /tmp/bannertool -type f -name bannertool -perm -111 | head -1)"
test -n "$BT"
install -m 755 "$BT" /work/.3ds-tools/bin/bannertool
```

Verify both tools:

```sh
ls -lh /work/.3ds-tools/bin/
/work/.3ds-tools/bin/makerom -help | head
/work/.3ds-tools/bin/bannertool 2>&1 | head
```

#### Build the project

Point the build script at the repository-local CIA tools and build:

```sh
export TMC3DS_TOOLS_ROOT=/work/.3ds-tools/bin
chmod +x platform/3ds/build.sh
./platform/3ds/build.sh
```

A successful CIA-capable build ends with output similar to:

```text
Created SMDH ".../build-3ds/game/tmc-3ds.icn".
Created banner ".../build-3ds/game/tmc-3ds.bnr".
Ready:
  .../build-3ds/game/tmc-3ds-v0.34.3dsx
  .../build-3ds/game/tmc-3ds-v0.34.cia
```

The exact version number is read from `platform/3ds/version.txt`.

Because `.3ds-tools/` lives inside the mounted repository, the tools survive `--rm` container removal. On later builds you only need to start the container, set the environment, and run the build script:

```sh
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
export TMC3DS_TOOLS_ROOT=/work/.3ds-tools/bin
./platform/3ds/build.sh
```

You can also pass the tools path when starting the container:

```sh
podman run --rm -it \
  -v "$PWD:/work:Z" \
  -w /work \
  -e TMC3DS_TOOLS_ROOT=/work/.3ds-tools/bin \
  devkitpro/devkitarm:latest \
  bash
```

### Native devkitPro build

If you prefer not to use a container, install devkitPro using its official platform-specific instructions, then install the Nintendo 3DS development group. On Debian-based Linux and macOS this is normally:

```sh
sudo dkp-pacman -S 3ds-dev
```

On Windows/MSYS2 or Linux distributions that use pacman directly, the command may be `pacman -S 3ds-dev` without `sudo dkp-`. Follow the current devkitPro installation instructions for your host.

After installation, verify the environment:

```sh
echo "$DEVKITPRO"
echo "$DEVKITARM"
command -v arm-none-eabi-gcc
cmake --version
```

Typical paths are:

```text
DEVKITPRO=/opt/devkitpro
DEVKITARM=/opt/devkitpro/devkitARM
```

For CIA packaging, either place `makerom` and `bannertool` in `$DEVKITPRO/tools/bin`, put them in your `PATH`, or keep them anywhere and point the build at them:

```sh
export TMC3DS_TOOLS_ROOT=/path/to/3ds-tools/bin
```

The same source-build commands from the container section can be used on Unix-like hosts if Git, GNU Make, GCC/G++ and the required native build tools are installed.

Then build from the repository root:

```sh
chmod +x platform/3ds/build.sh
./platform/3ds/build.sh
```

### Output files

Build artifacts are generated under:

```text
build-3ds/game/
```

The important files are:

```text
tmc-3ds-v<version>.3dsx
tmc-3ds-v<version>.cia   # only when makerom + bannertool are available
```

To force a completely clean rebuild:

```sh
rm -rf build-3ds/game
./platform/3ds/build.sh
```

The build does not include or embed a ROM.

### Upstream tool documentation

* devkitPro Getting Started: https://devkitpro.org/wiki/Getting_Started
* devkitPro pacman: https://devkitpro.org/wiki/devkitPro_pacman
* Official devkitARM container: https://hub.docker.com/r/devkitpro/devkitarm
* makerom / Project_CTR: https://github.com/3DSGuy/Project_CTR
* bannertool: https://github.com/diasurgical/bannertool

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
