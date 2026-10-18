# Contributing to The Minish Cap 3DS

Start with [the repository map](REPOSITORY.md), [architecture](ARCHITECTURE.md)
and [development](DEVELOPMENT.md). This branch maintains the Nintendo 3DS
application and native regression tools. Upstream matching-ROM and desktop
application builds are maintained by the credited upstream projects.

## Development loop

1. Install the SDK and dependencies listed in the development guide.
2. Build with `./platform/3ds/build.sh`; no ROM is required for compilation.
3. Run `python3 scripts/dev/run-tests.py` for the host regression suite.
4. Run `python3 scripts/check-repository.py`, `python3 scripts/dev/check.py`
   and the pinned formatter check described in the development guide.
5. Describe the exercised behavior and distinguish host, cross-build and
   physical-console evidence in the pull request.

Keep public text in English. Preserve engine symbols, regional data ordering,
callback order, saves, controls and compiler options during structural changes.
Renderer changes require representative parity evidence; new performance claims
require measurements on the stated console. Keep the development boot console.

## File placement and attribution

Put device integration in `platform/3ds`, shared bridges in `port`, gameplay
changes in `engine`, regressions in `tests` or the platform test directory,
and guides/artwork in `docs`. Update every build, include, script and document
consumer when relocating a file. Keep generated-source producers and original
license notices available. Imported dependencies retain their upstream style.

Never commit ROMs, extracted game data, saves, dumps, local SDK installations,
private operating instructions or credentials. Run save-writing tests in
disposable directories. Existing tags and release packages are immutable
version records for a source organization change.

Use the issue templates for reproducible reports and feature requests. Include
the version, console model and steps. Review diagnostics before sharing them.
