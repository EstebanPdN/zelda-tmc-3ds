# Palette and enemy death-effect fixes

The v1.3-E19 fixes adapt these GPL-3.0 Project Picori changes by 999sian:

- [a5ec5a6b23c9](https://github.com/999sian/tmc/commit/a5ec5a6b23c9): restore the native alias between `gUnk_02001A3C` and `gPaletteList[15]`, so releasing a temporary reservation makes that palette available again.
- [29928eec4c70](https://github.com/999sian/tmc/commit/29928eec4c70): guard the two remaining null accesses in `EnemyCreateDeathFX` when the effect cannot be allocated, preserving enemy cleanup and successful effect behavior.

The regression targets `palette_release_test` and `enemy_death_fx_test` call the production functions with controlled resource exhaustion. They cover palette reservation/release/reuse, preservation of existing allocations, failed and successful death effects, ordinary cleanup and boss allocation retries. These tests require no ROM or save data.

```sh
xmake build -y palette_release_test enemy_death_fx_test
build/pc/palette_release_test
build/pc/enemy_death_fx_test
```
