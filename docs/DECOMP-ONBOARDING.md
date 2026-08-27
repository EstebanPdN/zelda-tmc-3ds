# Decomp & Dual-Build Onboarding

This guide is for contributors working in the **decompiled engine** (`src/`) and
the **dual GBA/PC build**. That seam is the steepest part of the repo: the same
C in `src/` is compiled into two very different binaries, and the code does not
read like normal application code. Read this before touching `src/`.

For the PC port layer (`port/`), GBA→PC bug classes, and submission rules, see
[`CONTRIBUTING.md`](CONTRIBUTING.md). This document deliberately does not
duplicate them.

---

## 1. What this project is, and what you touch

Project Picori is a native PC port of *The Legend of Zelda: The Minish Cap*
(GBA). It is built on the [zeldaret/tmc](https://github.com/zeldaret/tmc)
decompilation: the original game is being rewritten, function by function, from
ARM assembly into C. This fork compiles that C as a native SDL3 application
(`tmc_pc`) instead of (or in addition to) a byte-matching GBA ROM. It is **not
an emulator** — the GBA game logic runs directly on x86-64, with PC shims
standing in for GBA hardware.

The code a contributor touches, by directory:

| Path | What it is |
|------|------------|
| `src/` | Decompiled GBA game logic (C). Compiled into **both** builds. Edit carefully — most of it still pattern-matches the ARM original. |
| `include/` | Shared GBA engine structs, globals, entity/save/screen/map definitions. |
| `asm/` | Still-undecompiled functions as hand-written ARM `.s`. Used by the **ROM build only**. |
| `data/` | GBA data tables as `.s`. ROM build only. |
| `port/` | PC-only glue: SDL3 entry, renderer, audio, ROM/address bridge, and the **stubs** that replace `asm/` on PC. |
| `linker.ld` | ROM link script — hardcodes per-object `.text`/`.rodata` placement (ROM build only). |
| `xmake.lua` | Build graph for both targets and all tools. |

The single most important fact: **`src/**.c` is shared.** A function you
decompile there must satisfy two consumers at once — see §2.

---

## 2. The two builds and the seam

There are two completely separate link targets driven by the same `src/`:

```
 GBA ROM (byte-matching, xmake "rom" task)
   asm/src/*.s  ──┐
   data/**.s    ──┤
   src/**.c     ──┼──► agbcc + arm-none-eabi-{as,ld}  ──►  tmc.gba / tmc.elf
                  │     (linked per-object via linker.ld)
                  └───────────────────────────────────────────────┐
                                                                   │ src/ is
 PC port (native, target "tmc_pc")                                 │ shared
   src/**.c (the same files) ──┐                                   │
   port/port_*.c, *.cpp        ├──► system C/C++ toolchain (SDL3) ─┘──► tmc_pc
   port/*_stubs*.c (hand)      │
   port/*_autogen.c (generated)┘
```

**GBA ROM target** (`xmake rom`, see `xmake.lua` `task("rom")`):
- Compiles every `src/**.c` with **agbcc** (`tools/agbcc/bin/agbcc`, an old
  GBA-era C compiler) plus the `arm-none-eabi` binutils, and assembles every
  `asm/**.s` + `data/**.s` (`xmake.lua:1318,1397-1398`).
- The output bytes must match the original retail ROM. This verification (and
  the SHA1 check) is the **upstream zeldaret/tmc** project's job — the PC fork,
  built for x86-64, cannot produce a matching ROM. See `CONTRIBUTING.md` →
  "Decompiling new functions".
- Links with `linker.ld`, which lists each object's section explicitly
  (`linker.ld:226+`).

**PC port target** (`xmake build -y tmc_pc`, see `xmake.lua` `target("tmc_pc")`):
- Compiles the same `src/**.c` with the host compiler (`-O0 -g`,
  `xmake.lua:1019`), links it against the `port/` layer, SDL3, ViruaPPU, and the
  audio libs.
- Does **not** touch `asm/` at all. The GBA symbols that `asm/` would have
  provided are instead supplied by:
  - hand-written stubs: `port/port_stubs.c`, `port/port_gameplay_stubs.c`,
    `port/port_linked_stubs.c` (`xmake.lua:860,881,868`);
  - generated tables: `port/stubs_autogen.c`, `port/data_stubs_autogen.c`,
    `port/data_const_stubs.c` (`xmake.lua:861-863`).
- Defines `PC_PORT`, `NON_MATCHING`, and the region/language macros
  (`xmake.lua:693`). The `asset_extractor` target defines `PC_PORT` too
  (`xmake.lua:244`); the ROM build never does.

### Where the `#ifdef PC_PORT` guards go

`PC_PORT` is defined **only** for the native targets (`tmc_pc`,
`asset_extractor`). It is the seam marker: code inside `#ifdef PC_PORT` exists
for the PC build; the `#else` (or unguarded) branch is what the ROM build / GBA
hardware sees. Keep the GBA branch byte-faithful and put PC-only behavior behind
the guard, so the same file stays valid for both consumers.

Worked example — `src/projectileUpdate.c` (recently converted from
`asm/src/projectileUpdate.s`):

```c
#ifdef PC_PORT
const ProjectileFn gProjectileFunctions[] = {   // real typed table on PC
    DarkNutSwordSlash, RockProjectile, /* ... */ V3TennisBallProjectile,
};
#else
extern const ProjectileFn gProjectileFunctions[]; // ROM build links the
#endif                                             // original ARM data table
```

On PC the function-pointer dispatch (`gProjectileFunctions[this->id](this)`)
needs real C symbols, so the array is defined here; on the ROM side the same
table already exists as assembled data and is merely `extern`-declared. The
shared `ProjectileUpdate()` body below the guard is identical for both builds.

---

## 3. Reading decompiled code (do not read `src/` like normal code)

Decomp source is generated/transcribed from machine code, then progressively
humanized. Two naming conventions dominate:

- **`sub_080XXXXX`** — an as-yet-unnamed function, where `080XXXXX` is its ROM
  address (`0x08……` is GBA cartridge ROM). Example: `sub_080028E0` called in
  `src/projectileUpdate.c`.
- **`gUnk_080XXXXX`** / **`gUnk_020XXXXX`** — unnamed data. `08…` is ROM
  (const tables), `02…` is EWRAM, `03…` is IWRAM (mutable globals). The
  `linker.ld` EWRAM/IWRAM maps (`linker.ld:13-224`) place many of these by hand.

These are not rare. As of this writing `src/` contains roughly **12,000**
`sub_080…` call sites and roughly **6,900** `gUnk_…` references (most in ROM
space). So expect to read code like `sub_080028E0(this)` where the name tells
you nothing but the address.

How to orient instead of reading top-to-bottom:
- Treat an unnamed `sub_`/`gUnk_` as an address, not a description. To learn what
  it does, look it up by that address, read its body, and infer behavior from
  call sites.
- Lean on the already-named neighbors. Engine entry points are named
  (`AgbMain`, `EnemyUpdate`, `ProjectileUpdate`, `DrawEntity`, the
  `gEntityLists`/`gRoomControls`/`gSave` globals); use them as anchors.
- Many functions still use `super` (= `&this->base`) and pattern-match the ARM
  original — do not "tidy" them into idiomatic C (see `CONTRIBUTING.md`).

When (and how) to rename: as you understand a `sub_`/`gUnk_`, rename it to a
descriptive identifier across the repo — that is the decomp's progress. But
renaming is a **byte-neutral** change for the ROM build: it must still compile to
the same bytes. Pure decomp/renaming work belongs **upstream at zeldaret/tmc**,
which can verify the ROM still matches; this PC fork cannot. Bring the result
here once it lands upstream, preserving the `#ifdef PC_PORT` / `PORT_STATIC_ASSERT`
layout guards.

---

## 4. Decompiling a function end-to-end (byte-matching workflow)

This is the upstream zeldaret/tmc loop; it is summarized here because its
*output* — a function moving from `asm/` into `src/` plus a `linker.ld` edit —
is what you import into this port. The authoritative walk-through is upstream's
CONTRIBUTING (`CONTRIBUTING.md` → "Decompiling new functions").

High-level steps:

1. **Pick a function** still living in an `asm/src/*.s` file (e.g. the
   `EnemyUpdate` body in `asm/src/enemy.s`, `thumb_func_start EnemyUpdate`).
2. **Write equivalent C** in the right `src/` file. The goal is C that, compiled
   under agbcc with the ROM build's flags, emits the *same ARM/THUMB bytes* as
   the original. (Compare `asm/src/enemy.s`'s `EnemyUpdate` to the now-C
   `ProjectileUpdate` in `src/projectileUpdate.c` — same init/disabled/dispatch
   shape.)
3. **Match the bytes.** The ROM build compiles `src/**.c` with agbcc at `-O2`
   (`xmake.lua:1219`: `-O2 -Wimplicit -Wparentheses -Werror -Wno-multichar
   -g3`); a few files need `-mthumb-interwork` (the `interwork_files` list,
   `xmake.lua:1222-1225`). Iterate on the C until the assembled object matches.
4. **Remove the function from the `.s`** so it is no longer assembled from
   `asm/`, leaving the C as the single source.
5. **Update `linker.ld`.** The ROM section map places objects explicitly, so the
   newly-C object must appear where the original `.text` lived. After
   `ProjectileUpdate` was decompiled, `linker.ld:254` reads
   `src/projectileUpdate.o(.text);` — slotted right after the script/data objects
   it followed in ROM, ahead of the interworking-compiled C block.
6. **No xmake edit needed for the file list.** The ROM task globs
   `os.files("src/**.c")` (`xmake.lua:1318`), so a new `src/` file is picked up
   automatically; ordering is controlled by `linker.ld`, not the glob.

Bringing it into this PC fork: add the file to `tmc_pc` (the PC target lists
`src/` files explicitly, e.g. `xmake.lua:944` `add_files("src/projectileUpdate.c")`),
wrap any PC-divergent data/behavior in `#ifdef PC_PORT` (§2), and keep the
`PORT_STATIC_ASSERT_OFFSET/SIZE` layout guards intact for 8-byte pointers.

---

## 5. agbcc gotchas cheat-sheet

agbcc (`tools/agbcc/bin/agbcc`) is a C89-era compiler/preprocessor used only for
the ROM build. It is fussy in ways modern GCC/Clang are not. Because the file is
shared, your `src/` C must satisfy agbcc even though you usually only build
`tmc_pc`. Common traps:

- **No preprocessor `#if/#elif/#else` *inside a function body across brace
  scopes*.** agbcc's preprocessor mis-pairs `#else`/`#endif` when the directives
  straddle `{ }` scopes. Prefer guarding the **whole function** with `#if`
  variants, or select behavior with a compile-time constant macro, instead of an
  in-body `#if … #else … #endif` that opens/closes braces in different arms.
- **Declarations must precede statements** (C89). Don't declare a variable after
  the first statement in a block.
- **Use `bool32`, not C99 `bool`** in engine code. The GBA ABI expects the
  32-bit boolean type used throughout `src/`/`include/`.
- **Multi-char char constants are normal** (`-Wno-multichar` is on,
  `xmake.lua:1219`) — used deliberately for 4-byte tags; don't "fix" them.
- **Packed-bitfield writes are common and layout-sensitive.** Bitfield packing
  differs between toolchains/platforms; the build pins GCC packing on MinGW
  (`-mno-ms-bitfields`, `xmake.lua:171-174`) precisely because a 5+5+6-bit packed
  struct sized differently and shifted every later member (issue #44). Don't
  reorder or retype bitfields casually.
- **Never dereference a raw GBA hardware/ROM address on PC.** On the ROM build a
  `*(vu16*)0x04000000` register access is fine; on PC it SIGSEGVs. Route
  ROM/EWRAM/IWRAM/MMIO access through the port indirection (`Port_ResolveRomData`,
  `Port_ReadU16/U32`, `gEwram[]`/`gIwram[]` in `port/port_rom.*`), guarded by
  `#ifdef PC_PORT`. (See `CONTRIBUTING.md` → "GBA → PC differences".)

If you hit a cryptic agbcc error during a ROM build, it is almost always one of
the above (most often the in-body `#if` mis-pairing).

---

## 6. Building & verifying

**PC port (the common path):**

```bash
# Full, user-style build; stages dist/USA (prompts when needed)
python3 build.py --usa

# Faster incremental dev cycle
xmake f -y --game_version=USA
xmake build -y tmc_pc
```

**Headless smoke test** (the minimum bar from `AGENTS.md` — must reach the
`Entering AgbMain...` line):

```bash
TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./dist/USA/tmc_pc --no-audio
```

A successful launch prints `Port layer initialized. Entering AgbMain...`. There
is **no automated test suite** (`AGENTS.md`, `CONTRIBUTING.md`); verification is
the smoke run plus interactive play via the **F8** warp/debug menu and **F9**
repro capture.

**GBA ROM (byte-matching build):**

```bash
xmake rom            # uses tools/agbcc + arm-none-eabi binutils
```

This requires `arm-none-eabi-gcc`/`as`/`ld`/`objcopy` (or DEVKITARM) and
`tools/agbcc/bin/agbcc`; the task errors out early if either is missing
(`xmake.lua:1152-1195`). It extracts assets if needed, generates enum includes,
preprocesses `linker.ld`, compiles `src/**.c` with agbcc, assembles
`asm/**.s` + `data/**.s`, links via `linker.ld`, and `objcopy`s to `.gba`.

A ROM is required at the repo root for asset extraction / full runtime (USA
`baserom.gba`, SHA1 `b4bd50e4131b027c334547b4524e2dbbd4227130`; see
`CONTRIBUTING.md`).

---

## 7. Where to get help / further reading

- **`CONTRIBUTING.md`** — source layout, the GBA→PC bug classes (8-byte
  pointers, NULL/OOB reads, raw-address derefs), the `#ifdef PC_PORT`
  convention, the full PC-port module map, and the upstream decompilation
  pointer.
- **`AGENTS.md`** — architecture & data flow, key directories/files, the full
  command list, conventions, and the testing/QA expectations.
- **`port/rando/README.md`** — the best-documented subsystem; read it before
  touching the randomizer (and as a model for how a port subsystem is structured).
- **Upstream [zeldaret/tmc](https://github.com/zeldaret/tmc)** — where
  byte-matching decompilation of new ARM functions actually happens and is
  verified.
- **`docs/`** — audits, parity notes, and the widescreen/audio/perf design
  documents for deeper PC-side context.
