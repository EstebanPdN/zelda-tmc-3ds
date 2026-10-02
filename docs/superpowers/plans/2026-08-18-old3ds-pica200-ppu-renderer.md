# Old 3DS PICA200 PPU Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render every TMC top-screen PPU state used on Old 3DS through PICA200, with byte-exact hardware parity checks and automatic software fallback.

**Architecture:** Keep `virtuappu_mode1_prepare_frame()` as the CPU HDMA/register prepass. A pure-C model decodes changed GBA tiles into a persistent RGBA5551 atlas and builds bounded geometry/state batches; a 3DS-only Citro3D backend submits those batches into a native RGBA5551 texture that the existing presenter scales without a CPU framebuffer upload. The current software PPU remains the reference and fallback.

**Tech Stack:** C11, devkitARM, libctru, Citro3D/Citro2D, Picasso PICA200 vertex shader, xmake host tests, CMake 3DS build.

## Global Constraints

- Enable the renderer only when `!Platform3DS_IsNew3DS()`; New 3DS retains its current software path.
- Cover all tiled, affine, OBJ, window, mosaic, blend, forced-blank, and widescreen states used by TMC.
- Accept zero pixel differences; no tolerance threshold.
- Reject unsupported/capacity states before `C3D_FrameBegin()`, reset HDMA, and render the same frame in software.
- Disable the GPU path after a post-begin failure or diagnostic mismatch; the next frame uses software.
- Allocate all atlas, vertex, index, batch, and parity buffers during initialization; no gameplay heap growth.
- Add no dependency beyond already-linked Citro3D, Citro2D, libctru, and the devkitPro Picasso tool.
- Retain only if the physical Gust Jar item-get capture reaches at least 48.13 presented FPS from the 43.75 FPS baseline, with zero parity failures and no worse audio deadline/underrun behavior.

---

### Task 1: Pure-C Tile Cache and RGBA5551 Decode

**Files:**
- Create: `platform/3ds/source/port_ppu_gpu_3ds_model.h`
- Create: `platform/3ds/source/port_ppu_gpu_3ds_model.c`
- Create: `platform/3ds/tests/port_ppu_gpu_3ds_model_test.c`
- Modify: `xmake.lua:1317-1327`

**Interfaces:**
- Consumes: `MODE1_VRAM_SIZE`, `MODE1_PALETTE_COLORS`, and `VirtuaPPUMode1GbaMemory` from `port/ppu/include/cpu/mode1.h`.
- Produces:
  - `void PpuGpu3DS_CacheInit(PpuGpu3DSCache* cache);`
  - `void PpuGpu3DS_CacheBeginFrame(PpuGpu3DSCache* cache, const uint16_t* bgPalette, const uint16_t* objPalette, uint32_t frame);`
  - `bool PpuGpu3DS_CacheTile(PpuGpu3DSCache* cache, const uint8_t* vram, PpuGpu3DSTileKey key, uint16_t* atlas, uint16_t* outSlot);`
  - `uint8_t PpuGpu3DS_MortonIndex(unsigned x, unsigned y);`
  - `uint16_t PpuGpu3DS_PackRgba5551(uint16_t gbaColor, bool opaque);`

- [ ] **Step 1: Add a failing cache/decode host test**

Create the test with deterministic assertions for Morton order, GBA BGR555 conversion, 4bpp nibble order, 8bpp indices, transparent index zero, palette invalidation, tile-byte invalidation, frame pinning, and LRU reuse:

```c
#include "port_ppu_gpu_3ds_model.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void) {
    uint8_t vram[MODE1_VRAM_SIZE] = { 0 };
    uint16_t bg[256] = { 0 };
    uint16_t obj[256] = { 0 };
    static uint16_t atlas[PPU_GPU3DS_ATLAS_PIXELS];
    PpuGpu3DSCache cache;
    uint16_t slot0, slot1;

    CHECK(PpuGpu3DS_MortonIndex(0, 0) == 0);
    CHECK(PpuGpu3DS_MortonIndex(1, 0) == 1);
    CHECK(PpuGpu3DS_MortonIndex(0, 1) == 2);
    CHECK(PpuGpu3DS_MortonIndex(7, 7) == 63);
    CHECK(PpuGpu3DS_PackRgba5551(0x001f, true) == 0xf801);
    CHECK(PpuGpu3DS_PackRgba5551(0x03e0, true) == 0x07c1);
    CHECK(PpuGpu3DS_PackRgba5551(0x7c00, true) == 0x003f);
    CHECK(PpuGpu3DS_PackRgba5551(0x7fff, false) == 0xfffe);

    bg[1] = 0x001f;
    bg[2] = 0x03e0;
    vram[0] = 0x21;
    PpuGpu3DS_CacheInit(&cache);
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 1);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
        (PpuGpu3DSTileKey){ .vramOffset = 0, .paletteBank = 0, .bpp8 = false, .domain = PPU_GPU3DS_BG },
        atlas, &slot0));
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(0, 0)] == 0xf801);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(1, 0)] == 0x07c1);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(2, 0)] == 0x0000);

    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
        (PpuGpu3DSTileKey){ .vramOffset = 0, .paletteBank = 0, .bpp8 = false, .domain = PPU_GPU3DS_BG },
        atlas, &slot1));
    CHECK(slot1 == slot0);
    bg[1] = 0x7c00;
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 2);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
        (PpuGpu3DSTileKey){ .vramOffset = 0, .paletteBank = 0, .bpp8 = false, .domain = PPU_GPU3DS_BG },
        atlas, &slot1));
    CHECK(atlas[(size_t)slot1 * 64] == 0x003f);

    puts("port_ppu_gpu_3ds_model_test: PASS");
    return 0;
}
```

Add the target:

```lua
target("port_ppu_gpu_3ds_model_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_defines("MODE1_GBA_WIDTH=266")
    add_includedirs("platform/3ds/source", "port/ppu/include")
    add_files("platform/3ds/source/port_ppu_gpu_3ds_model.c")
    add_files("platform/3ds/tests/port_ppu_gpu_3ds_model_test.c")
target_end()
```

- [ ] **Step 2: Run the test and verify the missing interface fails**

Run:

```bash
xmake build -P . port_ppu_gpu_3ds_model_test
```

Expected: compilation fails because `port_ppu_gpu_3ds_model.h` and its symbols do not exist.

- [ ] **Step 3: Implement the bounded tile cache**

Define exact storage and keys in the header:

```c
#pragma once
#include "cpu/mode1.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PPU_GPU3DS_ATLAS_SIDE = 512,
    PPU_GPU3DS_TILE_SIDE = 8,
    PPU_GPU3DS_SLOT_COUNT = 4096,
    PPU_GPU3DS_ATLAS_PIXELS = PPU_GPU3DS_ATLAS_SIDE * PPU_GPU3DS_ATLAS_SIDE
};

typedef enum PpuGpu3DSPaletteDomain { PPU_GPU3DS_BG, PPU_GPU3DS_OBJ } PpuGpu3DSPaletteDomain;

typedef struct PpuGpu3DSTileKey {
    uint32_t vramOffset;
    uint8_t paletteBank;
    bool bpp8;
    PpuGpu3DSPaletteDomain domain;
} PpuGpu3DSTileKey;

typedef struct PpuGpu3DSCacheEntry {
    PpuGpu3DSTileKey key;
    uint8_t source[64];
    uint32_t paletteGeneration;
    uint32_t lastUseFrame;
    bool valid;
    bool pinned;
} PpuGpu3DSCacheEntry;

typedef struct PpuGpu3DSCache {
    PpuGpu3DSCacheEntry entries[PPU_GPU3DS_SLOT_COUNT];
    uint16_t bgPalette[256];
    uint16_t objPalette[256];
    uint32_t bgBankGeneration[16];
    uint32_t objBankGeneration[16];
    uint32_t bg256Generation;
    uint32_t obj256Generation;
    uint32_t frame;
} PpuGpu3DSCache;
```

Implement Morton packing and BGR555 conversion exactly:

```c
uint8_t PpuGpu3DS_MortonIndex(unsigned x, unsigned y) {
    return (uint8_t)((x & 1u) | ((y & 1u) << 1u) | ((x & 2u) << 1u) |
                     ((y & 2u) << 2u) | ((x & 4u) << 2u) | ((y & 4u) << 3u));
}

uint16_t PpuGpu3DS_PackRgba5551(uint16_t gba, bool opaque) {
    return (uint16_t)(((gba & 0x001fu) << 11u) | ((gba & 0x03e0u) << 1u) |
                      ((gba & 0x7c00u) >> 9u) | (opaque ? 1u : 0u));
}
```

`PpuGpu3DS_CacheBeginFrame()` must unpin all entries, compare each 16-color palette bank with its shadow, update the 16 bank generations, and update the 256-color generation if any bank changed. `PpuGpu3DS_CacheTile()` must reject out-of-range 32/64-byte tile reads, reuse an exact key whose bytes and generation match, otherwise select an invalid or least-recently-used unpinned slot, decode 64 texels in Morton order, pin it, and return false when every slot is pinned.

- [ ] **Step 4: Run the focused host test**

Run:

```bash
xmake build -P . port_ppu_gpu_3ds_model_test && build/pc/port_ppu_gpu_3ds_model_test
```

Expected: `port_ppu_gpu_3ds_model_test: PASS`.

- [ ] **Step 5: Commit the cache**

```bash
git add xmake.lua platform/3ds/source/port_ppu_gpu_3ds_model.c platform/3ds/source/port_ppu_gpu_3ds_model.h platform/3ds/tests/port_ppu_gpu_3ds_model_test.c
git commit -m "Add bounded Old 3DS PPU tile cache"
```

---

### Task 2: State Bands, Window Intervals, and Command Capacity

**Files:**
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.h`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.c`
- Modify: `platform/3ds/tests/port_ppu_gpu_3ds_model_test.c`

**Interfaces:**
- Consumes: Task 1 cache types.
- Produces:
  - `size_t PpuGpu3DS_BuildBands(const PpuGpu3DSFrameView* frame, PpuGpu3DSBand out[160]);`
  - `size_t PpuGpu3DS_WindowIntervals(unsigned left, unsigned right, unsigned width, PpuGpu3DSInterval out[2]);`
  - `void PpuGpu3DS_CommandInit(PpuGpu3DSCommandBuffer* cmd, PpuGpu3DSVertex* vertices, size_t vertexCapacity, uint16_t* indices, size_t indexCapacity, PpuGpu3DSBatch* batches, size_t batchCapacity);`
  - `bool PpuGpu3DS_CommandReserve(PpuGpu3DSCommandBuffer* cmd, size_t vertices, size_t indices, size_t batches);`

- [ ] **Step 1: Add failing band/window/capacity cases**

Append cases that build three line states where lines 0-1 match and line 2 changes `BG0HOFS`; assert two bands. Assert normal `[16,32)`, wrapped `[220,width)+[0,20)`, empty `[8,8)`, and capacity rejection without changed counts:

```c
uint8_t io[3][MODE1_IO_MEM_SIZE] = { 0 };
uint16_t dispcnt[3] = { MODE1_DISP_BG0_ON, MODE1_DISP_BG0_ON, MODE1_DISP_BG0_ON };
int32_t affX[3] = { 0 }, affY[3] = { 0 };
PpuGpu3DSBand bands[160];
PpuGpu3DSInterval intervals[2];
PpuGpu3DSVertex vertices[4];
uint16_t indices[6];
PpuGpu3DSBatch batchesOut[1];
PpuGpu3DSCommandBuffer command;

io[2][MODE1_IO_BG0HOFS] = 1;
PpuGpu3DSFrameView view = {
    .width = 240, .height = 3, .ioPerLine = &io[0][0], .ioUniform = false,
    .dispcntPerLine = dispcnt, .affineRefX = affX, .affineRefY = affY
};
CHECK(PpuGpu3DS_BuildBands(&view, bands) == 2);
CHECK(bands[0].firstLine == 0 && bands[0].lineCount == 2);
CHECK(bands[1].firstLine == 2 && bands[1].lineCount == 1);
CHECK(PpuGpu3DS_WindowIntervals(16, 32, 240, intervals) == 1);
CHECK(intervals[0].left == 16 && intervals[0].right == 32);
CHECK(PpuGpu3DS_WindowIntervals(220, 20, 240, intervals) == 2);
CHECK(intervals[0].left == 220 && intervals[0].right == 240);
CHECK(intervals[1].left == 0 && intervals[1].right == 20);
CHECK(PpuGpu3DS_WindowIntervals(8, 8, 240, intervals) == 0);
PpuGpu3DS_CommandInit(&command, vertices, 4, indices, 6, batchesOut, 1);
CHECK(PpuGpu3DS_CommandReserve(&command, 4, 6, 1));
CHECK(!PpuGpu3DS_CommandReserve(&command, 1, 0, 0));
CHECK(command.vertexCount == 4 && command.indexCount == 6 && command.batchCount == 1);
```

- [ ] **Step 2: Run and observe missing-type failures**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test
```

Expected: compilation fails on `PpuGpu3DSFrameView`, `PpuGpu3DSBand`, and command types.

- [ ] **Step 3: Add the final frame and command data contracts**

Use these exact public shapes so later tasks do not rename the boundary:

```c
typedef struct PpuGpu3DSFrameView {
    unsigned width, height;
    bool affine, ioUniform;
    uint16_t frameDispcnt;
    VirtuaPPUMode1GbaMemory memory;
    const uint8_t* ioPerLine;
    const uint16_t* dispcntPerLine;
    const int32_t* affineRefX;
    const int32_t* affineRefY;
    const uint16_t* wsShadow;
    int wsShadowBaseTile[4];
    int wsCols, wsShadowHalfwords;
    int wsHudRightAnchor, wsHudRightNativeX;
    int wsMsgShift, wsMsgX0, wsMsgX1, wsMsgY0, wsMsgY1;
    bool objClipEnable;
    const uint8_t* objClipMark;
    int objClipY;
} PpuGpu3DSFrameView;

typedef struct PpuGpu3DSInterval { uint16_t left, right; } PpuGpu3DSInterval;
typedef struct PpuGpu3DSBand { uint16_t firstLine, lineCount; uint8_t ioRow; } PpuGpu3DSBand;
typedef struct PpuGpu3DSVertex { float x, y, z, w, u, v; } PpuGpu3DSVertex;
typedef enum PpuGpu3DSLayer { PPU_GPU3DS_BG0, PPU_GPU3DS_BG1, PPU_GPU3DS_BG2, PPU_GPU3DS_BG3, PPU_GPU3DS_OBJ, PPU_GPU3DS_BACKDROP } PpuGpu3DSLayer;
typedef enum PpuGpu3DSEffect { PPU_GPU3DS_EFFECT_NONE, PPU_GPU3DS_EFFECT_ALPHA, PPU_GPU3DS_EFFECT_BRIGHTEN, PPU_GPU3DS_EFFECT_DARKEN } PpuGpu3DSEffect;

typedef struct PpuGpu3DSBatch {
    uint32_t firstIndex, indexCount;
    uint16_t firstLine, lineCount, scissorLeft, scissorRight;
    uint8_t layer, priority, windowControl, target2;
    uint8_t effect, eva, evb, evy, objectIndex;
    bool objWindow, semiTransparent;
} PpuGpu3DSBatch;

typedef struct PpuGpu3DSCommandBuffer {
    PpuGpu3DSVertex* vertices;
    uint16_t* indices;
    PpuGpu3DSBatch* batches;
    size_t vertexCount, vertexCapacity;
    size_t indexCount, indexCapacity;
    size_t batchCount, batchCapacity;
} PpuGpu3DSCommandBuffer;
```

`PpuGpu3DS_BuildBands()` must compare only registers consumed by rendering: DISPCNT, BGxCNT/HOFS/VOFS, WINxH/V, WININ/WINOUT, MOSAIC, BLDCNT/BLDALPHA/BLDY, affine PA/PC, and per-line affine references. With `ioUniform=true`, every band references row zero. `PpuGpu3DS_CommandReserve()` must perform overflow-safe subtraction checks before changing any count.

- [ ] **Step 4: Run the test**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test && build/pc/port_ppu_gpu_3ds_model_test
```

Expected: PASS.

- [ ] **Step 5: Commit the model boundary**

```bash
git add platform/3ds/source/port_ppu_gpu_3ds_model.h platform/3ds/source/port_ppu_gpu_3ds_model.c platform/3ds/tests/port_ppu_gpu_3ds_model_test.c
git commit -m "Define Old 3DS PPU render commands"
```

---

### Task 3: Citro3D Native Target, Atlas, Shader, and Presenter Boundary

**Files:**
- Create: `platform/3ds/source/port_ppu_gpu_3ds.h`
- Create: `platform/3ds/source/port_ppu_gpu_3ds.c`
- Create: `platform/3ds/source/ppu_gpu_3ds.v.pica`
- Modify: `platform/3ds/source/platform_gpu_3ds.h:40-49`
- Modify: `platform/3ds/source/platform_gpu_3ds.c:230-295,369-410`
- Modify: `platform/3ds/CMakeLists.txt:76-140`

**Interfaces:**
- Consumes: Task 2 model types and the existing Citro3D owner in `platform_gpu_3ds.c`.
- Produces:
  - `bool PortPpuGpu3DS_Init(void);`
  - `void PortPpuGpu3DS_Shutdown(void);`
  - `bool PortPpuGpu3DS_Preflight(const PpuGpu3DSFrameView* frame);`
  - `bool PortPpuGpu3DS_DrawPrepared(void);`
  - `C3D_Tex* PortPpuGpu3DS_OutputTexture(void);`
  - `void PortPpuGpu3DS_Disable(void);`
  - `bool PlatformGpu3DS_BeginCustomTop(void);`
  - `void PlatformGpu3DS_DrawTopTexture(C3D_Tex* texture, unsigned width);`

- [ ] **Step 1: Add the minimal passthrough PICA vertex shader**

```asm
; position is already clip-space; texcoord is normalized atlas UV
.out outpos position
.out outtc0 texcoord0
.alias inpos v0
.alias intex v1
.proc main
    mov outpos, inpos
    mov outtc0, intex
    end
.end
```

- [ ] **Step 2: Wire Picasso and embedded binary generation**

Add:

```cmake
ctr_add_shader_library(ppu_gpu_3ds_shader source/ppu_gpu_3ds.v.pica)
dkp_add_embedded_binary_library(ppu_gpu_3ds_shader_bin ppu_gpu_3ds_shader)
```

Add `source/port_ppu_gpu_3ds_model.c` and `source/port_ppu_gpu_3ds.c` to `tmc-3ds`, add both to the existing LTO source-property list, and link `ppu_gpu_3ds_shader_bin` with the existing libraries:

```cmake
target_link_libraries(tmc-3ds PRIVATE citro2d citro3d ctru m ppu_gpu_3ds_shader_bin)
```

- [ ] **Step 3: Build and verify the missing backend fails**

Run with the established toolchain environment:

```bash
DEVKITPRO=/tmp/dkp-root/opt/devkitpro DEVKITARM=/tmp/dkp-root/opt/devkitpro/devkitARM PATH=/tmp/dkp-root/opt/devkitpro/devkitARM/bin:/tmp/dkp-root/opt/devkitpro/devkitpro-tools/bin:$PATH MAKEROM=/tmp/tmc3ds-tools/makerom/makerom BANNERTOOL=/tmp/tmc3ds-tools/bannertool-1.2.3-linux/bannertool bash platform/3ds/build.sh
```

Expected: link/compile failure because the backend functions are not defined.

- [ ] **Step 4: Implement allocation and shared-frame ownership**

`PortPpuGpu3DS_Init()` must allocate exactly:

```c
enum {
    PPU_GPU3DS_MAX_VERTICES = 32768,
    PPU_GPU3DS_MAX_INDICES = 49152,
    PPU_GPU3DS_MAX_BATCHES = 4096
};
```

Allocate the cache, vertices, indices, and batches once. Create the CPU-writable atlas with `C3D_TexInit(&sAtlas, 512, 512, GPU_RGBA5551)` and pass `(uint16_t*)sAtlas.data` directly to the model, avoiding a second 512x512 staging copy. Create a VRAM 256x256 `GPU_RGBA5551` output texture and the native target with `C3D_RenderTargetCreateFromTex(&sOutputTexture, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8)`. Parse the embedded shader with `DVLB_ParseFile`, bind a vertex program, configure position as four floats and UV as two floats, set nearest filtering and clamp wrapping, and initialize the command buffer.

Refactor the presenter so `PlatformGpu3DS_BeginTop()` becomes:

```c
void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width) {
    if (!pixels || !PlatformGpu3DS_BeginCustomTop()) return;
    DrawTopImage(pixels, width);
    ++sStats.topTransfers;
}
```

`PlatformGpu3DS_BeginCustomTop()` performs only the existing ready check, `C3D_FrameBegin(0)`, failure accounting, and `sFrameActive=true`. `PlatformGpu3DS_DrawTopTexture()` builds a 240x160/widescreen `Tex3DS_SubTexture`, uses the same aspect/style/FPS-overlay calculations as `DrawTopImage()`, and does not call `C3D_SyncDisplayTransfer()`.

Guard Citro3D declarations in `platform_gpu_3ds.h` with `#ifdef __3DS__` so the host layout test still compiles.

- [ ] **Step 5: Cross-build the resource-only backend**

Run the Task 3 build command again.

Expected: `tmc-3ds-v1.2-E1.3dsx` and `.cia` are produced; the existing software path is behaviorally unchanged because no caller uses the new backend.

- [ ] **Step 6: Commit the GPU resource boundary**

```bash
git add platform/3ds/CMakeLists.txt platform/3ds/source/platform_gpu_3ds.c platform/3ds/source/platform_gpu_3ds.h platform/3ds/source/port_ppu_gpu_3ds.c platform/3ds/source/port_ppu_gpu_3ds.h platform/3ds/source/ppu_gpu_3ds.v.pica
git commit -m "Add PICA200 PPU render target"
```

---

### Task 4: Opaque Text Background Geometry

**Files:**
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.h`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.c`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds.c`
- Modify: `platform/3ds/tests/port_ppu_gpu_3ds_model_test.c`

**Interfaces:**
- Consumes: Task 1 cache and Task 2 command buffer.
- Produces: `bool PpuGpu3DS_BuildCommands(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache, uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd);` for forced blank, backdrop, and opaque text BG states without windows/effects/mosaic/OBJ/affine.

- [ ] **Step 1: Add a failing one-tile BG command test**

Build a 240x160 frame with BG0 enabled, `BG0CNT` priority 2, screen base `0x800`, char base zero, map entry tile 1/palette bank 3/hflip, and one nontransparent pixel. Assert the command builder emits backdrop first and BG0 second, priority 2, atlas slot UV reversed horizontally, and a 240x160 scissor. Also assert BLDCNT effects, windows, OBJ, affine, and mosaic return false until their tasks land.

```c
CHECK(PpuGpu3DS_BuildCommands(&view, &cache, atlas, &command));
CHECK(command.batchCount == 2);
CHECK(command.batches[0].layer == PPU_GPU3DS_BACKDROP);
CHECK(command.batches[1].layer == PPU_GPU3DS_BG0);
CHECK(command.batches[1].priority == 2);
CHECK(command.batches[1].scissorLeft == 0 && command.batches[1].scissorRight == 240);
```

- [ ] **Step 2: Run and verify the builder is missing**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test
```

Expected: compile or link failure on `PpuGpu3DS_BuildCommands`.

- [ ] **Step 3: Implement text-BG tile walking from the software oracle**

Port the address and wrap rules from `virtuappu_mode1_render_text_bg_line()` at `port/ppu/src/mode1.c:955-1129` into tile-granularity geometry:

```c
const unsigned priority = bgcnt & 3u;
const uint32_t charBase = ((bgcnt >> 2u) & 3u) * 0x4000u;
const bool bpp8 = ((bgcnt >> 7u) & 1u) != 0;
const uint32_t screenBase = ((bgcnt >> 8u) & 0x1fu) * 0x800u;
const unsigned size = (bgcnt >> 14u) & 3u;
const unsigned mapWidthTiles = (size & 1u) ? 64u : 32u;
const unsigned mapHeightTiles = (size & 2u) ? 64u : 32u;
const unsigned scrollX = Read16(io, MODE1_IO_BG0HOFS + bg * 4u) & 0x1ffu;
const unsigned scrollY = Read16(io, MODE1_IO_BG0VOFS + bg * 4u) & 0x1ffu;
```

Emit only visible map cells plus one edge tile. Compute screenblock addressing as `(blockX + blockY * (mapWidthTiles / 32)) * 0x800 + (localY * 32 + localX) * 2`. Use atlas keys `(charBase + tileIndex * (bpp8 ? 64 : 32), paletteBank, bpp8, BG)`. Flip UV endpoints from map bits 10/11. Clip quads to the band and viewport through batch scissor rather than altering source UV.

Reject the frame before mutating counts when any address exceeds VRAM or command capacity. Forced blank emits one white clear batch; backdrop uses BG palette entry zero.

- [ ] **Step 4: Submit opaque batches in Citro3D**

In `PortPpuGpu3DS_Preflight()`, reset counts/cache pins, call the builder, and flush only atlas slots changed this frame plus vertex/index ranges. In `PortPpuGpu3DS_DrawPrepared()`, clear RGBA5551/depth/stencil, draw backdrop, bind atlas, enable nearest filtering and alpha test `GPU_GREATER, 0`, then issue each BG batch with its scissor and `C3D_DrawElements(GPU_TRIANGLES, batch->indexCount, C3D_UNSIGNED_SHORT, sIndices + batch->firstIndex)`.

- [ ] **Step 5: Run host and cross-build checks**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test && build/pc/port_ppu_gpu_3ds_model_test
DEVKITPRO=/tmp/dkp-root/opt/devkitpro DEVKITARM=/tmp/dkp-root/opt/devkitpro/devkitARM PATH=/tmp/dkp-root/opt/devkitpro/devkitARM/bin:/tmp/dkp-root/opt/devkitpro/devkitpro-tools/bin:$PATH MAKEROM=/tmp/tmc3ds-tools/makerom/makerom BANNERTOOL=/tmp/tmc3ds-tools/bannertool-1.2.3-linux/bannertool bash platform/3ds/build.sh
```

Expected: host PASS and 3DS artifacts produced.

- [ ] **Step 6: Commit opaque backgrounds**

```bash
git add platform/3ds/source/port_ppu_gpu_3ds_model.c platform/3ds/source/port_ppu_gpu_3ds_model.h platform/3ds/source/port_ppu_gpu_3ds.c platform/3ds/tests/port_ppu_gpu_3ds_model_test.c
git commit -m "Render tiled PPU backgrounds on PICA200"
```

---

### Task 5: Objects, Priority, and Windows

**Files:**
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.c`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds.c`
- Modify: `platform/3ds/tests/port_ppu_gpu_3ds_model_test.c`

**Interfaces:**
- Consumes: `PpuGpu3DS_BuildCommands()` and backend submission.
- Produces: complete opaque BG/OBJ ordering, regular/affine OBJ geometry, WIN0/WIN1 intervals, and OBJ-window stencil.

- [ ] **Step 1: Add failing draw-order and window tests**

Add these test helpers, then fixtures for priority and overlapping window regions:

```c
static size_t FindBatch(const PpuGpu3DSCommandBuffer* cmd, uint8_t layer, uint8_t objectIndex) {
    for (size_t i = 0; i < cmd->batchCount; ++i)
        if (cmd->batches[i].layer == layer && cmd->batches[i].objectIndex == objectIndex) return i;
    return SIZE_MAX;
}

static size_t FindWindowBatch(const PpuGpu3DSCommandBuffer* cmd, uint16_t left, uint16_t right, uint8_t control) {
    for (size_t i = 0; i < cmd->batchCount; ++i)
        if (cmd->batches[i].scissorLeft == left && cmd->batches[i].scissorRight == right &&
            cmd->batches[i].windowControl == control) return i;
    return SIZE_MAX;
}

/* Back-to-front tie order: BG3, BG2, BG1, BG0; OBJ priority N is above BG priority N. */
CHECK(FindBatch(&command, PPU_GPU3DS_BG3, UINT8_MAX) < FindBatch(&command, PPU_GPU3DS_BG2, UINT8_MAX));
CHECK(FindBatch(&command, PPU_GPU3DS_BG2, UINT8_MAX) < FindBatch(&command, PPU_GPU3DS_BG1, UINT8_MAX));
CHECK(FindBatch(&command, PPU_GPU3DS_BG1, UINT8_MAX) < FindBatch(&command, PPU_GPU3DS_BG0, UINT8_MAX));
CHECK(FindBatch(&command, PPU_GPU3DS_BG0, UINT8_MAX) < FindBatch(&command, PPU_GPU3DS_OBJ, 1));
/* Lower OAM index wins: command for index 1 precedes index 0. */
CHECK(FindBatch(&command, PPU_GPU3DS_OBJ, 1) < FindBatch(&command, PPU_GPU3DS_OBJ, 0));
/* Fixture intervals are outside [0,10), OBJWIN [10,20), WIN1 [20,30), WIN0 [30,40). */
CHECK(FindWindowBatch(&command, 10, 20, objwinControl) != SIZE_MAX);
CHECK(FindWindowBatch(&command, 20, 30, win1Control) != SIZE_MAX);
CHECK(FindWindowBatch(&command, 30, 40, win0Control) != SIZE_MAX);
```

Cover hidden/prohibited OAM, signed x/y wrapping, 1D/2D tile mapping, 4bpp/8bpp, flip, affine matrix indices, double-size bounds, semitrans flag, and object-window mode.

- [ ] **Step 2: Run and observe the unsupported-state failures**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test && build/pc/port_ppu_gpu_3ds_model_test
```

Expected: the new OBJ/window assertions fail because the builder returns false.

- [ ] **Step 3: Implement OBJ geometry exactly from OAM**

Port dimensions, wrapping, mapping, and affine matrix lookup from `virtuappu_mode1_render_obj_line()` at `port/ppu/src/mode1.c:1132-1424`. Preserve these invariants:

```c
if (shape >= 3 || hidden) continue;
if (objY >= 160) objY -= 256;
if (objX >= (int)frame->width) objX -= 512;
tileIndex = obj1d ? baseTile + tileRow * tilesWide + tileCol
                  : baseTile + tileRow * 32 + tileCol;
if (bpp8) tileIndex &= ~1u;
```

Emit OAM indices 127 down to 0 so lower indices draw later. Split sprites into 8x8 source tiles. For affine OBJ, transform each tile quad about the sprite center using signed 8.8 PA/PB/PC/PD values and preserve double-size clipping bounds. Mark object-window batches as stencil-only and semi-transparent batches with `semiTransparent=true`.

- [ ] **Step 4: Implement window region submission**

For each band, derive wrapped WIN0/WIN1 horizontal intervals and vertical activity exactly as `mode1.c:1488-1513`. First draw object-window sprite geometry into stencil bit `0x04` with color/depth writes disabled. Submit layer batches in four precedence regions (outside, OBJWIN, WIN1, WIN0), applying each region's six-bit control mask and scissor intervals. Preserve region bits while later composition writes target-2 bit `0x08`.

- [ ] **Step 5: Run focused tests and cross-build**

Run the two Task 4 commands.

Expected: host PASS and successful 3DS artifacts.

- [ ] **Step 6: Commit OBJ/windows**

```bash
git add platform/3ds/source/port_ppu_gpu_3ds_model.c platform/3ds/source/port_ppu_gpu_3ds.c platform/3ds/tests/port_ppu_gpu_3ds_model_test.c
git commit -m "Render PPU objects and windows on PICA200"
```

---

### Task 6: Blend Effects and Mosaic

**Files:**
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.c`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds.c`
- Modify: `platform/3ds/tests/port_ppu_gpu_3ds_model_test.c`

**Interfaces:**
- Consumes: Task 5 region and layer batches.
- Produces: target-2 stencil transitions, alpha/semitrans OBJ passes, brighten/darken TEV state, and BG/OBJ mosaic geometry.

- [ ] **Step 1: Add failing effect command tests**

For each effect, assert coefficient clamping and pass structure:

```c
CHECK(alphaBatch->effect == PPU_GPU3DS_EFFECT_ALPHA);
CHECK(alphaBatch->eva == 16 && alphaBatch->evb == 16);
CHECK(alphaBatch->target2 == 1);
CHECK(opaqueComplement->target2 == 0);
CHECK(brightBatch->evy == 16);
CHECK(darkBatch->evy == 16);
CHECK(mosaicBatch->firstLine % mosaicV == 0);
```

Use a semitrans OBJ where OBJ is not target 1 but must still alpha-blend when the visible destination is target 2. Add a destination-not-target-2 case that must replace opaquely.

- [ ] **Step 2: Run and verify effect fixtures fail**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test && build/pc/port_ppu_gpu_3ds_model_test
```

Expected: at least one effect fixture fails.

- [ ] **Step 3: Build exact effect batches**

Clamp `EVA`, `EVB`, and `EVY` to 16. Every color-writing layer updates only stencil bit `0x08` to indicate whether that layer is selected by BLDCNT target 2. For a target-1 layer or semi-transparent OBJ, emit:

1. stencil `0x08 == 0x08`: hardware alpha blend with constants `EVA/16` and `EVB/16`;
2. stencil `0x08 == 0`: opaque replacement;
3. both passes replace only `0x08` according to the new layer's target-2 membership.

Brighten uses `src + (white - src) * EVY/16`; darken uses `src * (16-EVY)/16` through TEV constants. Effects are disabled when the active window control bit 5 is clear.

For mosaic, snap BG source x/y to `(coord / size) * size` and split emitted quads at block boundaries. For OBJ, snap screen x/y to the block's top-left before applying the existing sprite transform, matching `mode1.c:1265-1281`. Reject no state merely because a mosaic enable bit has effective 1x1 dimensions.

- [ ] **Step 4: Configure Citro3D blend/TEV/stencil state per batch**

Reset alpha blend, TEV stages 0-2, alpha test, stencil function/write mask, and depth/color mask at each state transition; do not rely on Citro2D's prior state. After native PPU rendering, `PlatformGpu3DS_DrawTopTexture()` calls `C2D_SceneBegin()`, which reestablishes the present pass.

- [ ] **Step 5: Run tests and cross-build**

Run the Task 4 host and cross-build commands.

Expected: PASS and artifacts.

- [ ] **Step 6: Commit effects**

```bash
git add platform/3ds/source/port_ppu_gpu_3ds_model.c platform/3ds/source/port_ppu_gpu_3ds.c platform/3ds/tests/port_ppu_gpu_3ds_model_test.c
git commit -m "Add PICA200 PPU effects and mosaic"
```

---

### Task 7: Affine Background and Widescreen Rules

**Files:**
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.c`
- Modify: `platform/3ds/tests/port_ppu_gpu_3ds_model_test.c`

**Interfaces:**
- Consumes: per-line affine references from `virtuappu_mode1_prepare_frame()` and existing widescreen globals copied into `PpuGpu3DSFrameView`.
- Produces:
  - complete affine BG2, shadow columns, right HUD anchor, centered message box, and OBJ clip commands;
  - `int32_t PpuGpu3DS_AffineSample(int32_t reference, int16_t coefficient, int screenCoordinate);`
  - `int PpuGpu3DS_RemapBgX(const PpuGpu3DSFrameView* frame, unsigned bg, unsigned line, int nativeX);`
  - `bool PpuGpu3DS_ShadowEntry(const PpuGpu3DSFrameView* frame, unsigned bg, unsigned row, unsigned column, uint16_t* entry);`

- [ ] **Step 1: Add failing affine/widescreen fixtures**

Use deterministic 8.8 matrices for identity, rotation, negative references, wrap, and overflow-off. Exercise the same helpers used by command generation:

```c
CHECK(PpuGpu3DS_AffineSample(refX[9], pa, 17) == ((refX[9] + pa * 17) >> 8));
CHECK(PpuGpu3DS_AffineSample(refY[9], pc, 17) == ((refY[9] + pc * 17) >> 8));
CHECK(PpuGpu3DS_RemapBgX(&view, 0, 40, 176) == (int)view.width - 64);
CHECK(PpuGpu3DS_RemapBgX(&view, 0, view.wsMsgY0, view.wsMsgX0) == view.wsMsgX0 + view.wsMsgShift);
uint16_t shadowEntry = 0;
CHECK(PpuGpu3DS_ShadowEntry(&view, 2, 5, 1, &shadowEntry));
CHECK(shadowEntry == view.wsShadow[(2 * 32 + 5) * view.wsCols + 1]);
view.wsShadowBaseTile[2] = -1;
CHECK(!PpuGpu3DS_ShadowEntry(&view, 2, 5, 1, &shadowEntry));
```

- [ ] **Step 2: Run and verify affine/widescreen failures**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test && build/pc/port_ppu_gpu_3ds_model_test
```

Expected: new assertions fail.

- [ ] **Step 3: Implement affine BG2 geometry**

Use `mode1.c:2551-2599` as the exact oracle:

```c
srcX = (refX[line] + (int16_t)Read16(io, 0x20) * x) >> 8;
srcY = (refY[line] + (int16_t)Read16(io, 0x24) * x) >> 8;
if (wrap) { srcX &= mapSize - 1; srcY &= mapSize - 1; }
```

Build per-line or coalesced strips only when the affine mapping is identical. Use the affine map's byte tile indices, 8bpp atlas keys, and clip overflow-off samples. Preserve pixel-center and 8.8 truncation by deriving each strip edge from integer source coordinates; if a transformed tile edge cannot represent the sampled sequence without a rounding difference, split to one-pixel-width quads rather than accepting approximation.

- [ ] **Step 4: Implement existing widescreen remaps**

For x >= 240 on 32-tile BGs, read `wsShadow[(bg * 32 + row) * wsCols + col]` relative to `wsShadowBaseTile[bg]`; without a shadow entry, leave backdrop. Move BG0 native columns 176-239 to `width-64..width-1` when right anchoring is active. On published message lines, suppress `[wsMsgX0,wsMsgX1)` at its native location and emit it at `+wsMsgShift`. Apply the existing OBJ clip mask/y rule to affected sprites.

- [ ] **Step 5: Run tests and cross-build**

Run the Task 4 host and cross-build commands.

Expected: PASS and artifacts.

- [ ] **Step 6: Commit affine/widescreen support**

```bash
git add platform/3ds/source/port_ppu_gpu_3ds_model.c platform/3ds/tests/port_ppu_gpu_3ds_model_test.c
git commit -m "Complete PICA200 affine and widescreen PPU"
```

---

### Task 8: Old 3DS Integration, Fallback, Diagnostics, and Parity Readback

**Files:**
- Modify: `platform/3ds/source/port_ppu_3ds.c:544-775`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds.h`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds.c`
- Modify: `platform/3ds/source/platform_gpu_3ds.h`
- Modify: `platform/3ds/source/platform_gpu_3ds.c`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.h`
- Modify: `platform/3ds/source/port_ppu_gpu_3ds_model.c`
- Modify: `platform/3ds/tests/port_ppu_gpu_3ds_model_test.c`
- Modify: `platform/3ds/CMakeLists.txt:103-106`

**Interfaces:**
- Consumes: complete model/backend and existing bottom-screen frame lifecycle.
- Produces:
  - Old 3DS GPU selection and same-frame preflight fallback.
  - `PortPpuGpu3DSStats` in quick dumps.
  - `void PortPpuGpu3DS_RequestParityCheck(void);`
  - `void PortPpuGpu3DS_FinishParityCheck(void);`
  - `uint16_t PpuGpu3DS_PackAbgr8888(uint32_t abgr);`
  - one-shot CPU/GPU pixel comparison on the next complete frame.

- [ ] **Step 1: Add a host-testable selection helper and failing cases**

Add a pure inline selection helper to `port_ppu_gpu_3ds_model.h` and an ABGR conversion to the model source:

```c
static inline bool PpuGpu3DS_ShouldUse(bool isNew3DS, bool initialized, bool disabled) {
    return !isNew3DS && initialized && !disabled;
}

uint16_t PpuGpu3DS_PackAbgr8888(uint32_t abgr) {
    return (uint16_t)((((abgr >> 0u) & 0xffu) >> 3u) << 11u |
                      (((abgr >> 8u) & 0xffu) >> 3u) << 6u |
                      (((abgr >> 16u) & 0xffu) >> 3u) << 1u | 1u);
}
```

Assert all eight Boolean combinations and verify only `false,true,false` returns true. Also assert `0xff0000ff -> 0xf801`, `0xff00ff00 -> 0x07c1`, and `0xffff0000 -> 0x003f`.

- [ ] **Step 2: Run the selection test**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test && build/pc/port_ppu_gpu_3ds_model_test
```

Expected before helper addition: compile failure. Expected after adding exactly the helper above: PASS.

- [ ] **Step 3: Integrate prepare/preflight/draw/fallback**

Add these fixed buffers and the complete frame-view helper beside the existing 3DS PPU state:

```c
static uint8_t sGpuIoPerLine[MODE1_GBA_HEIGHT][MODE1_IO_MEM_SIZE];
static uint16_t sGpuDispcntPerLine[MODE1_GBA_HEIGHT];
static int32_t sGpuAffRefX[MODE1_GBA_HEIGHT], sGpuAffRefY[MODE1_GBA_HEIGHT];
static uint16_t sGpuWsShadow[MODE1_GBA_BG_COUNT * MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS];
static bool sGpuPpuInitialized, sGpuPpuDisabled;

static void FillPreparedFrameView(PpuGpu3DSFrameView* view) {
    memset(view, 0, sizeof(*view));
    view->width = sTopPresentWidth;
    view->height = MODE1_GBA_HEIGHT;
    view->affine = virtuappu_registers.mode == 2;
    view->ioUniform = virtuappu_mode1_pre_line_callback == NULL;
    virtuappu_mode1_get_bound_gba_memory(&view->memory);
    view->ioPerLine = &sGpuIoPerLine[0][0];
    view->dispcntPerLine = sGpuDispcntPerLine;
    view->affineRefX = sGpuAffRefX;
    view->affineRefY = sGpuAffRefY;
    view->wsCols = MODE1_WS_SHADOW_COLS;
    view->wsHudRightAnchor = virtuappu_mode1_ws_hud_right_anchor;
    view->wsHudRightNativeX = MODE1_WS_HUD_RIGHT_NATIVE_X;
    view->wsMsgShift = virtuappu_mode1_ws_msg_shift;
    view->wsMsgX0 = virtuappu_mode1_ws_msg_x0;
    view->wsMsgX1 = virtuappu_mode1_ws_msg_x1;
    view->wsMsgY0 = virtuappu_mode1_ws_msg_y0;
    view->wsMsgY1 = virtuappu_mode1_ws_msg_y1;
    view->objClipEnable = virtuappu_mode1_obj_clip_enable;
    view->objClipMark = virtuappu_mode1_obj_clip_mark;
    view->objClipY = virtuappu_mode1_obj_clip_y;
    bool anyShadow = false;
    for (unsigned bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
        view->wsShadowBaseTile[bg] = virtuappu_mode1_ws_shadow[bg] ? virtuappu_mode1_ws_shadow_base_tile[bg] : -1;
        if (virtuappu_mode1_ws_shadow[bg]) {
            memcpy(&sGpuWsShadow[bg * MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS],
                   virtuappu_mode1_ws_shadow[bg],
                   MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS * sizeof(uint16_t));
            anyShadow = true;
        }
    }
    view->wsShadow = anyShadow ? sGpuWsShadow : NULL;
    view->wsShadowHalfwords = anyShadow ? (int)(sizeof(sGpuWsShadow) / sizeof(sGpuWsShadow[0])) : 0;
}
```
In `Port_PPU_Init()`, initialize the GPU PPU only on Old 3DS after `PlatformGpu3DS_Init()`. In `Port_PPU_PresentFrame()` replace the unconditional `virtuappu_render_frame()` with this state machine:

```c
PpuGpu3DSFrameView frameView;
bool gpuReady = false;
if (PpuGpu3DS_ShouldUse(Platform3DS_IsNew3DS(), sGpuPpuInitialized, sGpuPpuDisabled)) {
    FillPreparedFrameView(&frameView);
    virtuappu_mode1_prepare_frame(&virtuappu_registers, sGpuIoPerLine[0], sGpuDispcntPerLine,
                                  sGpuAffRefX, sGpuAffRefY, &frameView.frameDispcnt);
    gpuReady = PortPpuGpu3DS_Preflight(&frameView);
    if (!gpuReady) port_hdma_vblank_reset();
}
if (!gpuReady) {
    virtuappu_render_frame();
    PlatformGpu3DS_BeginTop(sTopUpload, sTopPresentWidth);
} else if (!PlatformGpu3DS_BeginCustomTop() || !PortPpuGpu3DS_DrawPrepared()) {
    PortPpuGpu3DS_Disable();
    sGpuPpuDisabled = true;
} else {
    PlatformGpu3DS_DrawTopTexture(PortPpuGpu3DS_OutputTexture(), sTopPresentWidth);
}
```

Do not call the software upload on the successful GPU path. Keep bottom scheduling and `PlatformGpu3DS_EndBottom()` unchanged. Shutdown the GPU PPU before `PlatformGpu3DS_Shutdown()`.

- [ ] **Step 4: Add one-shot parity readback**

When L+R+A requests a quick dump, call `PortPpuGpu3DS_RequestParityCheck()`. The requested frame runs this order so CPU and GPU see the same frame-start HDMA state and no GPU readback is inspected before `C3D_FrameEnd()` submits it:

1. run the complete software renderer first and convert its visible ABGR8888 output into a preallocated RGBA5551 reference buffer;
2. call `port_hdma_vblank_reset()`, then run `virtuappu_mode1_prepare_frame()` and GPU preflight from the same state;
3. draw the GPU frame and enqueue a native-target copy into a second preallocated linear buffer before `PlatformGpu3DS_EndBottom()` ends the Citro3D frame;
4. at the start of the following `Port_PPU_PresentFrame()`, after the intervening vblank has completed the copy, compare every visible pixel and record first x/y plus total differences;
5. set disabled before renderer selection when any difference exists; otherwise continue on GPU.

Expose `void PortPpuGpu3DS_RequestParityCheck(void);` and `void PortPpuGpu3DS_FinishParityCheck(void);`. The latter is a no-op until a submitted parity copy is ready and is the first call in `Port_PPU_PresentFrame()`. Parity frames are marked as frame-pacer discontinuities and excluded from performance averages.

Expose stats:

```c
typedef struct PortPpuGpu3DSStats {
    uint64_t attemptedFrames, renderedFrames, fallbackFrames, disabledFrames;
    uint64_t tileHits, tileDecodes, atlasFlushBytes;
    uint64_t vertices, batches, parityChecks, parityFailures, differingPixels;
    uint32_t firstDiffX, firstDiffY;
    uint64_t preflightTicks, drawTicks;
    bool initialized, enabled, disabled;
} PortPpuGpu3DSStats;
```

Write these fields into the existing quick-dump `info.txt` PPU/GPU sections.

- [ ] **Step 5: Run all focused checks and cross-build**

```bash
xmake build -P . port_ppu_gpu_3ds_model_test
xmake build -P . platform_gpu_layout_3ds_test
xmake build -P . mode1_native_fast_path_test
xmake build -P . old3ds_frame_pacer_test
build/pc/port_ppu_gpu_3ds_model_test
build/pc/platform_gpu_layout_3ds_test
build/pc/mode1_native_fast_path_test
build/pc/old3ds_frame_pacer_test
DEVKITPRO=/tmp/dkp-root/opt/devkitpro DEVKITARM=/tmp/dkp-root/opt/devkitpro/devkitARM PATH=/tmp/dkp-root/opt/devkitpro/devkitARM/bin:/tmp/dkp-root/opt/devkitpro/devkitpro-tools/bin:$PATH MAKEROM=/tmp/tmc3ds-tools/makerom/makerom BANNERTOOL=/tmp/tmc3ds-tools/bannertool-1.2.3-linux/bannertool bash platform/3ds/build.sh
```

Expected: all four host binaries print PASS and the 3DSX/CIA are produced.

- [ ] **Step 6: Commit integration**
```bash
git add platform/3ds/CMakeLists.txt platform/3ds/source/port_ppu_3ds.c platform/3ds/source/port_ppu_gpu_3ds.c platform/3ds/source/port_ppu_gpu_3ds.h platform/3ds/source/platform_gpu_3ds.c platform/3ds/source/platform_gpu_3ds.h platform/3ds/source/port_ppu_gpu_3ds_model.c platform/3ds/source/port_ppu_gpu_3ds_model.h platform/3ds/tests/port_ppu_gpu_3ds_model_test.c
git commit -m "Use PICA200 PPU renderer on Old 3DS"
```

---

### Task 9: Physical Parity, Performance Gate, and Cleanup

**Files:**
- Modify only if evidence requires a correctness/performance fix: renderer files from Tasks 1-8.
- Modify: `CHANGELOG.md` only after the physical gate passes.

**Interfaces:**
- Consumes: installable build, quick-dump parity/stats, deterministic Gust Jar item-get reproduction.
- Produces: retained renderer with hardware proof, or a clean revert of Tasks 1-8 if it misses the gate.

- [ ] **Step 1: Run final build and container checks**

```bash
DEVKITPRO=/tmp/dkp-root/opt/devkitpro DEVKITARM=/tmp/dkp-root/opt/devkitpro/devkitARM PATH=/tmp/dkp-root/opt/devkitpro/devkitARM/bin:/tmp/dkp-root/opt/devkitpro/devkitpro-tools/bin:$PATH MAKEROM=/tmp/tmc3ds-tools/makerom/makerom BANNERTOOL=/tmp/tmc3ds-tools/bannertool-1.2.3-linux/bannertool bash platform/3ds/build.sh
/tmp/dkp-root/opt/devkitpro/devkitARM/bin/arm-none-eabi-size build-3ds/game/tmc-3ds.elf
/tmp/dkp-root/opt/devkitpro/tools/bin/3dsxdump build-3ds/game/tmc-3ds-v1.2-E1.3dsx
```

Expected: successful build, finite CODE/RODATA/DATA/BSS page counts, and no unresolved symbols.

- [ ] **Step 2: SHA-verify deployment to the existing FTP test path**

Upload `build-3ds/game/tmc-3ds-v1.2-E1.3dsx` to `/3ds/tmc-old3ds-test.3dsx`, retrieve it, and compare SHA-256. Expected: local and retrieved hashes are identical before launch.

- [ ] **Step 3: Run hardware parity coverage**

For title/file-select, normal field, item-get, affine title/gameplay, OBJ-window iris, mosaic, fade, and widescreen scenes:

1. hold L+R+A once;
2. wait for `DUMP SAVED`;
3. retrieve the dump;
4. verify `parityChecks` increases and `parityFailures: 0`, `differingPixels: 0`;
5. inspect displayed top/bottom BMPs for missing layers, seams, window leaks, or scaling changes.

Any nonzero difference is a correctness failure. Fix the first reported coordinate from the captured IO/VRAM/OAM state, rerun the focused host test and cross-build, and repeat that scene before continuing.

- [ ] **Step 4: Run the deterministic performance/audio comparison**

Trigger the established Gust Jar item-get reproduction, allow at least 900 logic frames, then save a dump. Compare against the 43.75 FPS / 13.511 ms software-PPU capture. Required evidence:

- measured presented FPS >= 48.13;
- engine cadence remains near 60 ticks/s;
- parity failures remain zero;
- PPU preflight plus PICA draw time is below the removed software render cost;
- audio underruns and dropped NDSP frames do not increase;
- music and SFX remain audible without crackling.

If any gate fails after root-cause correction, revert the eight renderer commits and retain only the approved design/plan documents and prior Old 3DS optimizations.

- [ ] **Step 5: Run cleanup checks after hardware success**

Run:

```bash
xmake build -P . port_ppu_gpu_3ds_model_test
xmake build -P . platform_gpu_layout_3ds_test
xmake build -P . mode1_native_fast_path_test
xmake build -P . old3ds_frame_pacer_test
build/pc/port_ppu_gpu_3ds_model_test && build/pc/platform_gpu_layout_3ds_test && build/pc/mode1_native_fast_path_test && build/pc/old3ds_frame_pacer_test
git diff --check
```

Expected: all PASS and no whitespace errors. Remove diagnostic-only frame dumps from the worktree; keep the runtime parity-on-request path and stats because they enforce the fallback contract.

- [ ] **Step 6: Document and commit only the proven result**

Add one changelog bullet stating that Old 3DS now rasterizes the GBA PPU through PICA200 with software fallback. Commit code, tests, and changelog together:

```bash
git add CHANGELOG.md xmake.lua platform/3ds/CMakeLists.txt platform/3ds/source/platform_gpu_3ds.c platform/3ds/source/platform_gpu_3ds.h platform/3ds/source/port_ppu_3ds.c platform/3ds/source/port_ppu_gpu_3ds.c platform/3ds/source/port_ppu_gpu_3ds.h platform/3ds/source/port_ppu_gpu_3ds_model.c platform/3ds/source/port_ppu_gpu_3ds_model.h platform/3ds/source/ppu_gpu_3ds.v.pica platform/3ds/tests/port_ppu_gpu_3ds_model_test.c
git commit -m "Verify Old 3DS PICA200 PPU renderer"
```
