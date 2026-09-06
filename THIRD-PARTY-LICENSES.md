# Third-Party Licenses & Notices

Project Picori's own code is licensed under the **GNU General Public License
v3.0 (GPL-3.0)** (see [`LICENSE`](LICENSE)). It applies to the original work
authored by this project's contributors, and the project as a whole is
distributed under the GPL-3.0.

This project also **uses, links, bundles, derives from, or invokes** the
third-party and upstream components listed below. **Each retains its own
license**, and those terms govern those components. They are
reproduced/redistributed here under their respective licenses.

> **Copyleft note.** Project Picori is licensed **GPL-3.0**, and it both links
> and derives from copyleft works:
>
> - **agbplay** (LGPL-3.0) is linked into `tmc_pc` in relinkable form.
> - The in-game randomizer under `port/rando/` is **derived from** the GPL-3.0
>   Minish Cap randomizer (`minishmaker/randomizer`): it shares that project's
>   `.logic` text format and reproduces its randomization behaviour. It is
>   treated as a derivative work, distributed here under the GPL-3.0 with
>   attribution.
> - The optional *Minish Cap Reborn*-parity quality-of-life features are
>   **ported from** Admentus64/The-Minish-Cap-Reborn (GPL-3.0) — see
>   [`docs/reborn-parity.md`](docs/reborn-parity.md) — and are likewise
>   distributed under the GPL-3.0 with attribution.

## Copyleft components (GPL / LGPL family)

| Component | Path | Upstream | License | Linkage |
|-----------|------|----------|---------|---------|
| **agbplay** (agbplay_core) | `libs/agbplay_core` | https://github.com/ipatix/agbplay | **LGPL-3.0** | Linked into `tmc_pc` in relinkable form. See `libs/agbplay_core/LICENSE`. |
| **Minish Cap randomizer** | `port/rando` (derived) | https://github.com/MinishMaker/randomizer | **GPL-3.0** | `port/rando/` derives from it (shared `.logic` format + randomization behaviour); distributed under GPL-3.0 with attribution. |
| **The Minish Cap Reborn** | QoL parity hooks (derived) | https://github.com/Admentus64/The-Minish-Cap-Reborn | **GPL-3.0** | QoL features ported from it (see `docs/reborn-parity.md`); distributed under GPL-3.0 with attribution. |

## Permissively-licensed components

| Component | Upstream | License |
|-----------|----------|---------|
| Dear ImGui | https://github.com/ocornut/imgui | MIT |
| {fmt} | https://github.com/fmtlib/fmt | MIT |
| nlohmann/json | https://github.com/nlohmann/json | MIT |
| GuiLite | https://github.com/idea4good/GuiLite | Apache-2.0 |
| SDL3 | https://github.com/libsdl-org/SDL | Zlib |
| zlib | https://zlib.net | Zlib |
| libpng | http://www.libpng.org/pub/png/libpng.html | libpng (PNG Reference Library) |

## Build-time decompilation toolchain (`tools/src/`)

These are **build-time only** tools (used to build the GBA ROM and process
assets); they are **not** part of the shipped `tmc_pc` binary. They originate
from the pret/zeldaret decomp toolchain and each keeps its own license, with the
license file present in-tree alongside the tool.

| Tool | Path | License | License file |
|------|------|---------|--------------|
| agb2mid | `tools/src/agb2mid` | MIT (© YamaArashi) | `tools/src/agb2mid/LICENSE` |
| aif2pcm | `tools/src/aif2pcm` | MIT (© huderlem; © Marco Trillo) | `tools/src/aif2pcm/LICENSE` |
| bin2c | `tools/src/bin2c` | MIT (© YamaArashi) | `tools/src/bin2c/LICENSE` |
| gbagfx | `tools/src/gbagfx` | MIT (© YamaArashi) | `tools/src/gbagfx/LICENSE` |
| mid2agb | `tools/src/mid2agb` | MIT (© YamaArashi) | `tools/src/mid2agb/LICENSE` |
| preproc | `tools/src/preproc` | MIT (© YamaArashi) | `tools/src/preproc/LICENSE` |
| scaninc | `tools/src/scaninc` | MIT (© YamaArashi) | `tools/src/scaninc/LICENSE` |
| **gbafix** | `tools/src/gbafix` | **GPL-3.0** | `tools/src/gbafix/COPYING` |

> `gbafix` is **GPL-3.0**, but it is a standalone build-time executable (it
> stamps the GBA ROM header); it is not linked into or distributed with the PC
> port, so it does not affect the port's licensing.

The project's own tools — `asset_processor`, `assets_extractor`, `tmc_strings`,
`util` — are first-party and covered by the project `LICENSE`.

**Build-time fetched tool dependencies** (downloaded by `tools/CMakeLists.txt`
via CMake FetchContent; not committed to this repo): nlohmann/json (MIT) and
{fmt} (MIT) as above, **CLI11** (BSD-3-Clause,
https://github.com/CLIUtils/CLI11), and cpp-best-practices/project_options
(MIT). These are pulled only when building the CMake tool set.

## Components without a published license

These submodules currently ship **without a license file** and are therefore
"all rights reserved" by default. They are authored by MatheoVignaud, a
collaborator on this port, and used by arrangement; because Project Picori is
now GPL-3.0, a GPL-compatible license must be added to these submodules for a
fully license-clean distribution. This is pending with the author — resolve it
before redistributing binaries.

> **Resolved for the software PPU:** the former `libs/ViruaPPU` submodule has
> been vendored first-party into `port/ppu/` as GPL-3.0-or-later (derived from
> VirtuaPPU `5cf5e99` plus this project's patches; see `port/ppu/README.md`).
> It is no longer an unlicensed third-party dependency.

| Component | Path | Upstream | License |
|-----------|------|----------|---------|
| VirtuaAPU | `libs/VirtuaAPU` | https://github.com/MatheoVignaud/VirtuaAPU | none published |
| tmc-Modern-Launcher | `libs/tmc-Modern-Launcher` | https://github.com/MatheoVignaud/tmc-Modern-Launcher | none published |
| tmc-Android-Experimental | `libs/tmc-Android-Experimental` | https://github.com/MatheoVignaud/tmc-Android-Experimental | none published |

## Upstream decompilation & game IP

This project builds on the **zeldaret/tmc** decompilation of *The Legend of
Zelda: The Minish Cap*. The decompilation reproduces the structure of a
copyrighted, commercially released game. All Nintendo intellectual property —
code, assets, characters, and trademarks — remains owned by **Nintendo**.
Nothing in this repository grants any rights to that intellectual property, and
a legitimately-owned ROM is required to extract assets and run the game.

---

*Generated as part of license review. If you add or remove a dependency, update
this file and `LICENSE` accordingly.*

## European gameplay backport reference

[Prof9's Minish Cap EU Backport](https://github.com/Prof9/Minish-Cap-EU-Backport)
(Unlicense) documents the Eenie, Stockwell bomb bag, and Wind Tribe roof fixes
implemented natively in this port. See [Native European compatibility fixes](docs/eu-backport.md).
The port does not redistribute its ROM patch or ROM-derived assets.
