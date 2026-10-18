/*
 * port/port_repro_perfcap.c — performance-capture harness.
 *
 * TMC_PERFCAP=1 drives title -> file-select -> game (same robust input mash
 * as port_repro_litarea.c), optionally warps to a busy scene
 * (TMC_PERFCAP_WARP="area,room,x,y,layer"; decimal or 0x-hex), lets the room
 * settle, then dumps a COMPLETE mode1 PPU memory snapshot to
 * TMC_PERFCAP_DUMP (default /tmp/tmc_ppu_snapshot.bin) for the standalone
 * render microbench (tools/ppu_bench), plus a sanity PNG.
 *
 * Unlike the port_quicksave.c state (which omits palette RAM + OAM), this
 * captures gIoMem + gVram + gBgPltt + gObjPltt + gOamMem, so the snapshot is
 * fully renderable offline by the microbench with no engine running.
 *
 * Dump format (all little-endian, host order):
 *   magic   "PPU1"            (4 bytes)
 *   sizes   u32 x5            (io, vram, bgpal, objpal, oam — in bytes)
 *   data    concatenated region bytes in that order
 *
 * With TMC_PROFILE set the harness keeps running after the dump so the
 * in-loop phase timers (VBlankIntrWait, port_bios.c) accumulate over real
 * gameplay frames; otherwise it exits cleanly right after the dump.
 */

#include <stdlib.h>
#include "port_repro.h"
#include <stdio.h>
#include <stdint.h>

#include "main.h"         /* gMain */
#include "port_gba_mem.h" /* gIoMem, gVram, gBgPltt, gObjPltt, gOamMem */
#include "room.h"         /* gRoomControls, gRoomVars.properties[] */
#include "port_debug_actions.h"
#include "virtuappu.h" /* virtuappu_frame_buffer, virtuappu_registers (Tier B hash) */

extern int Port_CaptureBaseFramebufferPNG(const char* path); /* shim in port_bugreport.cpp */

/* Tier B end-to-end oracle: FNV-1a 64 over the LIVE composited framebuffer,
 * i.e. the frame the engine just presented THIS scene — which (unlike the
 * static tools/ppu_parity render of the memory snapshot) reflects per-scanline
 * HBlank-DMA effects, mode2 affine accumulation, and the full composite. For a
 * renderer-pure hash, capture with TMC_COLOR_CORRECTION=0 so no post-process
 * has mutated the buffer in place. Deterministic because the settle frame is
 * fixed. Matches the byte order of tools/ppu_parity's fb_checksum. */
static uint64_t perfcap_frame_hash(void) {
    int w = virtuappu_registers.frame_width ? virtuappu_registers.frame_width : 240;
    int pitch = virtuappu_registers.frame_pitch ? virtuappu_registers.frame_pitch : w;
    const int h = 160;
    uint64_t hsh = 1469598103934665603ULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t v = virtuappu_frame_buffer[(size_t)y * pitch + x];
            hsh ^= v;
            hsh *= 1099511628211ULL;
        }
    }
    return hsh;
}

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

static void perfcap_dump(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[perfcap] dump open failed: %s\n", path);
        return;
    }
    const char magic[4] = { 'P', 'P', 'U', '1' };
    const uint32_t sizes[5] = { 0x400u, 0x18000u, 0x200u, 0x200u, 0x400u };
    fwrite(magic, 1, 4, f);
    fwrite(sizes, sizeof(uint32_t), 5, f);
    fwrite(gIoMem, 1, 0x400u, f);
    fwrite(gVram, 1, 0x18000u, f);
    fwrite(gBgPltt, 1, 0x200u, f);
    fwrite(gObjPltt, 1, 0x200u, f);
    fwrite(gOamMem, 1, 0x400u, f);
    fclose(f);
    if (getenv("TMC_PAL_TRACE")) {
        extern uint16_t gPaletteBuffer[];
        fprintf(
            stderr,
            "[perfcap] gPaletteBuffer rows 0/4/15: %04x %04x %04x %04x | %04x %04x %04x %04x | %04x %04x %04x %04x\n",
            gPaletteBuffer[0], gPaletteBuffer[1], gPaletteBuffer[2], gPaletteBuffer[3], gPaletteBuffer[64],
            gPaletteBuffer[65], gPaletteBuffer[66], gPaletteBuffer[67], gPaletteBuffer[240], gPaletteBuffer[241],
            gPaletteBuffer[242], gPaletteBuffer[243]);
        fprintf(
            stderr,
            "[perfcap] gBgPltt        rows 0/4/15: %04x %04x %04x %04x | %04x %04x %04x %04x | %04x %04x %04x %04x\n",
            ((uint16_t*)gBgPltt)[0], ((uint16_t*)gBgPltt)[1], ((uint16_t*)gBgPltt)[2], ((uint16_t*)gBgPltt)[3],
            ((uint16_t*)gBgPltt)[64], ((uint16_t*)gBgPltt)[65], ((uint16_t*)gBgPltt)[66], ((uint16_t*)gBgPltt)[67],
            ((uint16_t*)gBgPltt)[240], ((uint16_t*)gBgPltt)[241], ((uint16_t*)gBgPltt)[242], ((uint16_t*)gBgPltt)[243]);
    }
    {
        const uint16_t dispcnt = (uint16_t)(gIoMem[0] | (gIoMem[1] << 8));
        fprintf(stderr, "[perfcap] dumped PPU snapshot -> %s (dispcnt=0x%04x mode=%u)\n", path, dispcnt,
                (unsigned)(dispcnt & 7u));
        /* Tier B end-to-end parity oracle (see perfcap_frame_hash). */
        fprintf(stderr, "[perfcap] frame_hash=0x%016llx\n", (unsigned long long)perfcap_frame_hash());
    }
}

void Port_ReproPerfcap_Tick(unsigned int frame) {
    static int active = -1;
    static int want_warp = 0;
    static unsigned char wArea = 0, wRoom = 0, wLayer = 1;
    static unsigned short wX = 0, wY = 0;
    static int warp_done = 0;
    static unsigned int first_t2 = 0;
    static int settle_frame = 0;
    static int dumped = 0;
    static unsigned char last_task = 255;
    static int at_frame = 0; /* TMC_PERFCAP_AT_FRAME: capture at this frame, no warp/mash */

    if (active < 0) {
        const char* env = getenv("TMC_PERFCAP");
        active = (env && *env && env[0] != '0') ? 1 : 0;
        if (active) {
            const char* af = getenv("TMC_PERFCAP_AT_FRAME");
            if (af && *af)
                at_frame = atoi(af);
            const char* w = getenv("TMC_PERFCAP_WARP");
            if (w && *w) {
                unsigned int a, r, x, y, l;
                if (sscanf(w, "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l) == 5) {
                    want_warp = 1;
                    wArea = (unsigned char)a;
                    wRoom = (unsigned char)r;
                    wX = (unsigned short)x;
                    wY = (unsigned short)y;
                    wLayer = (unsigned char)l;
                }
            }
            fprintf(stderr, "[perfcap] harness active (warp=%d)\n", want_warp);
        }
    }
    if (!active)
        return;

    /* TMC_PERFCAP_AT_FRAME=N: capture whatever is on screen at frame N with no
     * title-mash and no warp. Lets the corpus include screens the warp path
     * can't reach — e.g. the title screen, which uses a GBA affine mode (VPPU
     * mode 2) and is otherwise mashed past on the way to gameplay. */
    if (at_frame > 0) {
        if ((int)frame >= at_frame && !dumped) {
            const char* out = getenv("TMC_PERFCAP_DUMP");
            if (!out || !*out)
                out = "/tmp/tmc_ppu_snapshot.bin";
            dumped = 1;
            perfcap_dump(out);
            Port_CaptureBaseFramebufferPNG("/tmp/tmc_perfcap_scene.png");
            if (!getenv("TMC_PROFILE")) {
                fflush(stderr);
                _Exit(0);
            }
        }
        return;
    }

    if (gMain.task != last_task) {
        fprintf(stderr, "[perfcap] frame %u: task %u -> %u\n", frame, (unsigned)last_task, (unsigned)gMain.task);
        last_task = gMain.task;
    }

    /* Robust title + file-select mash (GBA KEYINPUT: 0 = pressed). */
    {
        unsigned short presses = 0;
        if (gMain.task == 0 && frame >= 30 && (frame & 0xF) < 3)
            presses |= START_BUTTON;
        if (gMain.task == 1 && (frame & 0xF) < 3)
            presses |= A_BUTTON;
        if (presses)
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
    }

    if (gMain.task != 2)
        return;
    if (!first_t2)
        first_t2 = frame;

    if (want_warp) {
        if (!warp_done) {
            if ((frame % 60 == 0) && frame > 600) {
                int rc = Port_DebugAction_Warp(wArea, wRoom, wX, wY, wLayer);
                fprintf(stderr, "[perfcap] frame %u: warp -> %d\n", frame, rc);
                if (rc == 1) {
                    warp_done = 1;
                    settle_frame = (int)frame + 300;
                }
            }
            return;
        }
    } else if (!settle_frame) {
        settle_frame = (int)first_t2 + 300;
    }

    if (settle_frame && (int)frame >= settle_frame && !dumped) {
        const char* out = getenv("TMC_PERFCAP_DUMP");
        if (!out || !*out)
            out = "/tmp/tmc_ppu_snapshot.bin";
        dumped = 1;
        perfcap_dump(out);
        Port_CaptureBaseFramebufferPNG("/tmp/tmc_perfcap_scene.png");
        fprintf(stderr, "[perfcap] room area=0x%02x room=0x%02x  prop0=%p prop1=%p prop5(statechange)=%p prop7=%p\n",
                gRoomControls.area, gRoomControls.room, gRoomVars.properties[0], gRoomVars.properties[1],
                gRoomVars.properties[5], gRoomVars.properties[7]);
        fprintf(stderr, "[perfcap] anchor &Port_DebugAction_Warp=%p\n", (void*)Port_DebugAction_Warp);
        if (!getenv("TMC_PROFILE")) {
            fflush(stderr);
            _Exit(0);
        }
    }

    if (frame == 6000) {
        fprintf(stderr, "[perfcap] done (dumped=%d)\n", dumped);
        fflush(stderr);
        exit(0);
    }
}
