# Regression test ownership

Run host suites through [the development commands](../docs/DEVELOPMENT.md).
Tests use synthetic data and disposable directories; private ROMs and personal
saves are not fixtures in this repository.

| Location | Exercises |
| --- | --- |
| `tests/port/` | Engine/port save persistence, region resolution, progression, allocation, scripts, collision and tile streaming. |
| `platform/3ds/tests/` | Frame pacing, bottom-screen publication/animation, top presentation geometry, GPU command budgets and updater behavior. |
| `port/ppu/tests/` | Production software renderer, native fast paths and deterministic pixel comparisons. |

`xmake.lua` retains the existing target names. Moving a fixture does not change
its target, compile definitions, production implementation or expected result.
The standalone presentation test links the production `top_view_3ds.c`, not a
copy of its policy. Hardware-backed rendering and lifecycle still need a real
console; tests with service mocks cannot establish device acceptance.
