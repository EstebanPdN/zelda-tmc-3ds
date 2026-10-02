# Maintainability guide (PC port)

Conventions and reasoning for keeping `tmc_pc` easy to change and easy to
onboard into. Scope is the **PC port** (`port/`, the port-owned parts of
`src/`); byte-matching the GBA decomp is upstream at zeldaret/tmc and out of
scope here. See [CONTRIBUTING.md](../CONTRIBUTING.md) for the module map and
build/verify loop.

## Principles we follow

- **Low coupling, high cohesion.** A module does one job; a change shouldn't
  ripple across files. This is the strongest predictor of "safe to change."
- **One source of truth (DRY).** A symbol, fact, or constant is declared once.
- **KISS / YAGNI.** Boring over clever; don't build for hypothetical futures.
- **Document the *why*.** The code says *what*; comments and these docs say
  *why*. Cross-file decisions get a comment at the seam.

Anti-patterns to avoid: god objects, scattered global state *added by the
port* (the GBA globals are a fixed constraint — see below), circular includes,
and large multi-purpose functions.

## Header ownership rule (C/C++)

This is the rule most worth enforcing here, because the decomp tends to grow
inline `extern`s.

- **Declarations live in an owner header; both the caller and the definer
  `#include` it.** Never re-declare a cross-file symbol with an inline
  `extern` at the use site — it only works by include-order luck and breaks
  unpredictably when order changes, and it silently drifts from the
  definition. ([Google C++ Style Guide][g], [C++ Coding Standards ch.23][cs])
- **Don't rely on transitive includes.** If a file uses a symbol, it includes
  the header that declares it — not some other header that happens to drag it
  in. ([Google C++ Style Guide][g])
- **One module = one `.h` + `.c[pp]`** with a clear responsibility; the `.h`
  holds declarations, the `.c` holds definitions. ([Kieras guidelines][k])

Worked example in this repo: the headless repro harnesses
(`port/port_repro_*.c`) used to be declared with ~13 inline `extern`s inside
`port_bios.c`'s per-frame loop. They now live in `port/port_repro.h`, included
by both `port_bios.c` (caller) and every `port_repro_*.c` (definer), so the
compiler checks each definition against the shared declaration. Likewise
`gRomData` / `gRomSize` are owned by `port_rom.h` / `port_gba_mem.h` and
included, not re-`extern`ed.

**Sanctioned exception:** the C++ TUs that wrap engine globals in a small
`extern "C" { … }` block (`port_asset_loader.cpp`, `port_bugreport.cpp`,
`port_icon.cpp`). The C engine headers aren't `extern "C"`-guarded, so pulling
them into C++ can shift symbol linkage on the MSVC matrix target; and
`port_icon.cpp` can't include the project headers at all (the C decomp uses
`this` as a parameter name). Keep these blocks small and local.

## Metrics: smells, not gates

- **Maintainability Index** is dominated by raw length — splitting a file for
  clarity can *lower* its MI even though the code got better. Don't optimize
  the number. ([MS Learn][mi], [Sourcery critique][sc])
- **Cyclomatic complexity** is itself LOC-correlated; treat it as "where to
  look," then judge by reading. The generated TUs (`data_const_stubs.c`,
  `port_rom_tables.c`, `port_asset_index.c`, `generated_sounds_embed.cpp`)
  dominate line counts but carry near-zero cognitive load — ignore them.
- **Code churn** (from `git log`) is a better hot-file finder: high-churn
  *hand-written* files are where bugs cluster. Point first-time contributors
  at low-churn modules. ([churn][ch])

The hand-written file with real god-object risk is `port_imgui_menu.cpp`
(~3.5k lines). Prefer the same-TU `#include "*.inc"` split pattern already used
for `port_imgui_display_tab.inc` and `port_asset_loader_mods.inc`.

## Decompiled / legacy code: pin behavior with tests

A decomp is legacy code in Feathers' sense — "code without tests." The fix is
tests, not rewrites. ([WEWLC summary][wl])

- Write **characterization tests** that pin *observed* behavior, not a spec.
  `scripts/smoke_test.py` is the project's harness: it drives the real game
  loop and asserts on logged behavior (boot reaches `AgbMain`, the repro gate
  stays dormant, the mod loader is first-wins). Grow it when you fix a bug that
  has a deterministic, headless-observable signature.
- **Pin at the port seams**, not deep in the decomp. The `sub_*` / `gUnk_*`
  opacity is upstream's to fix; the leverage here is wrapping behavior at the
  PC boundary (mod-load priority, save/load round-trip, each `TMC_REPRO_*`
  harness) and asserting *that*.
- **The GBA globals (`gMain`, `gSave`, `gRoomControls`, …) are a hard
  constraint**, not a refactor target — behavioral fidelity requires them.
  When the port adds behavior, do it behind a seam (a port-layer function like
  `port_runtime_config.*` or `gba_MemPtr`), don't scatter new global state.

## Prioritized backlog

1. ~~Retire inline-`extern` debt (repro harnesses → `port_repro.h`; dedup
   `gRomData`/`gRomSize`).~~ **Done.**
2. Split `port_imgui_menu.cpp` by concern using the `.inc` pattern.
3. Grow `scripts/smoke_test.py` with more characterization tests as bugs are
   fixed (mod-load priority already covered).
4. Optionally make `port_gba_mem.h` / `port_rom.h` `extern "C"`-safe so the C++
   TUs can drop their hand-declared global blocks (touches core headers; verify
   the full matrix).

## Sources

[g]: https://google.github.io/styleguide/cppguide.html
[cs]: https://www.oreilly.com/library/view/c-coding-standards/0321113586/ch23.html
[k]: https://public.websites.umich.edu/~eecs381/handouts/CppHeaderFileGuidelines.pdf
[mi]: https://learn.microsoft.com/en-us/visualstudio/code-quality/code-metrics-maintainability-index-range-and-meaning
[sc]: https://www.sourcery.ai/blog/maintainability-index
[ch]: https://www.metridev.com/en/metrics/code-churn-metrics-best-practices/
[wl]: https://understandlegacycode.com/blog/key-points-of-working-effectively-with-legacy-code/

- Google C++ Style Guide — header rules, no transitive includes.
- Sutter & Alexandrescu, *C++ Coding Standards*, ch. 23 — minimize dependencies.
- Kieras, *C++ Header File Guidelines* — module = `.h`/`.c` with one job.
- MS Learn / Sourcery — Maintainability Index and its length bias.
- metridev — code churn as a defect-hotspot signal.
- Feathers, *Working Effectively with Legacy Code* — characterization tests, seams.
