# Development and verification

The Nintendo 3DS build starts at `platform/3ds/build.sh`. Host regression targets are declared in `xmake.lua`; independent desktop and
matching-GBA application builds have been retired from this branch. Read [architecture](ARCHITECTURE.md) to locate
owners before changing code.

## Build the 3DS packages

Install devkitPro/devkitARM, libctru, Citro2D, Citro3D, CMake and the updater
libraries described in [the platform guide](../platform/3ds/README.md).
`makerom` and `bannertool` are required for CIA packaging. Then run:

```sh
./platform/3ds/build.sh
```

The command produces a 3DSX and, when the packaging tools are available, a CIA
under `build-3ds/game/`. A message that CIA packaging was skipped is not a
successful CIA build. `platform/3ds/version.txt` controls versioned package
names and metadata. Generated build directories remain ignored.

No ROM is needed to compile the port. Running the game requires the user's own
compatible USA or European ROM; see [installation](../README.md#installation).
Do not regenerate ROM-derived data just to format or reorganize the source.

## Formatting

Use **clang-format 20.1.8**. For example, install it in an isolated environment:

```sh
python3 -m venv /tmp/tmc-format
/tmp/tmc-format/bin/pip install clang-format==20.1.8
python3 scripts/dev/format.py --clang-format /tmp/tmc-format/bin/clang-format
python3 scripts/dev/format.py --write --clang-format /tmp/tmc-format/bin/clang-format
```

The default is a read-only check; `--write` applies formatting. `format.sh`
forwards to the same command. The scope includes maintained C, C++, headers and
include fragments in `engine`, `port`, `platform` and `tests`. The script
lists explicit generated-file exclusions; imported SDL headers and `libs/`
retain upstream style. Formatting never invokes clang-tidy fixes. Keep semantic
refactors and formatter changes separately reviewable. The repository style
uses four spaces, attached braces, a 120-column target and unchanged include
order. Do not change literals, arithmetic, casts, packing, compiler flags or
function signatures merely to improve a formatting metric.

## Host regressions

Install xmake and a native C/C++ compiler. xmake resolves the SDL3 dependency used by selected host fixtures. Run:

```sh
python3 scripts/dev/run-tests.py --output /tmp/tmc-regressions
python3 scripts/dev/run-tests.py save_migration_integration_test widescreen_stream_test
```

The default runs every explicitly named `*_test` target plus the palette and
enemy-effect targets declared by the xmake table. It builds in the repository,
then executes each binary in its own temporary working directory. Never run a
save-writing fixture beside personal saves. Inspect JSON results and logs;
any build/run failure makes the command fail. Benchmarks, live-network updater
checks and full-game/ROM fixtures are deliberately separate commands.

The SDK-free presentation policy also supports a direct sanitized check:

```sh
cc -std=c11 -fsanitize=address,undefined -Iplatform/3ds/source \
  platform/3ds/source/top_view_3ds.c platform/3ds/tests/top_view_3ds_test.c \
  -o /tmp/tmc-top-view-test
/tmp/tmc-top-view-test
```

The matrix covers every aspect/filter combination with native, room-capped and
maximum-width inputs, including the old 400px request clamped to normal Wide.
For updater tests, use `platform/3ds/tests/run-update-tests.py --help`; that
harness requires a native Jansson installation and libcurl.

## Repository checks

```sh
python3 scripts/dev/check.py
python3 scripts/dev/format.py
git diff --check
```

The repository checker validates current public documentation links, test-source
references and private/runtime file exclusions. CI runs these static/style
checks alongside the existing 3DS build. See [test ownership](../tests/README.md)
for the suite boundaries.

## Evidence and review

Review source relocation separately from logic changes. Update build manifests,
header consumers, tests and documentation together. Preserve all game globals,
regional tables, callback order, saves, UI coordinates, assets and compiler
options unless the requested behavior change specifically requires otherwise.

Host tests establish the exercised native functions and policies. A successful
cross-build establishes ARM compilation/linking and packaging. Physical-console
screenshots, input, audio, HOME/sleep/resume and sustained timing require device
validation. A stable release label is not evidence of a new hardware benchmark.
