/*
 * Port: gFigurines table population.
 *
 * On GBA the figurine viewer (figurineMenu.c) reads
 *   gFigurines[idx] -> { u8* pal, u8* gfx, int size, int zero }
 * and feeds pal/gfx to LoadPalettes / LoadResourceAsync. The original
 * gFigurines lives in `data/gfx/figurines.s` as 136 packed (4-byte ptr,
 * 4-byte ptr, 4-byte size, 4-byte 0) entries pointing into the palette and
 * gfx blobs in ROM.
 *
 * port_linked_stubs.c previously stubbed gFigurines as a 512-byte zeroed
 * buffer. Result: every figurine load saw NULL pal / NULL gfx, the menu
 * SEGV'd on the first figurine you actually owned, and the in-room sprite
 * during the drawing animation rendered garbage (#57: "Figurine Minigame
 * Crashes the Game / The sprites are also corrupted").
 *
 * Fix mirrors the gSpritePtrs / gPalette_549 pattern in port_rom.c:
 * carry a compile-time table of GBA ROM addresses + sizes and resolve
 * them to native pointers after gRomData is mapped.
 */
#include <string.h>

#include "port_config.h"
#include "port_types.h"
#include "port_rom.h"

typedef struct {
    const u8* pal;
    const u8* gfx;
    int size;
    int zero;
} Figurine;

Figurine gFigurines[137];

typedef struct {
    u32 palAddr;
    u32 gfxAddr;
    u32 size;
} FigurineRomEntry;

/* Addresses + sizes pulled from port_asset_index.c (gFigurinePalN.gbapal,
 * gFigurineGfxN.4bpp). Sizes match data/gfx/figurines.s. Identical on USA
 * and EU — figurines.s has no #ifdef.
 *
 * Stored as ROM-relative offsets (matching port_asset_index.c convention);
 * the populator OR's in 0x08000000 to form the GBA address that
 * Port_ResolveRomData expects. */
static const FigurineRomEntry kFigurineEntries[137] = {
    [1] = { 0x005B5EC0, 0x0083FB00, 0x0580 },   [2] = { 0x005B5FA0, 0x00840080, 0x05E0 },
    [3] = { 0x005B6080, 0x00840660, 0x0480 },   [4] = { 0x005B6160, 0x00840AE0, 0x04C0 },
    [5] = { 0x005B6240, 0x00840FA0, 0x0500 },   [6] = { 0x005B6320, 0x008414A0, 0x0380 },
    [7] = { 0x005B6400, 0x00841820, 0x02C0 },   [8] = { 0x005B64E0, 0x00841AE0, 0x04E0 },
    [9] = { 0x005B65C0, 0x00841FC0, 0x0320 },   [10] = { 0x005B66A0, 0x008422E0, 0x0DA0 },
    [11] = { 0x005B6780, 0x00843080, 0x0E60 },  [12] = { 0x005B6860, 0x00843EE0, 0x0EE0 },
    [13] = { 0x005B6940, 0x00844DC0, 0x0DE0 },  [14] = { 0x005B6A20, 0x00845BA0, 0x1A00 },
    [15] = { 0x005B6B00, 0x008475A0, 0x19C0 },  [16] = { 0x005B6BE0, 0x00848F60, 0x10C0 },
    [17] = { 0x005B6CC0, 0x0084A020, 0x03E0 },  [18] = { 0x005B6DA0, 0x0084A400, 0x03A0 },
    [19] = { 0x005B6E80, 0x0084A7A0, 0x0360 },  [20] = { 0x005B6F60, 0x0084AB00, 0x0B80 },
    [21] = { 0x005B7040, 0x0084B680, 0x0B00 },  [22] = { 0x005B7120, 0x0084C180, 0x0AE0 },
    [23] = { 0x005B7200, 0x0084CC60, 0x0480 },  [24] = { 0x005B72E0, 0x0084D0E0, 0x0320 },
    [25] = { 0x005B73C0, 0x0084D400, 0x0360 },  [26] = { 0x005B74A0, 0x0084D760, 0x03A0 },
    [27] = { 0x005B7580, 0x0084DB00, 0x0400 },  [28] = { 0x005B7660, 0x0084DF00, 0x07E0 },
    [29] = { 0x005B7740, 0x0084E6E0, 0x2A00 },  [30] = { 0x005B7820, 0x008510E0, 0x2A00 },
    [31] = { 0x005B7900, 0x00853AE0, 0x2A00 },  [32] = { 0x005B79E0, 0x008564E0, 0x03C0 },
    [33] = { 0x005B7AC0, 0x008568A0, 0x0380 },  [34] = { 0x005B7BA0, 0x00856C20, 0x0380 },
    [35] = { 0x005B7C80, 0x00856FA0, 0x0380 },  [36] = { 0x005B7D60, 0x00857320, 0x0560 },
    [37] = { 0x005B7E40, 0x00857880, 0x0480 },  [38] = { 0x005B7F20, 0x00857D00, 0x0480 },
    [39] = { 0x005B8000, 0x00858180, 0x0500 },  [40] = { 0x005B80E0, 0x00858680, 0x14C0 },
    [41] = { 0x005B81C0, 0x00859B40, 0x0660 },  [42] = { 0x005B82A0, 0x0085A1A0, 0x1080 },
    [43] = { 0x005B8380, 0x0085B220, 0x0800 },  [44] = { 0x005B8460, 0x0085BA20, 0x0800 },
    [45] = { 0x005B8540, 0x0085C220, 0x0500 },  [46] = { 0x005B8620, 0x0085C720, 0x0500 },
    [47] = { 0x005B8700, 0x0085CC20, 0x3080 },  [48] = { 0x005B87E0, 0x0085FCA0, 0x29E0 },
    [49] = { 0x005B88C0, 0x00862680, 0x3A00 },  [50] = { 0x005B89E0, 0x00866080, 0x29C0 },
    [51] = { 0x005B8AC0, 0x00868A40, 0x34E0 },  [52] = { 0x005B8BA0, 0x0086BF20, 0x3900 },
    [53] = { 0x005B8C80, 0x0086F820, 0x30E0 },  [54] = { 0x005B8D60, 0x00872900, 0x2C80 },
    [55] = { 0x005B8E40, 0x00875580, 0x3500 },  [56] = { 0x005B8F20, 0x00878A80, 0x2780 },
    [57] = { 0x005B9000, 0x0087B200, 0x2EA0 },  [58] = { 0x005B90E0, 0x0087E0A0, 0x3320 },
    [59] = { 0x005B91C0, 0x008813C0, 0x2AE0 },  [60] = { 0x005B92A0, 0x00883EA0, 0x1F00 },
    [61] = { 0x005B9380, 0x00885DA0, 0x4000 },  [62] = { 0x005B9460, 0x00889DA0, 0x3F80 },
    [63] = { 0x005B9540, 0x0088DD20, 0x1C40 },  [64] = { 0x005B9620, 0x0088F960, 0x1660 },
    [65] = { 0x005B9700, 0x00890FC0, 0x1C80 },  [66] = { 0x005B97E0, 0x00892C40, 0x2300 },
    [67] = { 0x005B98C0, 0x00894F40, 0x2480 },  [68] = { 0x005B99A0, 0x008973C0, 0x0440 },
    [69] = { 0x005B9A80, 0x00897800, 0x08C0 },  [70] = { 0x005B9B60, 0x008980C0, 0x0E00 },
    [71] = { 0x005B9C40, 0x00898EC0, 0x0380 },  [72] = { 0x005B9D20, 0x00899240, 0x0920 },
    [73] = { 0x005B9E00, 0x00899B60, 0x02E0 },  [74] = { 0x005B9EE0, 0x00899E40, 0x07A0 },
    [75] = { 0x005B9FC0, 0x0089A5E0, 0x0300 },  [76] = { 0x005BA0A0, 0x0089A8E0, 0x0F00 },
    [77] = { 0x005BA180, 0x0089B7E0, 0x0360 },  [78] = { 0x005BA260, 0x0089BB40, 0x0780 },
    [79] = { 0x005BA340, 0x0089C2C0, 0x0400 },  [80] = { 0x005BA420, 0x0089C6C0, 0x0380 },
    [81] = { 0x005BA500, 0x0089CA40, 0x0A00 },  [82] = { 0x005BA5E0, 0x0089D440, 0x0A00 },
    [83] = { 0x005BA6C0, 0x0089DE40, 0x0300 },  [84] = { 0x005BA7A0, 0x0089E140, 0x04C0 },
    [85] = { 0x005BA880, 0x0089E600, 0x07C0 },  [86] = { 0x005BA960, 0x0089EDC0, 0x0B20 },
    [87] = { 0x005BAA40, 0x0089F8E0, 0x07E0 },  [88] = { 0x005BAB20, 0x008A00C0, 0x0360 },
    [89] = { 0x005BAC00, 0x008A0420, 0x08C0 },  [90] = { 0x005BACE0, 0x008A0CE0, 0x07C0 },
    [91] = { 0x005BADC0, 0x008A14A0, 0x0DC0 },  [92] = { 0x005BAEA0, 0x008A2260, 0x0300 },
    [93] = { 0x005BAF80, 0x008A2560, 0x02E0 },  [94] = { 0x005BB060, 0x008A2840, 0x0500 },
    [95] = { 0x005BB140, 0x008A2D40, 0x07C0 },  [96] = { 0x005BB220, 0x008A3500, 0x0300 },
    [97] = { 0x005BB300, 0x008A3800, 0x07A0 },  [98] = { 0x005BB3E0, 0x008A3FA0, 0x0500 },
    [99] = { 0x005BB4C0, 0x008A44A0, 0x03C0 },  [100] = { 0x005BB5A0, 0x008A4860, 0x09A0 },
    [101] = { 0x005BB680, 0x008A5200, 0x0DC0 }, [102] = { 0x005BB760, 0x008A5FC0, 0x03A0 },
    [103] = { 0x005BB840, 0x008A6360, 0x0980 }, [104] = { 0x005BB920, 0x008A6CE0, 0x07C0 },
    [105] = { 0x005BBA00, 0x008A74A0, 0x0820 }, [106] = { 0x005BBAE0, 0x008A7CC0, 0x0340 },
    [107] = { 0x005BBBC0, 0x008A8000, 0x0500 }, [108] = { 0x005BBCA0, 0x008A8500, 0x0500 },
    [109] = { 0x005BBD80, 0x008A8A00, 0x07C0 }, [110] = { 0x005BBE60, 0x008A91C0, 0x0660 },
    [111] = { 0x005BBF40, 0x008A9820, 0x03C0 }, [112] = { 0x005BC020, 0x008A9BE0, 0x0460 },
    [113] = { 0x005BC100, 0x008AA040, 0x0380 }, [114] = { 0x005BC1E0, 0x008AA3C0, 0x0820 },
    [115] = { 0x005BC2C0, 0x008AABE0, 0x06A0 }, [116] = { 0x005BC3A0, 0x008AB280, 0x0980 },
    [117] = { 0x005BC480, 0x008ABC00, 0x05E0 }, [118] = { 0x005BC560, 0x008AC1E0, 0x08A0 },
    [119] = { 0x005BC640, 0x008ACA80, 0x08C0 }, [120] = { 0x005BC720, 0x008AD340, 0x05A0 },
    [121] = { 0x005BC800, 0x008AD8E0, 0x0620 }, [122] = { 0x005BC8E0, 0x008ADF00, 0x0F00 },
    [123] = { 0x005BC9C0, 0x008AEE00, 0x0640 }, [124] = { 0x005BCAA0, 0x008AF440, 0x2580 },
    [125] = { 0x005BCB80, 0x008B19C0, 0x17A0 }, [126] = { 0x005BCC60, 0x008B3160, 0x19A0 },
    [127] = { 0x005BCD40, 0x008B4B00, 0x1300 }, [128] = { 0x005BCE20, 0x008B5E00, 0x1EA0 },
    [129] = { 0x005BCF00, 0x008B7CA0, 0x1640 }, [130] = { 0x005BCFE0, 0x008B92E0, 0x17C0 },
    [131] = { 0x005BD0C0, 0x008BAAA0, 0x0E00 }, [132] = { 0x005BD1A0, 0x008BB8A0, 0x0580 },
    [133] = { 0x005BD280, 0x008BBE20, 0x0C40 }, [134] = { 0x005BD360, 0x008BCA60, 0x1100 },
    [135] = { 0x005BD440, 0x008BDB60, 0x2580 }, [136] = { 0x005BD520, 0x008C00E0, 0x18E0 },
};

static u32 ReadLe32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Resolve the packed table from the active ROM. The pointed-to palette and
 * graphics addresses move between USA and EU even though the entry layout is
 * identical. The compiled table remains only as a guarded fallback. */
void Port_PopulateFigurines(void) {
    memset(gFigurines, 0, sizeof(gFigurines));
    for (u32 i = 1; i <= 136; i++) {
        if (gRomOffsets != NULL && gRomOffsets->figurines != 0 &&
            gRomOffsets->figurines + (i + 1) * 16 <= gRomSize) {
            const u8* entry = &gRomData[gRomOffsets->figurines + i * 16];
            gFigurines[i].pal = (u8*)Port_ResolveRomData(ReadLe32(entry));
            gFigurines[i].gfx = (u8*)Port_ResolveRomData(ReadLe32(entry + 4));
            gFigurines[i].size = (int)ReadLe32(entry + 8);
        } else {
            gFigurines[i].pal = (u8*)Port_ResolveRomData(0x08000000u | kFigurineEntries[i].palAddr);
            gFigurines[i].gfx = (u8*)Port_ResolveRomData(0x08000000u | kFigurineEntries[i].gfxAddr);
            gFigurines[i].size = (int)kFigurineEntries[i].size;
        }
        gFigurines[i].zero = 0;
    }
}
