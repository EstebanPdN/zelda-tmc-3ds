# Contributing to Project Picori

Thanks for helping with the Minish Cap PC port! This fork turns the
[zeldaret/tmc](https://github.com/zeldaret/tmc) decompilation into a native PC
game (SDL3 + a software PPU + the agbplay audio engine). The contributions we
most want: **bug fixes** for crashes and rendering/gameplay glitches,
build/packaging improvements, and accessibility / quality-of-life features.

If you want to *decompile* still-undecompiled GBA functions (matching ARM
assembly byte-for-byte), that work belongs upstream at zeldaret/tmc — see
[Decompiling new functions](#decompiling-new-functions) at the bottom.

## Getting set up

- Read the [README](../README.md) for the build (`python3 build.py`, or the faster
  `xmake` dev cycle) and [INSTALL.md](INSTALL.md) for build options.
- You need your own ROM at the repo root — it is **not** shipped. USA
  `baserom.gba`, SHA1 `b4bd50e4131b027c334547b4524e2dbbd4227130`.
- There is **no automated test suite.** Verification is interactive play, the
  **F8** warp/debug menu, capturing repros with **F9**, and comparing against
  GBA reference behaviour.
- Code-quality conventions for this port (header ownership, the metrics that
  matter, when to add a characterization test) live in
  [docs/MAINTAINABILITY.md](MAINTAINABILITY.md).


## Your first contribution

If you want one path through the repo that teaches the conventions without
requiring GBA reverse-engineering first, do this:

1. Build the native port with `python3 build.py --usa` or
   `xmake build -y tmc_pc`.
2. Run the documented smoke path and make sure it reaches
   `Port layer initialized. Entering AgbMain...`.
3. Read the [PC port module map](#pc-port-module-map) below before opening
   files. Most first fixes live in `port/`, not in `src/`.
4. Pick a contained change:
   - a persisted setting in `port_runtime_config.*`;
   - an F8 menu tweak in `port_imgui_menu.cpp`;
   - a ROM/asset/mod path issue in `port_asset_loader.*` or `port_mods.*`;
   - a crash fix in `src/` guarded behind `#ifdef PC_PORT`.
5. Follow the owner-file rule: change the runtime path first, then the UI,
   then the docs if the user-visible behavior changed.
6. Build again, play-test the affected area, and say exactly what you tested
   in the PR.

If you are working in the decompiled engine (`src/`) or want to move a
function from `asm/` into C, read [docs/DECOMP-ONBOARDING.md](DECOMP-ONBOARDING.md)
first. That document covers the dual GBA/PC build, agbcc quirks, and the
byte-matching workflow.

## Source layout

| Path | What it is |
|------|------------|
| `src/` | C decompilation of the GBA game. **Edit carefully** — many functions still pattern-match the ARM original and use `super` (= `&this->base`). |
| `port/` | PC-only glue: main loop, ROM mmap, asset loader, PPU/audio/input bridges, quicksave, bug-report, debug menu. Most port work happens here and in guarded `src/` edits. |
| `port/ppu/` | Software GBA PPU renderer, vendored first-party (GPL-3.0). Edit directly; guard every change with the parity gate — see [Renderer changes](#renderer-changes-portppu). |
| `libs/agbplay_core` | GBA M4A audio engine (LGPL-3.0 submodule). |
| `asm/` | Undecompiled functions, linked alongside `src/` via `linker.ld`. |
| `data/`, `assets/` | GBA data (`.s`) and the extracted runtime asset trees. |


### PC port module map

The `port/` directory is intentionally thin glue around the decompiled GBA
engine, but it has grown into several subsystems. Start from the owner file
below instead of grepping randomly:

| Area | Owner files | Notes |
|------|-------------|-------|
| Process boot | `port_main.c` | Reserves the GBA address window, loads `config.json`, creates SDL, runs the prelaunch screen, selects the active mod set, then loads ROM/assets/audio before entering `AgbMain()`. |
| Runtime config / input | `port_runtime_config.*`, `port_config.h`, `port_bios.c` | `port_runtime_config.*` owns persisted settings and gamepad/key bindings. `port_bios.c` polls SDL events and handles global hotkeys (`F5` save, `F6` load/TTS stop, `F7` TTS, `F8` menu, `F9` bug report, `F10` accessibility scan, `F12` upscaler). |
| ROM / native address bridge | `port_rom.*`, `port_gba_mem.*`, `port_offset_*.h` | Converts ROM/GBA addresses into native pointers. Use these helpers; never cast a raw `0x08...` address. |
| Assets / mods | `port_asset_bootstrap.*`, `port_asset_loader.*`, `port_asset_pak*`, `port_asset_pipeline.*`, `port_mods.*` | Asset lookup is: mod override → mounted `.pak` → loose `assets/` file → ROM fallback at the caller. Mods are Tier 1 asset replacements; see `mods/README.md`. |
| Rendering | `port_ppu.*`, `port/ppu/src/*`, `port_gpu_renderer.*`, `port_filter.*`, `port_upscale.*`, `port_glslp_*` | `port_ppu.cpp` owns presentation; `port/ppu/` is the vendored software PPU (edit directly, guard with the parity gate). SDL_GPU is an optional backend. |
| Audio | `port_audio.*`, `port_m4a_backend.*`, `port_m4a_stubs.c`, `port_audio_mute.*` | `port_audio.c` owns SDL audio device/thread plumbing; `port_m4a_backend.cpp` adapts agbplay/M4A data. |
| Debug/UI | `port_imgui_menu.cpp`, `port_debug_menu.*`, `port_debug_actions.c`, `port_bugreport.*`, `port_repro_*.c` | `port_imgui_menu.cpp` draws the modern F8 UI; `port_debug_menu.*` is the legacy page model still used by keyboard/controller navigation. Repro files are env-gated harnesses for specific bugs. |
| Randomizer | `port/rando/*` | Best documented subsystem; read `port/rando/README.md` before touching it. `rando_logic_test` is a focused self-test target. |
| Native stubs | `port_stubs.c`, `port_gameplay_stubs.c`, `port_linked_stubs.c` | Hand-written replacements for missing GBA assembly/data symbols. Prefer a real typed implementation here over a placeholder. |
| Generated data/tables | `stubs_autogen.c`, `data_stubs_autogen.c`, `data_const_stubs.c`, `port_room_funcs.c`, `port_script_funcs.c`, `port_rom_tables.c`, `port_asset_index.c`, `generated_sounds_embed.cpp` | Do not hand-edit. Regenerate or add a hand-written override in the right owner file. |

Header rule: if a symbol is used across files, put its declaration in an
owner header and include that header. Do **not** add new file-local
`extern` declarations in consumers; they hide coupling and make refactors
unsafe.

### Adding a persisted setting

Adding a persisted setting usually touches five places:

1. Add the runtime field and default JSON key in `port_runtime_config.cpp`.
2. Parse it in `Port_Config_Load`.
3. Add getter/setter declarations in `port_runtime_config.h`.
4. Make the setter update `sConfigJson[...]` and call `SaveConfig()`.
5. Add the UI control in `port_imgui_menu.cpp` only after the runtime path
   works without UI.

Adding a new moddable asset category should keep the same resolution order:
manifest/auto-discovered mod replacement first, then `.pak`, then loose
`assets/`, then a ROM fallback at the engine boundary. Do not add a second
asset search path convention.

### Stub taxonomy and the dual-build decision tree

The repo has two consumers for gameplay code:

- the **GBA ROM build** (`xmake rom`): `src/**.c` + `asm/` + `data/`,
  linked by `linker.ld`;
- the **PC port** (`tmc_pc`): the same `src/**.c` plus the native
  bridge/stub layer in `port/`.

That means "fix it in one place" is only true when the fix belongs in
shared gameplay C (`src/`). If you change a gameplay behavior only in a
PC stub, the ROM build still behaves differently. Use this decision tree:

1. **Is the bug in shared gameplay logic?**
   - Yes → fix it in `src/`.
   - If the fix is only needed on native builds, guard the divergent path
     with `#ifdef PC_PORT` and comment why the GBA tolerated the issue.
2. **Is the symbol still undecompiled in `asm/` and only needed by the PC
   build?**
   - Function body / gameplay helper → hand-written stubs live in
     `port_stubs.c`, `port_gameplay_stubs.c`, or `port_linked_stubs.c`.
   - Data/table symbol → generated shims live in `stubs_autogen.c`,
     `data_stubs_autogen.c`, or `data_const_stubs.c`.
3. **Is it a room-init or script-call address lookup?**
   - Room property callbacks → `port_room_funcs.c` (auto-generated map
     from `(area, room)` to native function pointers).
   - Script `Call` / `CallWithArg` targets → `port_script_funcs.c`
     (auto-generated address → native function table).
4. **Is it a PC-only service/API?**
   - It belongs in the owning `port_*.c/.cpp` file, declared in its
     owner header, and consumed by `#include` — not by a new file-local
     `extern`.

Rules of thumb:

- `src/**.c` is shared. Start there before adding a stub.
- `*_autogen.c` / `data_const_stubs.c` / `port_room_funcs.c` /
  `port_script_funcs.c` are generated or table-driven — do not hand-edit
  them unless you are also changing their generator/source of truth.
- If the linker says a symbol is missing, grep for it first. A surprising
  number of "missing" gameplay functions already exist as placeholders in
  one of the `port_*stubs*.c` files and just need a real implementation.
- Keep the two-build mental model in your head at all times:
  `src/` serves **both** builds; `port/` serves **only** the native port.

## The one thing to internalise: GBA → PC differences

The port compiles GBA game logic natively for x86-64 — it is **not** an
emulator. Most bugs come from a handful of hardware-behaviour differences.
**When you fix one, guard it behind `#ifdef PC_PORT`** so the code still matches
the GBA original, and comment it referencing the bug.

- **Pointers are 8 bytes, not 4.** Any struct with embedded pointers shifts
  layout on PC. Keep both layouts in sync with the
  `PORT_STATIC_ASSERT_OFFSET/SIZE` macros (`include/global.h`). Enemy
  subclasses that wrap `Entity base` sometimes need an extra 4 bytes of
  `#ifdef PC_PORT` padding (the "#98" pattern — template in
  `src/enemy/eyegore.c`).
- **NULL / out-of-range reads crash here.** On GBA, reading address `0` or an
  OOB table index returned harmless garbage; on PC it SIGSEGVs. Guard the
  deref / clamp the index and early-return — don't "fix" the underlying logic.
  Canonical examples: `src/npc/cat.c` (#91),
  `src/projectile/darkNutSwordSlash.c` (#97).
- **Never dereference a raw GBA address.** ROM/EWRAM/IWRAM pointers
  (`0x08…/0x02…/0x03…`) must be routed through `Port_ResolveRomData`,
  `Port_ReadU16/U32`, or `gEwram[]`/`gIwram[]` (`port/port_rom.*`).

These bug classes recur across `src/` — most new crashes are another instance
of one of them. The existing `#ifdef PC_PORT` fixes and their comments (grep
for `PC_PORT`) are the best reference when you hit one.

## Fixing a bug

1. **Reproduce it.** Bug reports come as a `bugreport_*` folder (screenshot,
   `save.bin`, `state.txt`, and on crashes `backtrace.txt` + `maps.txt`). Drop
   `save.bin` next to `tmc_pc` as `tmc.sav` to load the exact state. Press **F9**
   anytime to capture your own bundle.
2. **Locate a crash.** On Linux, resolve `backtrace.txt`'s addresses with
   `addr2line -e build/pc/tmc_pc -fpi <addr − base>` (build the *same* commit),
   then inspect the faulting instruction with
   `objdump -d build/pc/tmc_pc --disassemble=<Function>`.
3. **Fix defensively**, behind `#ifdef PC_PORT`, with a comment naming the bug
   and why the GBA tolerated it.
4. **Build and play-test** the affected area — the F8 warp menu makes this fast.
5. **Commit one fix per commit**, message style `fix(port): <short description>`
   (reference an issue number when there is one).

## Renderer changes (port/ppu)

The software GBA PPU lives in-tree at `port/ppu/` (vendored, GPL-3.0-or-later;
it was formerly a patched `libs/ViruaPPU` submodule — see `port/ppu/README.md`).
**Edit the source directly** — no patches, no submodule reset.

Every render change MUST be guarded by the byte-exact parity gate:

```sh
tools/ppu_parity_check.sh        # must PASS before and after a change you intend to be render-neutral
```

The renderer is integer-only, so the golden hashes in
`tools/ppu_golden_hashes.txt` are portable (the same gate runs in CI). If a
change *intentionally* alters output, regenerate the golden
(`tools/ppu_parity_check.sh --update`) and justify it in the commit. The
committed corpus is hermetic (boot/title screens); for gameplay coverage that
needs a save, point `PPU_CORPUS`/`PPU_GOLDEN` at a local corpus (see the header
of `tools/ppu_corpus.txt`).

## Submitting changes

- Keep PRs focused — one logical change per PR where practical.
- Match the existing code style and the `#ifdef PC_PORT` convention; don't
  reformat unrelated lines.
- Say **how you tested** (which area / repro), since there is no CI test gate.
- For larger work, open an issue first so we can coordinate.

- If you touched `src/`, or anything related to `asm/`/`linker.ld`, read
  [docs/DECOMP-ONBOARDING.md](docs/DECOMP-ONBOARDING.md) before posting the
  PR. The shared-engine / dual-build rules are different from normal app code.

## Decompiling new functions

Matching still-undecompiled ARM functions byte-for-byte happens in the upstream
[zeldaret/tmc](https://github.com/zeldaret/tmc) project — its build produces the
GBA ROM and verifies the SHA1 match, which this PC fork (built for x86-64) can't
do. Upstream's CONTRIBUTING guide walks through a full `asm/*.s` → `src/*.c`
example. When you bring a newly decompiled function into this port, keep the
`#ifdef PC_PORT` / `PORT_STATIC_ASSERT` layout guards intact so it works under
8-byte pointers.
