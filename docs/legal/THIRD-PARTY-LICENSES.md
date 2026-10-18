# Third-Party Licenses & Notices

Project Picori's own code is licensed under the **GNU General Public License
v3.0 (GPL-3.0)** (see [`LICENSE`](../../LICENSE)). It applies to the original work
authored by this project's contributors, and the project as a whole is
distributed under the GPL-3.0.

This project also **uses, links, bundles, derives from, or invokes** the
third-party and upstream components listed below. **Each retains its own
license**, and those terms govern those components. They are
reproduced/redistributed here under their respective licenses.

> **Copyleft note.** Project Picori is licensed **GPL-3.0**, and it both links
> and derives from copyleft works:
>
> - **agbplay** (LGPL-3.0) is linked into the 3DS target; complete corresponding source and build scripts are provided.
> - The optional *Minish Cap Reborn*-parity quality-of-life features are
>   **ported from** Admentus64/The-Minish-Cap-Reborn (GPL-3.0) — see
>   [`docs/reborn-parity.md`](../reborn-parity.md) — and are likewise
>   distributed under the GPL-3.0 with attribution.

## Copyleft components (GPL / LGPL family)

| Component | Path | Upstream | License | Linkage |
|-----------|------|----------|---------|---------|
| **agbplay** (agbplay_core) | `src/third_party/agbplay_core` | https://github.com/ipatix/agbplay | **LGPL-3.0** | Linked into the 3DS target. See `src/third_party/agbplay_core/LICENSE` and `src/third_party/agbplay_core/COPYING.LESSER`; the complete source and build scripts support rebuilding with a modified library. |
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

## Retired upstream build tools

The independent matching-GBA toolchain and Android application shell are not
part of this branch's current build. Their historical source and notices remain
in the original release tags and credited upstream repositories. Native 3DS
dependencies and required source generators remain in the current source tree.

## Software PPU

The software PPU is vendored in `src/port/ppu/` as GPL-3.0-or-later, derived from
VirtuaPPU `5cf5e99` with port patches. See `src/port/ppu/README.md` for provenance.

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
implemented natively in this port. See [Native European compatibility fixes](../eu-backport.md).
The port does not redistribute its ROM patch or ROM-derived assets.

## 3DS updater and idle card

The updater and procedural antialiased Triforce mask are adapted from
[EstebanPdN/zelda-alttp-3ds](https://github.com/EstebanPdN/zelda-alttp-3ds), including
[PR #32](https://github.com/EstebanPdN/zelda-alttp-3ds/pull/32).
The Minish Cap interface decodes its own menu art from the user's ROM at runtime.

The updater links curl 8.4.0 (curl license), mbedTLS 2.28.8 (Apache-2.0), and
Jansson 2.14 (MIT), using devkitPro's 3DS patches. Their license texts, pinned
source digests and build details are in `platform/3ds/update-dependencies/`.
The Mozilla CA bundle is sourced from curl.se and retains its embedded notices.
