# Repository map

This guide describes `zelda-tmc-3ds`. Start with the [project README](../README.md)
for downloads, supported devices, installation, controls and upstream credits.
The root keeps discoverable build entry points, project metadata and legal
notices. Implementation, resources and supporting documents are grouped below.

## Source ownership

| Location | Responsibility |
| --- | --- |
| `engine/src, engine/include` | Shared C gameplay engine and declarations used by the native port. |
| `engine/assets` | Symbolic metadata and generator inputs; no extracted game assets. |
| `port` | Native engine bridges, ROM access, saves, software PPU and audio. |
| `platform/3ds` | Device integration, updater, packaging and platform regressions. |
| `libs` | agbplay source and its original LGPL notices. |
| `config/roms` | Reference clean-ROM checksum descriptions. |
| `tests` | Host regression fixtures and the symbolic Goron script reference. |
| `tools, scripts` | Source generators, parity tools, formatting and repository checks. |
| `docs` | Current guides, attribution, artwork and dated technical references. |
| `build/USA/assets, build/EU/assets` | Tracked symbolic offset headers consumed by engine bridges. |

Source file moves must preserve include paths, generated inputs, toolchain
flags, runtime data locations and package contents. Update every consumer
together and run the affected build and regressions. A source relocation is
not a gameplay change or a new performance claim.

## Find the right task

- Installation and diagnostics: use the README's current instructions.
- Game-specific development: follow the build/test guides linked there.
- File placement: use the table above and the checked
  [layout manifest](repository-layout.json).
- Repository check: run `python3 scripts/check-repository.py` at the root.
- Release history: use the existing GitHub Releases and immutable version tags.

## Names and labels

Public documentation, new comments, commit messages, issue templates and
metadata are written in English. Keep source symbols and names that are
compatibility contracts. Preserve upstream notices and original quoted
evidence in their original form.

Repository topics describe the actual project and technology. Issue labels
classify work: `bug`, `documentation`, `enhancement`, `question`, `build`,
`performance`, `graphics`, `audio` and `compatibility`. Existing specialized
labels remain available. Version tags identify released source; do not move
them to an organizational commit or replace published binaries for a file move.

## Keep the source tree reproducible

Keep generated builds, SDK installations, caches, game inputs, saves and private
diagnostics outside tracked source. Generated source and redistributable
resources that are required by the build remain tracked with their producer
and license context. Imported dependencies keep their own provenance and
formatting. Private operating instructions stay outside this repository.

The repository check covers current entry-point documentation and declared
file placement. Historical investigations retain their version scope and do
not establish current hardware performance or compatibility.

## Supported source scope

This branch maintains the Nintendo 3DS application and the host tools needed to
build, generate data for, or verify it. Independent Android, desktop and other
console application shells have been retired where they were separate from the
3DS dependency graph. Required shared engines and upstream dependencies retain
their source and license context. A platform name in a shared dependency is not
by itself evidence that the file is unused.

The cleanup preserves game algorithms, save formats, controls, display policies
and device lifecycle behavior. Existing release tags and downloadable packages
remain version records. Source organization does not establish new hardware
performance or compatibility guarantees.
