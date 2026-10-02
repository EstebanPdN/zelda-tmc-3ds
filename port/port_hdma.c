#include "port_hdma.h"

#include <string.h>

#define HDMA_CHANNELS 4

#define DMA_CNT_DEST_FIXED 0x0040
#define DMA_CNT_DEST_RELOAD 0x0060
#define DMA_CNT_DEST_MASK 0x0060
#define DMA_CNT_SRC_FIXED 0x0100
#define DMA_CNT_32BIT 0x0400

/* Three GBA destination-increment modes per scanline:
 *   FIXED:  no increment during transfer, no reload between transfers
 *   INC:    increment during transfer, no reload between transfers
 *   RELOAD: increment during transfer, *do* reload between transfers
 * The previous implementation conflated FIXED and RELOAD, which made the
 * 8-u16 affine-matrix HBlank-DMA used by the rolling barrel and similar
 * scenes write all 8 values to BG2PA, leaving BG2PB..Y_H untouched. */
typedef enum {
    DEST_INC = 0,
    DEST_FIXED,
    DEST_RELOAD,
} HdmaDestMode;

typedef struct {
    int active;
    const uint8_t* src_orig;
    const uint8_t* src;
    uint8_t* dest_orig;
    uint8_t* dest;
    uint16_t count; // units per HBlank transfer
    uint8_t unit;   // 2 or 4 bytes
    uint8_t src_fixed;
    uint8_t dest_mode; // HdmaDestMode
} HdmaChannel;

static HdmaChannel s_channels[HDMA_CHANNELS];

void port_hdma_register(int channel, const void* src, void* dest, uint16_t cnt_h, uint16_t count) {
    HdmaChannel* c;
    uint16_t dm;

    if (channel < 0 || channel >= HDMA_CHANNELS) {
        return;
    }
    c = &s_channels[channel];
    c->active = 1;
    c->src_orig = c->src = (const uint8_t*)src;
    c->dest_orig = c->dest = (uint8_t*)dest;
    c->count = count ? count : 1;
    c->unit = (cnt_h & DMA_CNT_32BIT) ? 4 : 2;
    c->src_fixed = (cnt_h & DMA_CNT_SRC_FIXED) ? 1 : 0;
    dm = cnt_h & DMA_CNT_DEST_MASK;
    if (dm == DMA_CNT_DEST_FIXED) {
        c->dest_mode = DEST_FIXED;
    } else if (dm == DMA_CNT_DEST_RELOAD) {
        c->dest_mode = DEST_RELOAD;
    } else {
        c->dest_mode = DEST_INC;
    }
}

void port_hdma_unregister(int channel) {
    if (channel < 0 || channel >= HDMA_CHANNELS) {
        return;
    }
    s_channels[channel].active = 0;
}

int port_hdma_has_active_channels(void) {
    int ch;

    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        if (s_channels[ch].active) {
            return 1;
        }
    }
    return 0;
}

int port_hdma_dest_overlaps(const void* lo, const void* hi) {
    int ch;

    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        const HdmaChannel* c = &s_channels[ch];
        const uint8_t* d_lo;
        const uint8_t* d_hi;
        size_t span;

        if (!c->active) {
            continue;
        }
        /* Per-line write footprint starting at dest_orig. FIXED writes one
         * unit in place; INC/RELOAD advance across `count` units. INC also
         * drifts across lines, but TMC's affine users are RELOAD/FIXED —
         * treat the first-line footprint as the footprint. */
        span = (size_t)(c->dest_mode == DEST_FIXED ? c->unit : c->unit * c->count);
        d_lo = c->dest_orig;
        d_hi = c->dest_orig + span;
        if (d_lo < (const uint8_t*)hi && d_hi > (const uint8_t*)lo) {
            return 1;
        }
    }
    return 0;
}

void port_hdma_step_line(int line) {
    int ch;

    (void)line;
    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        HdmaChannel* c = &s_channels[ch];
        uint8_t* d;
        uint16_t i;

        if (!c->active) {
            continue;
        }
        d = c->dest;
        for (i = 0; i < c->count; ++i) {
            memcpy(d, c->src, c->unit);
            if (!c->src_fixed) {
                c->src += c->unit;
            }
            /* DEST_FIXED never advances within a transfer; INC and RELOAD do. */
            if (c->dest_mode != DEST_FIXED) {
                d += c->unit;
            }
        }
        /* Between scanlines: RELOAD rewinds to dest_orig; INC keeps the
         * advanced pointer; FIXED stayed put anyway. */
        c->dest = (c->dest_mode == DEST_RELOAD) ? c->dest_orig : d;
    }
}

void port_hdma_vblank_reset(void) {
    int ch;

    /*
     * TMC re-arms its HBlank DMA via SetVBlankDMA each frame, so registers
     * are typically refreshed during VBlank. If a channel happens to outlive
     * the frame, rewind src/dest so the same per-scanline table replays.
     */
    for (ch = 0; ch < HDMA_CHANNELS; ++ch) {
        HdmaChannel* c = &s_channels[ch];
        if (!c->active) {
            continue;
        }
        c->src = c->src_orig;
        c->dest = c->dest_orig;
    }
}
