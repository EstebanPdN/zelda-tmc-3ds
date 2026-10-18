# Developer utilities

These utilities are not part of the game runtime. Run them from the repository
root so paths to source files and build outputs resolve consistently.

- `asmdiff.sh` compares a ROM address range between two GBA images.
- `calcrom.pl` reports code and data progress from a linker map.
- `format.py` checks maintained C/C++ style; `--write` applies formatting.
  `format.sh` forwards arguments without applying clang-tidy rewrites.
- `run-tests.py` builds host targets and executes them in isolated directories.
- `check.py` validates source references, current documentation links and public-file boundaries.
- `genctx.sh` produces the aggregate `ctx.c` include context.
- `progress.py` reports decompilation progress from the USA linker map.
