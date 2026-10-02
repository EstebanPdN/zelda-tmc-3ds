# Old 3DS Exact-Hybrid Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce original Nintendo 3DS CPU-to-GPU upload traffic without changing rendering, game timing, audio, widescreen, either screen, or New 3DS behavior.

**Architecture:** Keep VirtuaPPU as the exact GBA renderer and keep the existing 512×256 tiled PICA textures. On Old 3DS only, render into compact aligned linear upload surfaces and use the existing GSP display transfer to tile them into those textures. A pure layout contract is shared by the presenter and PPU orchestration and tested on the host.

**Tech Stack:** C11, libctru GSPGPU, Citro3D, Citro2D, xmake host tests, CMake/devkitARM 3DS build.

## Global Constraints

- Preserve game timing, audio, rendering output, widescreen, both screens, and New 3DS behavior.
- Old 3DS top upload layout is exactly 272×160 RGBA8 pixels.
- Old 3DS bottom upload layout is exactly 320×240 RGBA8 pixels per buffer.
- New 3DS upload layout remains exactly 512×256 for both top and bottom.
- PICA textures remain exactly 512×256.
- No extra conversion, copy pass, frame latency, presentation skipping, dependency, or user setting.
- Retain existing initialization failure behavior and bottom visible-generation/touch synchronization.

---

### Task 1: Define and test upload layout contract

**Files:**
- Modify: `platform/3ds/source/platform_gpu_3ds.h`
- Create: `platform/3ds/tests/platform_gpu_layout_3ds_test.c`
- Modify: `xmake.lua` beside the existing `bottom_frame_state_3ds_test` target

**Interfaces:**
- Produces: `PlatformGpu3DSUploadLayout` and `PlatformGpu3DS_GetUploadLayout(bool old3dsProfile)` for both the PPU orchestrator and GPU presenter.
- Consumes: no platform runtime APIs; the helper remains host-testable.

- [ ] **Step 1: Add the failing layout test**

```c
#include "platform_gpu_3ds.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    const PlatformGpu3DSUploadLayout oldLayout = PlatformGpu3DS_GetUploadLayout(true);
    assert(oldLayout.topPitch == 272u);
    assert(oldLayout.topRows == 160u);
    assert(oldLayout.bottomPitch == 320u);
    assert(oldLayout.bottomRows == 240u);
    assert(oldLayout.topPitch >= 266u && (oldLayout.topPitch & 7u) == 0u);
    assert(oldLayout.bottomPitch >= 320u && (oldLayout.bottomPitch & 7u) == 0u);

    const PlatformGpu3DSUploadLayout newLayout = PlatformGpu3DS_GetUploadLayout(false);
    assert(newLayout.topPitch == 512u);
    assert(newLayout.topRows == 256u);
    assert(newLayout.bottomPitch == 512u);
    assert(newLayout.bottomRows == 256u);

    puts("platform_gpu_layout_3ds_test: PASS");
    return 0;
}
```

Add this xmake target:

```lua
target("platform_gpu_layout_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/tests/platform_gpu_layout_3ds_test.c")
target_end()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `xmake build platform_gpu_layout_3ds_test`

Expected: compilation fails because `PlatformGpu3DSUploadLayout` and `PlatformGpu3DS_GetUploadLayout` do not exist.

- [ ] **Step 3: Add the minimal pure layout contract**

Add before `PlatformGpu3DSStats` in `platform_gpu_3ds.h`:

```c
typedef struct PlatformGpu3DSUploadLayout {
    unsigned topPitch;
    unsigned topRows;
    unsigned bottomPitch;
    unsigned bottomRows;
} PlatformGpu3DSUploadLayout;

static inline PlatformGpu3DSUploadLayout PlatformGpu3DS_GetUploadLayout(bool old3dsProfile) {
    return old3dsProfile ? (PlatformGpu3DSUploadLayout){ 272u, 160u, 320u, 240u }
                         : (PlatformGpu3DSUploadLayout){ 512u, 256u, 512u, 256u };
}
```

- [ ] **Step 4: Run the focused test**

Run: `xmake build platform_gpu_layout_3ds_test && build/pc/platform_gpu_layout_3ds_test`

Expected: `platform_gpu_layout_3ds_test: PASS`.

- [ ] **Step 5: Commit the contract**

```bash
git add platform/3ds/source/platform_gpu_3ds.h platform/3ds/tests/platform_gpu_layout_3ds_test.c xmake.lua
git commit -m "test: define 3DS GPU upload layouts"
```

### Task 2: Use compact Old 3DS upload surfaces

**Files:**
- Modify: `platform/3ds/source/platform_gpu_3ds.c:9-23,134-193,220-232,287-294,359-396`
- Modify: `platform/3ds/source/platform_gpu_3ds.h:7-33`
- Modify: `platform/3ds/source/port_ppu_3ds.c:34-82,257-260,390-406,531-600,603-616`

**Interfaces:**
- Consumes: `PlatformGpu3DS_GetUploadLayout(bool)` from Task 1.
- Produces: compact Old 3DS pointers and pitches consumed directly by VirtuaPPU and the bottom painter; adds layout byte counts to `PlatformGpu3DSStats`.

- [ ] **Step 1: Extend layout test with exact byte counts**

Add to the Old 3DS assertions:

```c
assert(oldLayout.topPitch * oldLayout.topRows * sizeof(uint32_t) == 174080u);
assert(oldLayout.bottomPitch * oldLayout.bottomRows * sizeof(uint32_t) == 307200u);
```

Add `#include <stdint.h>` and run:

`xmake build platform_gpu_layout_3ds_test && build/pc/platform_gpu_layout_3ds_test`

Expected: PASS; this locks the transfer sizes before platform code changes.

- [ ] **Step 2: Store the selected layout in the GPU presenter**

Add:

```c
static PlatformGpu3DSUploadLayout sUploadLayout;
```

At the start of `PlatformGpu3DS_Init` set:

```c
sUploadLayout = PlatformGpu3DS_GetUploadLayout(old3dsProfile);
const size_t topBytes = (size_t)sUploadLayout.topPitch * sUploadLayout.topRows * sizeof(uint32_t);
const size_t bottomBytes = (size_t)sUploadLayout.bottomPitch * sUploadLayout.bottomRows * sizeof(uint32_t);
```

Allocate, clear, and initially flush `topBytes` and `bottomBytes` instead of the current 512×256 byte counts. Keep the PICA texture allocations and dimensions unchanged.

- [ ] **Step 3: Transfer only compact source rectangles**

In `DrawTopImage`, replace the fixed flush and input dimensions with:

```c
const size_t topBytes = (size_t)sUploadLayout.topPitch * sUploadLayout.topRows * sizeof(uint32_t);
GSPGPU_FlushDataCache(pixels, topBytes);
C3D_SyncDisplayTransfer((u32*)pixels,
                        GX_BUFFER_DIM(sUploadLayout.topPitch, sUploadLayout.topRows),
                        (u32*)sTopTexture.data,
                        GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT),
                        TextureTransfer());
```

In `PlatformGpu3DS_EndBottom`, use the analogous `bottomPitch`, `bottomRows`, and exact `bottomBytes`. Do not change the tiled texture, subtexture, target, or draw dimensions.

- [ ] **Step 4: Render directly with matching compact pitches**

Replace `TOP_PITCH` in `port_ppu_3ds.c` with two selected-layout fields:

```c
static unsigned sTopUploadPitch;
static unsigned sBottomUploadPitch;
```

During `Port_PPU_Init`:

```c
const PlatformGpu3DSUploadLayout uploadLayout =
    PlatformGpu3DS_GetUploadLayout(!Platform3DS_IsNew3DS());
sTopUploadPitch = uploadLayout.topPitch;
sBottomUploadPitch = uploadLayout.bottomPitch;
```

Use `sTopUploadPitch` for `virtuappu_registers.frame_pitch` and `virtuappu_mode1_set_output_buffer`. Use `sBottomUploadPitch` as the `Port_SecondScreen_3DS_PaintInto` pitch. This preserves direct rendering with no intermediate copy.

- [ ] **Step 5: Record selected layout in diagnostics**

Add these fields to `PlatformGpu3DSStats`:

```c
uint32_t topUploadPitch;
uint32_t topUploadBytes;
uint32_t bottomUploadPitch;
uint32_t bottomUploadBytes;
```

Populate them at initialization. Add one dump line in `Port_PPU_3DS_WriteQuickDump`:

```c
fprintf(info, "GPU upload pitch/bytes top: %lu / %lu; bottom: %lu / %lu\n",
        (unsigned long)gpuStats.topUploadPitch, (unsigned long)gpuStats.topUploadBytes,
        (unsigned long)gpuStats.bottomUploadPitch, (unsigned long)gpuStats.bottomUploadBytes);
```

- [ ] **Step 6: Run host correctness gates**

Run:

```bash
xmake build platform_gpu_layout_3ds_test mode1_native_fast_path_test old3ds_frame_pacer_test bottom_frame_state_3ds_test
build/pc/platform_gpu_layout_3ds_test
build/pc/mode1_native_fast_path_test
build/pc/old3ds_frame_pacer_test
build/pc/bottom_frame_state_3ds_test
bash tools/ppu_parity_check.sh
```

Expected: every focused test prints PASS; parity reports both committed scenes PASS.

- [ ] **Step 7: Build the 3DS target**

Run: `platform/3ds/build.sh`

Expected: `build-3ds/game/tmc-3ds-v1.2-E1.3dsx` is produced. If `/opt/devkitpro` is unavailable, record this exact external blocker; do not claim a successful 3DS build.

- [ ] **Step 8: Inspect optimized byte counts**

Old 3DS diagnostic expectation:

```text
GPU upload pitch/bytes top: 272 / 174080; bottom: 320 / 307200
```

New 3DS diagnostic expectation:

```text
GPU upload pitch/bytes top: 512 / 524288; bottom: 512 / 524288
```

The final Old 3DS run must also show unchanged visuals and logic cadence, no increase in audio deadline misses, and improved or unchanged presentation cadence.

- [ ] **Step 9: Commit the optimized path**

```bash
git add platform/3ds/source/platform_gpu_3ds.c platform/3ds/source/platform_gpu_3ds.h platform/3ds/source/port_ppu_3ds.c platform/3ds/tests/platform_gpu_layout_3ds_test.c
git commit -m "perf: compact Old 3DS GPU uploads"
```

### Task 3: Performance gate and cleanup

**Files:**
- Modify only if evidence requires it: `docs/superpowers/specs/2026-08-17-old3ds-performance-design.md`
- Verify: `port/ppu/src/mode1.c`, `tools/ppu_bench.c`

**Interfaces:**
- Consumes: existing `ppu_bench` and Old 3DS path counters.
- Produces: no speculative renderer code.

- [ ] **Step 1: Rebuild and run the isolated PPU benchmark**

```bash
gcc -O3 -mavx2 -mfma -fopenmp -I port/ppu/include -DMODE1_GBA_WIDTH=266 \
    tools/ppu_bench.c port/ppu/src/mode1.c -o /tmp/ppu_bench -lm
/tmp/ppu_bench /tmp/tmc_old3ds_title.bin 2000 1
```

Expected checksum for the current title snapshot: `0xdea70ed1df132443`.

- [ ] **Step 2: Apply the 3% retention rule**

Do not change `mode1.c` from this workload alone: it is a title/mode-2 scene, not representative Old 3DS field gameplay. A new PPU optimization is permitted only when a field-gameplay snapshot is available and repeated medians improve its exact hot path by at least 3% without more than 1% regression elsewhere.

- [ ] **Step 3: Remove generated local artifacts from the review surface**

Confirm `baserom.gba`, `tmc.sav`, extracted `rom_data`, xmake output, and `/tmp` snapshots remain ignored and uncommitted. Do not delete user-owned ROM or save sources outside this clone.

- [ ] **Step 4: Review the final diff and commit plan metadata**

Verify the code diff contains only the upload-layout contract, compact Old 3DS buffer/transfer changes, diagnostics, and focused test. Commit this plan separately if it remains uncommitted:

```bash
git add docs/superpowers/plans/2026-08-17-old3ds-performance.md
git commit -m "docs: plan Old 3DS performance optimization"
```
