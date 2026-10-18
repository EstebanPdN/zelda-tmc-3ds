# Developer utilities

These utilities are not part of the game runtime. Run them from the repository
root so paths to source files and build outputs resolve consistently.

- `format.py` checks maintained C/C++ style; `--write` applies formatting.
  `format.sh` forwards arguments without applying clang-tidy rewrites.
- `run-tests.py` builds host targets and executes them in isolated directories.
- `check.py` validates source references, current documentation links and public-file boundaries.
