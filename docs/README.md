# Documentation

Project documentation is grouped here so the repository root stays focused on
the source tree, build entry points, licensing, and the public project README.

## Start here

- [Build instructions](INSTALL.md)
- [Contributing guide](CONTRIBUTING.md)
- [Decompilation onboarding](DECOMP-ONBOARDING.md)
- [Maintainability guide](MAINTAINABILITY.md)
- [Changelog](CHANGELOG.md)

## Platform and compatibility

- [Multi-region runtime](MULTI_REGION.md)
- [Regional flag audit](REGIONAL-FLAG-AUDIT.md)
- [Japanese ROM support](JP_PORT_ENABLEMENT.md)
- [GBA accuracy audit](GBA-ACCURACY-AUDIT-2026-06-15.md)
- [Assembly-to-C guide](ASM-TO-C-GUIDE.md)

## Graphics and performance

- [GPU rasterizer design](gpu-rasterizer-design.md)
- [GPU rasterizer parity notes](gpu-rasterizer-parity-notes.md)
- [Widescreen design](widescreen-phase2-design.md)
- [Widescreen status](widescreen-status-2026-05-30.md)
- [Render-thread performance](perf-render-threads-2026-06-08.md)

The remaining dated audit and design records document specific investigations.
They are retained for engineering context and regression history.

## Generated API documentation

Doxygen and m.css configuration lives in [doxygen/](doxygen/). Run the tooling
from the repository root so its relative input and output paths remain stable.
