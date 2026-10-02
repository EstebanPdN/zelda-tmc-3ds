#include "common.h"

#ifdef PC_PORT
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "port_asset_loader.h"
#include "port_rom.h"
#include "port_hdma.h"
#endif
#include "area.h"
#include "asm.h"
#include "assets/map_offsets.h"
#include "port_offset_remap.h"
#include "flags.h"
#include "functions.h"
#include "game.h"
#include "global.h"
#include "item.h"
#include "kinstone.h"
#include "main.h"
#include "message.h"
#include "physics.h"
#include "room.h"
#include "save.h"
#include "screen.h"
#include "sound.h"
#include "structures.h"
#include "subtask.h"

extern u8 gUnk_03003DE0;
extern u8 gzHeap[0x1000];
#ifdef PC_PORT
/* On GBA, the linker places gUnk_02035542 at gzHeap+2 (overlapping).
 * On PC they are separate arrays, so alias it with a macro. */
#define gUnk_02035542 (gzHeap + 2)
#else
extern u8 gUnk_02035542[];
#endif
extern u32 gDungeonMap[0x800];
extern s16 gUnk_02018EE0[];

extern void (*const gFuseActions[])(void);

static void StoreKeyInput(Input* input, u32 keyInput);
void ClearOAM(void);
void ResetScreenRegs(void);
void MessageFromFusionTarget(u32);
void sub_0801E24C(s32, s32);
void sub_0801E290(u32, u32, u32);
s32 GetIndexInKinstoneBag(u32);

extern u32 sub_0807CB24(u32, u32);

typedef struct {
    u16 paletteId;
    u8 destPaletteNum;
    u8 numPalettes;
} PaletteGroup;

typedef struct {
    union {
        int raw;
        struct {
            u8 filler0[0x3];
            u8 unk3;
        } bytes;
    } unk0;
    u32 dest;
    u32 unk8;
} GfxItem;

#ifdef TMC_N64
/* GBA ROM structs are little-endian; on the big-endian N64 a native multi-byte
 * read byte-swaps them. Convert ROM-sourced u16/u32 struct fields to host order.
 * (u8 fields are byte reads and need no conversion.) */
#define ROM_U16(x) ((u16)__builtin_bswap16((u16)(x)))
#define ROM_U32(x) ((u32)__builtin_bswap32((u32)(x)))
#else
#define ROM_U16(x) (x)
#define ROM_U32(x) (x)
#endif

typedef struct {
    u8 filler[0xA00];
} struct_02017AA0;
extern u16 MAY_ALIAS gUnk_02017AA0[];

extern const PaletteGroup* gPaletteGroups[];
#ifdef PC_PORT
extern const u8* gGlobalGfxAndPalettes;
#else
extern const u8 gGlobalGfxAndPalettes[];
#endif
extern u32 gUsedPalettes;
extern u16 gPaletteBuffer[];
extern const GfxItem* gGfxGroups[];

extern const u32 gUnk_080C9460[];

void SortKinstoneBag(void);

extern void* GetRoomProperty(u32, u32, u32);

extern u8 gMapData[];
extern const DungeonLayout* const* const gDungeonLayouts[];
extern u16 gMapDataBottomSpecial[];

u32 sub_0801DF10(const DungeonLayout* lyt);
bool32 IsRoomVisited(TileEntity* tileEntity, u32 bank);
u32 sub_0801DF60(u32 a1, u8* p);
u32 sub_0801DF78(u32 a1, u32 a2);
void DrawMapPixel(u32 x, u32 y, s32 color);
void sub_0801E64C(s32 x1, s32 y1, s32 x2, s32 y2, s32 offset);

extern void* GetRoomProperty(u32, u32, u32);

enum DungeonMapObjectTypes {
    DMO_TYPE_NONE,
    DMO_TYPE_PLAYER,
    DMO_TYPE_CHEST,
    DMO_TYPE_ENTRY,
    DMO_TYPE_BOSS,
};

#ifdef PC_PORT
#define COMMON_AREA_TABLE_COUNT 0x90

static void Common_AbortMissingAssetGroup(const char* kind, u32 group) {
#ifdef TMC_N64
    /* #N64: graphics/palettes come from the embedded ROM via the GBA-original
     * path *below* the PC `assets/` block, not from a PC asset tree. Returning
     * here lets the caller (LoadGfxGroup / LoadPaletteGroup) fall through to
     * that ROM path instead of aborting. */
    (void)kind;
    (void)group;
    return;
#endif
    fprintf(stderr, "\n[FATAL] %s group %u not found.\n", kind, group);
    fprintf(stderr, "\n  The PC port reads tile graphics, palettes and music\n");
    fprintf(stderr, "  from an `assets/` tree that you produce once by running\n");
    fprintf(stderr, "  the extractor:\n\n");
    fprintf(stderr, "      ./asset_extractor          (Linux/macOS)\n");
    fprintf(stderr, "      asset_extractor.exe        (Windows)\n\n");
    fprintf(stderr, "  Run it in the same directory as `tmc_pc` (or `cd` into\n");
    fprintf(stderr, "  that directory before running tmc_pc) — it needs to find\n");
    fprintf(stderr, "  baserom.gba next to itself, then writes assets/ and\n");
    fprintf(stderr, "  assets_src/ alongside the binary.\n\n");

    /* Surface enough diagnostic info that #110-style bug reports can
     * be triaged without asking the user to re-run with a debug build.
     * Tell them: did we find the ROM at all? Is assets/ present? Is
     * the palette_groups.json in it? Print the cwd + exe-dir so the
     * user can verify the paths we probed. */
    {
        extern void Port_DumpAssetEnvironment(FILE * out, const char* kind, unsigned int group);
        Port_DumpAssetEnvironment(stderr, kind, group);
    }
    abort();
}

static bool32 Common_IsRoomHeaderPtrInRom(const RoomHeader* ptr) {
    return Port_IsRoomHeaderPtrReadable(ptr);
}

static RoomHeader* Common_GetAreaRoomHeaderSafe(u32 area, u32 room) {
    RoomHeader* table;

    if (area >= COMMON_AREA_TABLE_COUNT || room >= MAX_ROOMS) {
        return NULL;
    }

    table = gAreaRoomHeaders[area];
    if (!Common_IsRoomHeaderPtrInRom(table)) {
        Port_RefreshAreaData(area);
        table = gAreaRoomHeaders[area];
    }

    if (!Common_IsRoomHeaderPtrInRom(table)) {
        fprintf(stderr, "[AREA] common invalid room header table area=%u ptr=%p\n", area, (void*)table);
        return NULL;
    }

    return table + room;
}
#else
static RoomHeader* Common_GetAreaRoomHeaderSafe(u32 area, u32 room) {
    return gAreaRoomHeaders[area] + room;
}
#endif

// More like PrepareTileEntitesForDungeonMap or so

u32 DecToHex(u32 value) {
#ifdef PC_PORT
    /* GBA-original abuses Div (SWI 0x06) which returns the quotient in r0
     * and the remainder in r1 as a side effect, then reads `r1` between
     * Div calls. On the port, Div is plain `num / denom` and r1 stays
     * uninitialized — every digit past the first comes out as garbage,
     * which is why "You got X items!" textboxes always rendered nonsense.
     * Compute BCD digits cleanly here. */
    u32 result = 0;
    u32 remainder;
    u32 divisor;

    if (value >= 100000000u) {
        return 0x99999999u;
    }

    remainder = value;
    divisor = 10000000u;
    /* Pack 8 BCD digits, MSB first, into result. */
    for (int shift = 28; shift >= 0; shift -= 4) {
        u32 digit = remainder / divisor;
        remainder = remainder % divisor;
        result |= (digit & 0xFu) << shift;
        if (divisor != 1u) {
            divisor /= 10u;
        }
    }
    return result;
#else
    u32 result;
    FORCE_REGISTER(u32 r1, r1);

    if (value >= 100000000) {
        return 0x99999999;
    }

    result = Div(value, 10000000) * 0x10000000;
    result += Div(r1, 1000000) * 0x1000000;
    result += Div(r1, 100000) * 0x100000;
    result += Div(r1, 10000) * 0x10000;
    result += Div(r1, 1000) * 0x1000;
    result += Div(r1, 100) * 0x100;
    result += Div(r1, 10) * 0x10;
    return result + r1;
#endif
}

u32 ReadBit(void* src, u32 bit) {
    return (*((u8*)src + bit / 8) >> (bit % 8)) & 1;
}

u32 WriteBit(void* src, u32 bit) {
#ifdef PC_PORT
    u8* p = (u8*)src + bit / 8;
    u32 mask = 1 << (bit & 7);
    u32 orig = *p & mask;
    *p |= mask;
    return orig;
#else
    u32 b;
    u32 mask;
    u32 orig;

    // note that the following line relies on undefined behaviour; b is not initialised
    b += ((u32)src + bit / 8) - b;
    mask = 0x7;
    mask = 1 << (bit & mask);

    orig = *((u8*)b);
    *((u8*)b) |= mask;

    orig &= mask;
    return orig;
#endif
}

u32 ClearBit(void* src, u32 bit) {
#ifdef PC_PORT
    u8* p = (u8*)src + bit / 8;
    u32 mask = 1 << (bit & 7);
    u32 orig = *p & mask;
    *p &= ~mask;
    return orig;
#else
    u32 b;
    u32 mask;
    u32 orig;

    // note that the following line relies on undefined behaviour; b is not initialised
    b += ((u32)src + bit / 8) - b;
    mask = 0x7;
    mask = 1 << (bit & mask);

    orig = *((u8*)b);
    *((u8*)b) &= ~mask;

    orig &= mask;
    return orig;
#endif
}

void MemFill16(u32 value, void* dest, u32 size) {
#ifdef PC_PORT
    u16* d = (u16*)port_resolve_write_addr((uintptr_t)dest);
    u32 i;
    for (i = 0; i < size / 2; i++) {
        d[i] = (u16)value;
    }
#else
    DmaFill16(3, value, dest, size);
#endif
}

void MemFill32(u32 value, void* dest, u32 size) {
#ifdef PC_PORT
    u32* d = (u32*)port_resolve_write_addr((uintptr_t)dest);
    u32 i;
    for (i = 0; i < size / 4; i++) {
        d[i] = value;
    }
#else
    DmaFill32(3, value, dest, size);
#endif
}

void MemClear(void* dest, u32 size) {
#ifdef PC_PORT
    memset(port_resolve_write_addr((uintptr_t)dest), 0, size);
#else
    DmaClear32(3, dest, size);
#endif
}

void MemCopy(const void* src, void* dest, u32 size) {
#ifdef PC_PORT
    void* resolvedDest = port_resolve_write_addr((uintptr_t)dest);
    const void* resolvedSrc = port_resolve_copy_src(src, size);
    memmove(resolvedDest, resolvedSrc, size);
#else
    DmaCopy32(3, src, dest, size);
#endif
}

void ReadKeyInput(void) {
    u32 keyInput = ~REG_KEYINPUT & KEYS_MASK;
    StoreKeyInput(&gInput, keyInput);
}

static void StoreKeyInput(Input* input, u32 keyInput) {
    u32 heldKeys = input->heldKeys;
    u32 difference = keyInput & ~heldKeys;
    input->newKeys = difference;
    if (keyInput == heldKeys) {
        if (--input->menuScrollTimer == 0) {
            input->menuScrollTimer = 4;
            input->menuScrollKeys = keyInput;
        } else {
            input->menuScrollKeys = 0;
        }
    } else {
        input->menuScrollTimer = 20;
        input->menuScrollKeys = difference;
    }
    input->heldKeys = keyInput;
}

void LoadPaletteGroup(u32 group) {
#ifdef PC_PORT
    if (Port_LoadPaletteGroupFromAssets(group)) {
        return;
    }
#ifndef MULTI_REGION
    /* #110 — palette group is missing in the assets/ tree (most often
     * because the user's save header reports a non-English language
     * whose palette files weren't extracted, e.g. group 2 = Japanese
     * title intro).  Before aborting, fall back to the canonical
     * English-equivalent group:
     *   2 -> 1 (title intro)
     *   4 -> 3 (file-select)
     *   N -> N (no known fallback, abort)
     * Title-screen colours will look slightly off in the rare case
     * the user genuinely wanted a non-English palette, but the game
     * still boots.  A real Japanese / EU build with the right ROM
     * would have those palettes extracted and this path wouldn't
     * fire. */
    static u8 sWarnedFallback[256] = { 0 };
    u32 fallback = group;
    switch (group) {
        case 2:
            fallback = 1;
            break;
        case 4:
            fallback = 3;
            break;
        default:
            break;
    }
    if (fallback != group) {
        if (group < 256 && !sWarnedFallback[group]) {
            sWarnedFallback[group] = 1;
            fprintf(stderr,
                    "\n[WARN] palette group %u not in assets/, falling back to group %u "
                    "(English-equivalent).  Re-run asset_extractor if you wanted "
                    "the original language palettes.\n",
                    group, fallback);
        }
        if (Port_LoadPaletteGroupFromAssets(fallback)) {
            return;
        }
    }
#endif
    /* Fall through to ROM-backed group. Multi-region asset packs may contain
     * sparse/metadata-only groups; the selected ROM is authoritative. */
#endif
    const PaletteGroup* paletteGroup = gPaletteGroups[group];
#ifdef PC_PORT
    /* #port bug-class-2: a ROM palette group can be unresolved (NULL) on the
     * native port; the GBA always had a valid ROM pointer here. Skip rather than
     * NULL-deref (crashes on N64; harmless garbage read on GBA). */
    if (paletteGroup == NULL) {
        static u8 sSeenNullPg[256] = { 0 };
        if (group < 256 && !sSeenNullPg[group]) {
            sSeenNullPg[group] = 1;
            fprintf(stderr, "[port] LoadPaletteGroup: group %u unresolved (NULL) — skipping\n", group);
        }
        return;
    }
#endif
    while (1) {
        /* Read the 4-byte PaletteGroup via a 32-bit load (N64 cart lbu/lhu are
         * broken); ROM_U32 byte-swaps on the BE host. */
        u32 pg = ROM_U32(*(const u32*)paletteGroup);
        u32 destPaletteNum = (pg >> 16) & 0xFF;
        u32 numPalettes = (pg >> 24) & 0xF;
        if (numPalettes == 0) {
            numPalettes = 16;
        }
#ifdef PC_PORT
        if (getenv("TMC_PAL_TRACE")) {
            const u8* src = &gGlobalGfxAndPalettes[(pg & 0xFFFF) * 32];
            fprintf(stderr, "[paltrace] group=%u palId=%u dest=%u n=%u src[0..7]=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                    group, pg & 0xFFFF, destPaletteNum, numPalettes, src[0], src[1], src[2], src[3], src[4], src[5],
                    src[6], src[7]);
        }
#endif
        LoadPalettes(&gGlobalGfxAndPalettes[(pg & 0xFFFF) * 32], destPaletteNum, numPalettes);
        if (((pg >> 24) & 0x80) == 0) {
            break;
        }
        paletteGroup++;
    }
}

#ifdef PC_PORT
/* Second-screen HUD (port_second_screen_render.c) needs stable, ROM-level
 * icon colors independent of whatever's currently sitting in the live,
 * shared gPaletteBuffer — pause-menu screens reload/repurpose those banks
 * constantly for different UI contexts, so "read gPaletteBuffer bank N"
 * would show whatever the *main* screen last put there, not reliably item
 * icon colors. Mirrors LoadPaletteGroup's single-entry resolution above but
 * returns a raw pointer instead of DMA'ing into gPaletteBuffer — placed
 * here (not in port/) because gPaletteGroups/PaletteGroup are file-private
 * to this translation unit. Only resolves the first chained entry; that's
 * sufficient for a single 16-color icon palette (icon graphics don't use
 * multi-group chaining). */
const u8* Port_GetRawPaletteGroupData(u32 group, u32* outNumColors) {
    const PaletteGroup* paletteGroup = gPaletteGroups[group];
    if (paletteGroup == NULL) {
        if (outNumColors) {
            *outNumColors = 0;
        }
        return NULL;
    }
    u32 pg = ROM_U32(*(const u32*)paletteGroup);
    u32 numPalettes = (pg >> 24) & 0xF;
    if (numPalettes == 0) {
        numPalettes = 16;
    }
    if (outNumColors) {
        *outNumColors = numPalettes * 16;
    }
    return &gGlobalGfxAndPalettes[(pg & 0xFFFF) * 32];
}
#endif

void LoadPalettes(const u8* src, s32 destPaletteNum, s32 numPalettes) {
    u16* dest;
    u32 size = numPalettes * 32;
    u32 usedPalettesMask = 1 << destPaletteNum;
    while (--numPalettes > 0) {
        usedPalettesMask |= (usedPalettesMask << 1);
    }
    gUsedPalettes |= usedPalettesMask;
    dest = &gPaletteBuffer[destPaletteNum * 16];
    DmaCopy32(3, src, dest, size);
#ifdef TMC_N64
    /* DmaCopy32 byte-preserves the GBA little-endian colors; a native u16 read
     * on the big-endian N64 would byte-swap each BGR555 color. Swap in place so
     * the engine and ViruaPPU see correct colors. */
    for (u32 i = 0; i < size / 2u; i++)
        dest[i] = (u16)__builtin_bswap16(dest[i]);
#endif
}

void SetColor(u32 colorIndex, u32 color) {
    gPaletteBuffer[colorIndex] = color;
    gUsedPalettes |= 1 << (colorIndex / 16);
}

void SetFillColor(u32 color, u32 disable_layers) {
    if (disable_layers) {
        gScreen.lcd.displayControlMask = ~(DISPCNT_OBJ_ON | DISPCNT_BG_ALL_ON);
    } else {
        gScreen.lcd.displayControlMask = ~0;
    }
    SetColor(0, color);
}

void LoadGfxGroup(u32 group) {
#ifdef PC_PORT
    if (Port_LoadGfxGroupFromAssets(group)) {
        return;
    }
    /* Fall through to ROM-backed group. Multi-region asset packs may contain
     * sparse/metadata-only groups; the selected ROM is authoritative. */
#endif
    u32 terminator;
    u32 dmaCtrl;
    int gfxOffset;
    const u8* src;
    u32 dest;
    int size;
    const GfxItem* gfxItem;
#ifdef PC_PORT
    /* group is a raw byte (room data / gRoomVars.graphicsGroups, whose "none"
     * sentinel is 0xFF); gGfxGroups has 133 slots (GFX_GROUPS_COUNT_MAX,
     * port_rom.c). GBA walked adjacent ROM as a GfxItem list — on PC the
     * neighboring globals hold non-NULL pointers, defeating the NULL check
     * below and ending in a wild gfx copy. */
    if (group >= 133) {
        static u8 sSeenBadGfxGroup[256] = { 0 };
        if (!sSeenBadGfxGroup[(u8)group]) {
            sSeenBadGfxGroup[(u8)group] = 1;
            fprintf(stderr, "[port] LoadGfxGroup: group %u out of range — skipping\n", group);
        }
        return;
    }
    gfxItem = gGfxGroups[group];
    /* #port bug-class-2: ROM gfx group can be unresolved (NULL) on the native
     * port; skip rather than NULL-deref (crashes on N64). */
    if (gfxItem == NULL) {
        static u8 sSeenNullGfx[256] = { 0 };
        if (group < 256 && !sSeenNullGfx[group]) {
            sSeenNullGfx[group] = 1;
            fprintf(stderr, "[port] LoadGfxGroup: group %u unresolved (NULL) — skipping\n", group);
        }
        return;
    }
#else
    gfxItem = gGfxGroups[group];
#endif
    while (1) {
        u32 loadGfx = FALSE;
        /* Read unk0 via a 32-bit load (N64 cart byte reads are broken). */
        u32 gi = ROM_U32(gfxItem->unk0.raw);
        u32 ctrl = (gi >> 24) & 0xF;
        switch (ctrl) {
            case 0x7:
                loadGfx = TRUE;
                break;
            case 0xD:
                return;
            case 0xE:
                if (gSaveHeader->language != 0 && gSaveHeader->language != 1) {
                    loadGfx = TRUE;
                }
                break;
            case 0xF:
                if (gSaveHeader->language != 0) {
                    loadGfx = TRUE;
                }
                break;
            default:
                if (ctrl == gSaveHeader->language) {
                    loadGfx = TRUE;
                }
                break;
        }

        if (loadGfx) {
            gfxOffset = gi & 0xFFFFFF;
            src = &gGlobalGfxAndPalettes[gfxOffset];
            dest = ROM_U32(gfxItem->dest);
            size = (int)ROM_U32(gfxItem->unk8);
            dmaCtrl = 0x80000000;
            if (size < 0) {
                if (dest >= VRAM) {
                    LZ77UnCompVram(src, (void*)dest);
                } else {
                    LZ77UnCompWram(src, (void*)dest);
                }
            } else {
                DmaSet(3, src, dest, dmaCtrl | ((u32)size >> 1));
            }
        }

        terminator = (gi >> 24) & 0x80;
        gfxItem++;
        if (!terminator) {
            break;
        }
    }
}

void sub_0801D898(void* dest, void* src, u32 word, u32 size) {
    u32 v6;
    u32 i;

    if (size & 0x8000)
        v6 = 0x40;
    else
        v6 = 0x20;

    size &= (short)~0x8000;
    do {
        DmaCopy16(3, src, dest, word * 2);
        src += word * 2;
        dest += v6 * 2;
    } while (--size);
}

void* zMalloc(u32 size) {
    u16* heapStartOffset;
    u32 slotFound;
    u8* allocatedEntryStartOffset;
    u8* allocatedEntryEndOffset;
    u8* candidateSlotEndOffset;
    u8* candidateSlotStartOffset;
    u16 index1, index2;
    u16 numEntries;
    // align to 4
    size = (size + 3) & ~3;

    heapStartOffset = (u16*)(gzHeap);
    numEntries = heapStartOffset[0];
    slotFound = TRUE;

    // Check for a candidate slot at the tail-end of the heap buffer
    candidateSlotEndOffset = (u8*)heapStartOffset + sizeof(gzHeap);
    candidateSlotStartOffset = candidateSlotEndOffset - size;
    for (index2 = 0; index2 < numEntries; index2++) {

        // Check if there is overlap with already allocated slots
        allocatedEntryStartOffset = gzHeap + heapStartOffset[(index2 * 2) + 1];
        allocatedEntryEndOffset = gzHeap + heapStartOffset[(index2 * 2) + 2];

        if ((allocatedEntryStartOffset <= candidateSlotStartOffset &&
             candidateSlotStartOffset <= allocatedEntryEndOffset)) {
            slotFound = FALSE;
            break;
        }

        if ((allocatedEntryStartOffset <= candidateSlotEndOffset &&
             candidateSlotEndOffset <= allocatedEntryEndOffset)) {
            slotFound = FALSE;
            break;
        }

        if ((allocatedEntryStartOffset <= candidateSlotStartOffset &&
             candidateSlotEndOffset <= allocatedEntryEndOffset) ||
            (candidateSlotStartOffset <= allocatedEntryStartOffset &&
             allocatedEntryEndOffset <= candidateSlotEndOffset)) {
            slotFound = FALSE;
            break;
        }
    }

    if (!slotFound) {
        index1 = 0;
        // Start searching for candidate slot from the left side of the heap buffer.
        do {
            candidateSlotEndOffset = gzHeap + heapStartOffset[(index1 * 2) + 1];
            candidateSlotStartOffset = candidateSlotEndOffset - size;
            slotFound = FALSE;

            // Ensure that the candidate slot doesn't collide with heap offsets section
            if (candidateSlotStartOffset >= (u8*)(2 + (u32)heapStartOffset + (numEntries << 2) + 4)) {
                slotFound = TRUE;

                // Check if there is overlap with already allocated slots
                for (index2 = 0; index2 < numEntries; index2++) {

                    allocatedEntryStartOffset = gzHeap + heapStartOffset[(index2 * 2) + 1];
                    allocatedEntryEndOffset = gzHeap + heapStartOffset[(index2 * 2) + 2];

                    if ((allocatedEntryStartOffset <= candidateSlotStartOffset &&
                         candidateSlotStartOffset < allocatedEntryEndOffset)) {
                        slotFound = FALSE;
                        break;
                    }

                    if ((allocatedEntryStartOffset < candidateSlotEndOffset &&
                         candidateSlotEndOffset <= allocatedEntryEndOffset)) {
                        slotFound = FALSE;
                        break;
                    }

                    if ((allocatedEntryStartOffset <= candidateSlotStartOffset &&
                         candidateSlotEndOffset <= allocatedEntryEndOffset) ||
                        (candidateSlotStartOffset <= allocatedEntryStartOffset &&
                         allocatedEntryEndOffset <= candidateSlotEndOffset)) {
                        slotFound = FALSE;
                        break;
                    }
                }
                if (slotFound) {
                    break;
                }
            }
        } while ((index1 = (u16)(index1 + 1)) < numEntries);
    }
    if (!slotFound)
        return 0;

    // Register successful allocation
    *(u16*)(gUnk_02035542 + (numEntries << 2)) = candidateSlotStartOffset - (gUnk_02035542 - 2);
    *(u16*)(gUnk_02035542 + (numEntries << 2) + 2) = candidateSlotStartOffset - (gUnk_02035542 - 2) + size;
    *(u16*)(gUnk_02035542 - 2) = numEntries + 1;
    MemClear(candidateSlotStartOffset, size);
    return candidateSlotStartOffset;
}

void zFree(void* ptr) {
    u32 uVar1;
    u32 i;
    u16* entryPtr;
    s32 numEntries;
    u16* lastEntryPtr;

    uVar1 = (int)ptr - (int)gzHeap;
    if (uVar1 < 0x1000) {
        entryPtr = (u16*)gzHeap;
        numEntries = *entryPtr++;

        for (i = 0; i < numEntries; entryPtr += 2, i++) {
            if (*entryPtr == uVar1) {
#ifdef PC_PORT
                /* Avoid UB from gzHeap-2 pointer arithmetic */
                lastEntryPtr = (u16*)(gzHeap + numEntries * 4 - 2);
#else
                lastEntryPtr = &((u16*)(gzHeap - 2))[numEntries * 2];
#endif
                *entryPtr = *lastEntryPtr;
                *lastEntryPtr++ = 0;
                *(entryPtr + 1) = *lastEntryPtr;
                *lastEntryPtr = 0;
                *(u16*)(gzHeap) = numEntries - 1;
                break;
            }
        }
    }
}

void zMallocInit(void) {
    MemClear(gzHeap, sizeof(gzHeap));
}

void DispReset(bool32 refresh) {
    gMain.interruptFlag = 1;
    gUnk_03003DE0 = 0;
    gFadeControl.active = 0;
    gScreen.vBlankDMA.readyBackup = FALSE;
    gScreen.vBlankDMA.ready = FALSE;
    DmaStop(0);
#ifdef PC_PORT
    gba_write16(REG_ADDR_DISPCNT, 0);
#else
    REG_DISPCNT = 0;
#endif
    ClearOAM();
    ResetScreenRegs();
#ifdef PC_PORT
    gba_MemClear(0x0600C000, 0x20);
#else
    MemClear((void*)0x600C000, 0x20);
#endif
    MemClear(gBG0Buffer, sizeof(gBG0Buffer));
    gScreen.bg0.updated = refresh;
}

void ClearOAM(void) {
    u8* d = (u8*)gOAMControls.oam;
    u8* mem = (u8*)0x07000000;
    u32 i;
    for (i = 128; i != 0; --i) {
        *(u16*)d = 0x2A0;
        d += 8;
#ifdef PC_PORT
        gba_write16((uint32_t)mem, 0x2A0);
#else
        *(u16*)mem = 0x2A0;
#endif
        mem += 8;
    }
}

void ResetScreenRegs(void) {
    MemClear(&gScreen, sizeof(gScreen));
    gScreen.bg0.subTileMap = &gBG0Buffer;
    gScreen.bg0.control = 0x1F0C;
    gScreen.bg1.subTileMap = &gBG1Buffer;
    gScreen.bg1.control = 0x1C01;
    gScreen.bg2.subTileMap = &gBG2Buffer;
    gScreen.bg2.control = 0x1D02;
    gScreen.bg3.subTileMap = &gBG3Buffer;
    gScreen.bg3.control = 0x1E03;
    gScreen.lcd.displayControl = 0x140;
    gScreen.lcd.displayControlMask = 0xffff;
}

u32 sub_0801DB94(void) {
    return gRoomTransition.player_status.dungeon_map_y >> 11;
}

void DrawDungeonMap(u32 floor, DungeonMapObject* specialData, u32 size) {
    DungeonLayout* floorMapData;
    TileEntity* tileEntity;
    u32 flagBankOffset;
    RoomHeader* roomHeader;
    s32 tmp1;
    s32 tmp3;

    MemClear(specialData, size);
    specialData->type = DMO_TYPE_PLAYER;
    specialData->x = (gRoomTransition.player_status.dungeon_map_x >> 4) & 0x7f;
    specialData->y = (gRoomTransition.player_status.dungeon_map_y >> 4) & 0x7f;
    specialData++;
    floorMapData = (DungeonLayout*)gDungeonLayouts[gArea.dungeon_idx][floor];
    while (floorMapData->area != 0) {
        tileEntity = (TileEntity*)GetRoomProperty(floorMapData->area, floorMapData->room, 3);
        if (tileEntity == NULL) {
            floorMapData++;
        } else {
            flagBankOffset = sub_0801DF10(floorMapData);
            if (HasDungeonCompass()) {
                while (tileEntity->type != 0) {
                    switch (tileEntity->type) {
                        case SMALL_CHEST:
                        case BIG_CHEST:
                            if (!CheckLocalFlagByBank(flagBankOffset, tileEntity->localFlag)) {
                                roomHeader = Common_GetAreaRoomHeaderSafe(floorMapData->area, floorMapData->room);
                                if (roomHeader == NULL) {
                                    break;
                                }
                                specialData->type = DMO_TYPE_CHEST;
                                if (tileEntity->type == SMALL_CHEST) {
                                    specialData->x =
                                        ((((tileEntity->tilePos << 4 & 0x3f0) | 8) + (roomHeader->map_x % 0x800)) >> 4);
                                    specialData->y =
                                        ((((tileEntity->tilePos >> 2 & 0x3f0) | 8) + (roomHeader->map_y % 0x800)) >> 4);
                                } else {
                                    specialData->x = (((roomHeader->map_x % 0x800) + tileEntity->tilePos) >> 4);
                                    specialData->y = (((roomHeader->map_y % 0x800) + (*(u16*)&tileEntity->_6)) >> 4);
                                }
                                specialData++;
                            }
                            break;
                    }
                    tileEntity++;
                }
            }
            if ((HasDungeonCompass() && ((floorMapData->unk_2 & 2) != 0)) &&
                (!CheckGlobalFlag(gArea.dungeon_idx + 1))) {
                roomHeader = Common_GetAreaRoomHeaderSafe(floorMapData->area, floorMapData->room);
                if (roomHeader != NULL) {
                    specialData->type = DMO_TYPE_BOSS;
                    tmp1 = ((roomHeader->pixel_width / 2) + roomHeader->map_x) / 16;
                    if (tmp1 < 0) {
                        tmp1 = tmp1 + 0x7f;
                    }
                    specialData->x = tmp1 + (tmp1 / 128) * -128;
                    tmp3 = ((roomHeader->pixel_height / 2) + roomHeader->map_y) / 16;
                    specialData->y = tmp3 + (tmp3 / 128) * -128;
                    specialData++;
                }
            }
            if (floorMapData->area == gRoomTransition.player_status.dungeon_area &&
                floorMapData->room == gRoomTransition.player_status.dungeon_room) {
                specialData->type = DMO_TYPE_ENTRY;
                // TODO rename dungeon_x and dungeon_y to dungeonEntryX/Y?
                specialData->x = ((u16)gRoomTransition.player_status.dungeon_x >> 4) & 0x7f;
                specialData->y = ((u16)gRoomTransition.player_status.dungeon_y >> 4) & 0x7f;
                specialData++;
            }
            floorMapData++;
        }
    }
}

void sub_0801DD58(u32 area, u32 room) {
    RoomHeader* hdr = Common_GetAreaRoomHeaderSafe(area, room);
    if (hdr == NULL) {
        return;
    }
    gArea.pCurrentRoomInfo->map_x = hdr->map_x;
    gArea.pCurrentRoomInfo->map_y = hdr->map_y;
}

void LoadDungeonMap(void) {
    LoadResourceAsync(gDungeonMap, (void*)(uintptr_t)0x6006000, sizeof(gDungeonMap));
}

void DrawDungeonFeatures(u32 floor, void* data, u32 size) {
    u32 bankOffset;
    u32 width;
    u32 height;
    u32 x;
    u32 y;
    u16 mapX;
    u16 mapY;
    u32 tmp;
    u32 tmp2;
    u32 color;
    u32 features;
    TileEntity* tileEntity;
    RoomHeader* roomHeader;
    const DungeonLayout* layout;
    const DungeonLayout* nextLayout;
    u8* ptr;
    u32 tmp3;
    u32 tmp4;

    if (!AreaHasMap()) {
        return;
    }
    layout = gDungeonLayouts[gArea.dungeon_idx][floor];
    MemClear(gMapDataBottomSpecial, 0x8000);
#ifdef PC_PORT
    /* #69: gDungeonMap accumulates pixels across floor switches because the
     * `if (features != 0)` gate below skips rooms with no draw features —
     * those tiles never get their pixels reset, so the previous floor's
     * pixels stay drawn under the new floor. The original GBA flow must
     * have cleared the bitmap somewhere off the C dispatch path; here we
     * clear the destination explicitly before each redraw. Cheap (8 KiB
     * memset) and only runs on floor switch / pause-menu open. */
    MemClear(gDungeonMap, sizeof(gDungeonMap));
#endif
    while (layout->area != 0) {
        // ROOM_VISIT_MARKER has to be first TileEntity in the room.
        tileEntity = (TileEntity*)GetRoomProperty(layout->area, layout->room, 3);
        bankOffset = sub_0801DF10(layout);
        features = 0;
        if (layout->area == gUI.roomControls.area && layout->room == gUI.roomControls.room) {
            features = 8;
        } else {
            if (HasDungeonMap()) {
                features = 2;
            }
            if (IsRoomVisited(tileEntity, bankOffset)) {
                features = 3;
            }
        }
        if ((layout->unk_2 & 1) != 0) {
            features = 0;
        }
        nextLayout = layout + 1;
        if (features != 0) {
            // Copies 0x400 bytes even though the data is less most of the time.
            /* gDungeonLayouts is a compiled table — mapDataOffset is a baked
             * USA gMapData index; remap for EU/JP (garbage pause-menu maps). */
            DmaCopy32(3, gMapData + Port_RemapMapOffset(layout->mapDataOffset), gMapDataBottomSpecial, 0x400);

            roomHeader = Common_GetAreaRoomHeaderSafe(layout->area, layout->room);
            if (roomHeader == NULL) {
                layout = nextLayout;
                continue;
            }
            mapX = roomHeader->map_x / 0x10;
            tmp3 = roomHeader->map_y;
            tmp4 = 0x7ff;
            mapY = (tmp3 & tmp4) / 0x10;
            width = roomHeader->pixel_width / 0x10;
            height = roomHeader->pixel_height / 0x10;
            tmp = (width + 3) / 4;

            for (y = 0; y < height; y++) {
                ptr = (u8*)gMapDataBottomSpecial + y * tmp;
                for (x = 0; x < width; x++) {
                    tmp2 = mapX + x;
                    color = sub_0801DF78(sub_0801DF60(x, ptr), features);
                    DrawMapPixel(tmp2, mapY + y, color);
                }
            }
        }
        layout = nextLayout;
    }
}

u32 sub_0801DF10(const DungeonLayout* layout) {
    u32 offset;

    if (layout->unk_3 == 1)
        offset = 0x300;
    else
        offset = GetFlagBankOffset(layout->area);
    return offset;
}

void DrawMapPixel(u32 x, u32 y, s32 color) {
    u32* ptr;
    u32 tmp;
    ptr = &gDungeonMap[(((y >> 3) * 0x10 + (x >> 3)) * 8)];
    ptr = &ptr[(y & 7)];
    tmp = (color << ((x & 7) * 4));
    ptr[0] = (ptr[0] & gUnk_080C9460[x & 7]) | tmp;
}
u32 sub_0801DF60(u32 a1, u8* p) {
    return (p[a1 >> 2] >> (2 * (~a1 & 3))) & 3;
}

u32 sub_0801DF78(u32 a1, u32 a2) {
    switch (a1) {
        case 0:
        case 1:
        default:
            return a1;
        case 2:
            return a2;
        case 3:
            return 7;
    }
}

bool32 IsRoomVisited(TileEntity* tileEntity, u32 bank) {
    if (tileEntity == NULL)
        return FALSE;

    for (; tileEntity->type != 0; tileEntity++) {
        if (tileEntity->type == ROOM_VISIT_MARKER)
            return CheckLocalFlagByBank(bank, tileEntity->localFlag);
    }
    return FALSE;
}

void InitializeFuseInfo(Entity* entity, u32 textIndex, u32 cancelledTextIndex, u32 fusingTextIndex) {
    MemClear(&gFuseInfo, sizeof(gFuseInfo));
    gFuseInfo.textIndex = textIndex;
    gFuseInfo.cancelledTextIndex = cancelledTextIndex;
    gFuseInfo.fusingTextIndex = fusingTextIndex;
    gFuseInfo.entity = entity;
    gFuseInfo.kinstoneId = gPossibleInteraction.kinstoneId;
    if (entity != NULL) {
        gFuseInfo.prevUpdatePriority = entity->updatePriority;
        entity->updatePriority = 2;
    }
    gFuseInfo.fusionState = FUSION_STATE_0;
}

// returns the fusion state
u32 PerformFuseAction(void) {
    gFuseActions[gFuseInfo.action]();
    return gFuseInfo.fusionState;
}

void Fuse_Action0(void) {
    MessageFromFusionTarget(gFuseInfo.textIndex);
    gFuseInfo.fusionState = FUSION_STATE_3;
    gFuseInfo.action = 1;
}

void Fuse_Action1(void) {
    if ((gMessage.state & MESSAGE_ACTIVE) == 0) {
        MenuFadeIn(4, 0);
        gFuseInfo.fusionState = FUSION_STATE_4;
        gFuseInfo.action = 2;
        SoundReq(SFX_6B);
    }
}

// Waits until FUSION_STATE_5 or FUSION_STATE_6 is reached and displays the corresponding message.
void Fuse_Action2(void) {
    u32 textIndex;
    switch (gFuseInfo.fusionState) {
        case FUSION_STATE_5:
            textIndex = gFuseInfo.cancelledTextIndex;
            break;
        case FUSION_STATE_6:
            textIndex = gFuseInfo.fusingTextIndex;
            break;
        default:
            return;
    }
    MessageFromFusionTarget(textIndex);
    gFuseInfo.action = 3;
}

void Fuse_Action3(void) {
    if ((gMessage.state & MESSAGE_ACTIVE) == 0) {
        if (gFuseInfo.entity != NULL) {
            gFuseInfo.entity->updatePriority = gFuseInfo.prevUpdatePriority;
        }
        gFuseInfo.fusionState = gFuseInfo.fusionState == FUSION_STATE_6 ? FUSION_STATE_2 : FUSION_STATE_1;
    }
}

void MessageFromFusionTarget(u32 textIndex) {
    if (textIndex != 0) {
        if (gFuseInfo.entity != NULL) {
            MessageNoOverlap(textIndex, gFuseInfo.entity);
        } else {
            MessageFromTarget(textIndex);
        }
    }
}

void sub_0801E104(void) {
    gScreen.lcd.displayControl &= ~0x6000;
    gScreen.vBlankDMA.ready = FALSE;
#ifdef PC_PORT
    /* #103 root cause: this sub only clears vBlankDMA.ready. On GBA that was
       enough because VBlankIntr ran DmaStop(0) every frame, so the HBlank DMA
       stopped re-arming the moment `ready` went FALSE. On PC DmaStop is a
       no-op and the per-scanline HBlank channel keeps firing the stale src
       table into its dest register until something calls DisableVBlankDMA —
       corrupting the next scene's BG/window rendering. Tear the channel down
       here exactly like DisableVBlankDMA does (main.c). This neutralizes the
       whole class of sub_0801E104-only teardowns: lightManager (dark-room end),
       whiteTriangleEffect (screen wipe), vaatiAppearingManager Evaporate. */
    port_hdma_unregister(0);
#endif
}

void sub_0801E120(void) {
    gScreen.lcd.displayControl |= 0x2000;
    gScreen.controls.windowInsideControl = 0x3F37;
    gScreen.controls.windowOutsideControl = 0x3F;
    gScreen.controls.window0HorizontalDimensions = 0;
    gScreen.controls.window0VerticalDimensions = 160;
}

void sub_0801E154(u32 a1) {
    sub_0801E24C(a1, 0);
}

void sub_0801E160(u32 a1, u32 a2, u32 a3) {
    MemClear(&((struct_02017AA0*)gUnk_02017AA0)[gUnk_03003DE4[0]], sizeof(struct_02017AA0));
    sub_0801E290(a1, a2, a3);
    SetVBlankDMA((u16*)&((struct_02017AA0*)gUnk_02017AA0)[gUnk_03003DE4[0]], (u16*)REG_ADDR_WIN0H,
                 ((DMA_ENABLE | DMA_START_HBLANK | DMA_16BIT | DMA_REPEAT | DMA_SRC_INC | DMA_DEST_RELOAD) << 16) +
                     0x1);
}

void sub_0801E1B8(u32 a1, u32 a2) {
    gScreen.lcd.displayControl |= 0x2000;
    gScreen.controls.windowInsideControl = a1;
    gScreen.controls.windowOutsideControl = a2;
    gScreen.controls.window0HorizontalDimensions = 0;
    gScreen.controls.window0VerticalDimensions = 160;
}

void sub_0801E1EC(u32 a1, u32 a2, u32 a3) {
    MemClear(&((struct_02017AA0*)gUnk_02017AA0)[gUnk_03003DE4[0]], sizeof(struct_02017AA0));
    sub_0801E24C(a3, 0);
    sub_0801E290(a1, a2, a3);
    SetVBlankDMA((u16*)&((struct_02017AA0*)gUnk_02017AA0)[gUnk_03003DE4[0]], (u16*)REG_ADDR_WIN0H,
                 ((DMA_ENABLE | DMA_START_HBLANK | DMA_16BIT | DMA_REPEAT | DMA_SRC_INC | DMA_DEST_RELOAD) << 16) +
                     0x1);
}

void sub_0801E24C(s32 param_1, s32 param_2) {
    s32 r1;
    s32 r2, i;
    u16* p5;
    p5 = &gUnk_02018EE0[param_2];
    i = 0;
    r2 = param_1;
    r1 = 3 - (r2 * 2);
    while (i <= r2) {
        p5[i] = r2;
        p5[r2] = i;
        if (r1 < 0) {
            r1 += 6 + i++ * 4;
        } else {
            r1 += 10 + (i++ - (r2--)) * 4;
        }
    }
}

void sub_0801E290(u32 param_1, u32 param_2, u32 count) {
    s32 uVar1;
    s32 iVar2;
    s32 iVar4;
    u8* forwardAccess;
    u8* backwardAccess;
    s16* puVar6;
    u32 uVar5;
    u32 uVar7;
    u32 index;
    u32 x;
#ifdef PC_PORT
    /* On GBA, pointer arithmetic wraps at 32 bits, so starting from filler[-2]
     * (param_2=0xFFFFFFFF) works. On 64-bit, that produces an invalid address.
     * Use index-based access instead of pointer incrementing. */
    u8* base = ((struct_02017AA0*)gUnk_02017AA0)[gUnk_03003DE4[0]].filler;
    uVar5 = uVar7 = param_2;
    puVar6 = gUnk_02018EE0;

    while (count-- > 0) {
        uVar1 = *puVar6++;
        iVar2 = param_1 - uVar1;
        iVar4 = param_1 + uVar1;
        if (iVar2 < 0) {
            iVar2 = 0;
        }
        if (iVar4 > 0xef) {
            iVar4 = 0xf0;
        }
        if (((u16)uVar5 & 0xffff) < 0xa0) {
            base[uVar5 * 2] = iVar4;
            base[uVar5 * 2 + 1] = iVar2;
        }
        if (((u16)uVar7 & 0xffff) < 0xa0) {
            base[uVar7 * 2] = iVar4;
            base[uVar7 * 2 + 1] = iVar2;
        }
        uVar5--;
        uVar7++;
    }
#else
    forwardAccess = &gUnk_02017AA0[gUnk_03003DE4[0]].filler[param_2 * 2];
    backwardAccess = forwardAccess;
    uVar5 = uVar7 = param_2;
    puVar6 = gUnk_02018EE0;

    while (count-- > 0) {
        uVar1 = *puVar6++;
        iVar2 = param_1 - uVar1;
        iVar4 = param_1 + uVar1;
        if (iVar2 < 0) {
            iVar2 = 0;
        }
        if (iVar4 > 0xef) {
            iVar4 = 0xf0;
        }
        if (((u16)uVar5 & 0xffff) < 0xa0) {
            backwardAccess[0] = iVar4;
            backwardAccess[1] = iVar2;
        }
        if (((u16)uVar7 & 0xffff) < 0xa0) {
            forwardAccess[0] = iVar4;
            forwardAccess[1] = iVar2;
        }
        backwardAccess -= 2;
        forwardAccess += 2;
        uVar5--;
        uVar7++;
    }
#endif
}

void sub_0801E31C(u32 sp00, u32 sp04, s32 r10, s32 r9) {
    u16 sp1c, sp1c2;
    u16 kk, kk2;
    u16 uVar2;
    s32 r5;
    s32 r6;
    s32 r7;
    s32 r8; // the lower one of param3 and param4

    MemClear(&gUnk_02017AA0[gUnk_03003DE4[0]], 0xa00);
    if (r10 < r9) {
        r6 = 0;
        r7 = r8 = r10;
        r5 = 3 - r8 * 2;

        while (r6 <= r7) {
            sp1c = Div(r9 * r6, r10);
            kk = Div(r9 * r7, r10);
            // TODO: Fix data type in declaration. There shouldn't be a need to cast this.
            ((u32*)gUnk_02018EE0)[r6] = kk;
            ((u32*)gUnk_02018EE0)[r7] = sp1c;
            if (r5 < 0) {
                r5 += 6 + r6 * 4;
                r6++;
            } else {
                r5 += 10 + (r6 - r7) * 4;
                r7--;
                r6++;
            }
        }
    } else {
        r6 = 0;
        r7 = r8 = r9;
        r5 = 3 - r8 * 2;

        while (r6 <= r7) {
            sp1c2 = Div(r10 * r6, r9);
            kk2 = Div(r10 * r7, r9);
            // TODO: Fix data type in declaration. There shouldn't be a need to cast this.
            ((u32*)gUnk_02018EE0)[r6] = kk2;
            ((u32*)gUnk_02018EE0)[r7] = sp1c2;
            if (r5 < 0) {
                r5 += 6 + r6 * 4;
                r6++;
            } else {
                r5 += 10 + (r6 - r7) * 4;
                r7--;
                r6++;
            }
        }
    }
    sub_0801E290(sp00, sp04, r8);
    SetVBlankDMA((u16*)&gUnk_02017AA0[gUnk_03003DE4[0]], (u16*)REG_ADDR_WIN0H,
                 ((DMA_ENABLE | DMA_START_HBLANK | DMA_16BIT | DMA_REPEAT | DMA_SRC_INC | DMA_DEST_RELOAD) << 16) +
                     0x1);
}

void sub_0801E49C(s32 baseX, s32 baseY, s32 radius, u32 baseAngle) {
    u8* ptr2;
    u32* ptr1;
    u32 angle;
    s32 x1, x2, x3, y1, y2, y3;

    MemFill16(0xffff, gUnk_02018EE0, 0x780);
    angle = (baseAngle - 0x40) & 0xff;
    x1 = baseX + (gSineTable[angle + 0x40] * radius >> 8);
    y1 = baseY + (gSineTable[angle] * radius >> 8);
    angle = (baseAngle + 0x68) & 0xff;
    x2 = baseX + (gSineTable[angle + 0x40] * radius >> 8);
    y2 = baseY + (gSineTable[angle] * radius >> 8);
    angle = (baseAngle - 0xe8) & 0xff;
    x3 = baseX + (gSineTable[angle + 0x40] * radius >> 8);
    y3 = baseY + (gSineTable[angle] * radius >> 8);
    sub_0801E64C(x1, y1, x2, y2, 0);
    sub_0801E64C(x1, y1, x3, y3, 1);
    sub_0801E64C(x2, y2, x3, y3, 2);
    MemClear(((struct_02017AA0*)gUnk_02017AA0)[gUnk_03003DE4[0]].filler, 0xa00);
    ptr1 = (u32*)gUnk_02018EE0;
    ptr2 = ((struct_02017AA0*)gUnk_02017AA0)[gUnk_03003DE4[0]].filler;
    for (y1 = 0xa0; y1 > 0; y1--, ptr2 += 2) {
        x1 = ptr1[0];
        x2 = ptr1[1];
        x3 = ptr1[2];
        ptr1 += 3;
        if (x1 > x2) {
            SWAP(x1, x2, y2);
        }
        if (x1 > x3) {
            SWAP(x1, x3, y2);
        }
        if (x2 > x3) {
            SWAP(x2, x3, y2);
        }
        if (x1 != 0xffffffff) {
            ptr2[0] = x3;
            ptr2[1] = x1;
        } else {
            if (x2 != x1) {
                ptr2[0] = x3;
                ptr2[1] = x2;
            } else {
                if (x3 != x1) {
                    ptr2[1] = x1;
                    ptr2[0] = x1;
                }
            }
        }
    }
    SetVBlankDMA((u16*)((struct_02017AA0*)gUnk_02017AA0)[gUnk_03003DE4[0]].filler, (u16*)REG_ADDR_WIN0H,
                 ((DMA_ENABLE | DMA_START_HBLANK | DMA_16BIT | DMA_REPEAT | DMA_SRC_INC | DMA_DEST_RELOAD) << 16) +
                     0x1);
}

void sub_0801E64C(s32 x1, s32 y1, s32 x2, s32 y2, s32 offset) {
    // GBA Resolutions
    const s32 MAX_X_COORD = 240;
    const s32 MAX_Y_COORD = 160;

    s32 slope, preciseX, tmp;
    s32* drawPtr = (s32*)gUnk_02018EE0;

    if ((y1 < 0 && y2 < 0) || (y1 >= MAX_Y_COORD && y2 >= MAX_Y_COORD))
        return;

    if (y1 > y2) {
        SWAP(y1, y2, tmp);
        SWAP(x1, x2, tmp);
    }

    if (y1 == y2)
        return;

    slope = Div((x2 - x1) * 0x10000, y2 - y1);
    if (y1 < 0) {
        x1 += (slope * -y1) >> 0x10;
        y1 = 0;
    }
    if (y2 >= MAX_Y_COORD) {
        y2 = MAX_Y_COORD - 1;
    }
    preciseX = x1 << 0x10;
    drawPtr += y1 * 3 + offset;
    do {
        // Clamp x1 in range
        x1 = x1 < 0 ? 0 : x1;
        x1 = x1 < MAX_X_COORD ? x1 : MAX_X_COORD;

        *drawPtr = x1;
        preciseX += slope;
        x1 = preciseX >> 0x10;
        y1++;
        drawPtr += 3;
    } while (y1 <= y2);
}

void NotifyFusersOnFusionDone(KinstoneId kinstoneId) {
    u32 tmp;
    u32 index;
    if (kinstoneId - 1 < 100) {
        for (index = 0; index < 0x80; index++) {
            if (kinstoneId == gSave.kinstones.fuserOffers[index]) {
                gSave.kinstones.fuserOffers[index] = KINSTONE_NEEDS_REPLACEMENT;
            }
        }
        tmp = GetFuserId(gFuseInfo.entity);
        if ((tmp - 1 < 0x7f) && (gSave.kinstones.fuserOffers[tmp] == KINSTONE_NEEDS_REPLACEMENT)) {
            gSave.kinstones.fuserOffers[tmp] = KINSTONE_JUST_FUSED;
        }
        for (index = 0; index < 0x20; index++) {
            if (kinstoneId == gPossibleInteraction.candidates[index].kinstoneId) {
                gPossibleInteraction.candidates[index].kinstoneId = KINSTONE_NEEDS_REPLACEMENT;
            }
        }
    }
}

void AddKinstoneToBag(u32 kinstoneId) {
    s32 index;
    s32 tmp;

    SortKinstoneBag(); // sometimes called just for this function
    if (kinstoneId == KINSTONE_NONE) {
        return;
    }
    if ((u32)(kinstoneId - 0x65) < 0x11) {
        index = GetIndexInKinstoneBag(kinstoneId);
        if (index < 0) {
            index = 0;
            while (gSave.kinstones.types[index] != KINSTONE_NONE) {
                index++;
            }
        }
        if ((u32)index < 0x12) {
            gSave.kinstones.types[index] = kinstoneId;
            tmp = gSave.kinstones.amounts[index] + 1;
            if (tmp > 99) {
                tmp = 99;
            }
            gSave.kinstones.amounts[index] = tmp;
        }
    }
}

void RemoveKinstoneFromBag(KinstoneId kinstoneId) {
    s32 idx = GetIndexInKinstoneBag(kinstoneId);
    if (idx >= 0) {
        s32 next = gSave.kinstones.amounts[idx] - 1;
        if (next <= 0) {
            gSave.kinstones.types[idx] = KINSTONE_NONE;
            next = 0;
        }
        gSave.kinstones.amounts[idx] = next;
    }
}

u32 GetAmountInKinstoneBag(KinstoneId kinstoneId) {
    s32 index = GetIndexInKinstoneBag(kinstoneId);
    if (index < 0) {
        return 0;
    }
    return gSave.kinstones.amounts[index];
}

u32 CheckKinstoneFused(u32 kinstoneId) {
    if (kinstoneId - 1 >= 100) {
        return 0;
    }
    return ReadBit(&gSave.kinstones.fusedKinstones, kinstoneId);
}

bool32 CheckFusionMapMarkerDisabled(u32 kinstoneId) {
    if (kinstoneId - 1 >= 100) {
        return FALSE;
    }
    return ReadBit(&gSave.kinstones.fusionUnmarked, kinstoneId);
}

void SortKinstoneBag(void) {
    u32 i;

    KinstoneSave* ptr = &gSave.kinstones;

    for (i = 0; i < 19; i++) {
        if (ptr->amounts[i] == 0) {
            ptr->types[i] = 0;
        }
    }

    ptr->types[18] = 0;
    ptr->amounts[18] = 0;

    for (i = 0; i < 18; i++) {
        u32 t = ptr->types[i];
        if (t < 0x65 || t > 0x75) {
            MemCopy(&ptr->types[i + 1], &ptr->types[i], 0x12 - i);
            MemCopy(&ptr->amounts[i + 1], &ptr->amounts[i], 0x12 - i);
        }
    }
}
s32 GetIndexInKinstoneBag(u32 kinstoneId) {
    u32 i;

    for (i = 0; i < 0x12; ++i) {
        if (kinstoneId == gSave.kinstones.types[i])
            return i;
    }
    return -1;
}

// For example if a chest from a fusion is opened, hide the chest marker
void UpdateVisibleFusionMapMarkers(void) {
    u32 kinstoneId;
    const KinstoneWorldEvent* gKinstoneWorldEvents_sel = gKinstoneWorldEvents;
    extern const KinstoneWorldEvent gKinstoneWorldEvents_eu[];
    extern const KinstoneWorldEvent gKinstoneWorldEvents_jp[];
    if (REGION_IS_EU)
        gKinstoneWorldEvents_sel = gKinstoneWorldEvents_eu;
    else if (REGION_IS_JP)
        gKinstoneWorldEvents_sel = gKinstoneWorldEvents_jp;
    for (kinstoneId = 10; kinstoneId <= 100; ++kinstoneId) {
        if (CheckKinstoneFused(kinstoneId) && !CheckFusionMapMarkerDisabled(kinstoneId)) {
            u32 worldEventId = gKinstoneWorldEvents_sel[kinstoneId].worldEventId;
            const WorldEvent* s = &GetWorldEvents()[worldEventId];
            u32 flag = s->flag;
            u32 tmp;
            switch (s->condition) {
                case CND_0:
                    tmp = 0;
                    break;
                case CND_1:
                    tmp = s->bank;
                    break;
                case CND_2:
                    tmp = 0xf;
                    break;
                case CND_3:
                    tmp = 0x10;
                    break;
                case CND_4:
                    tmp = 0x11;
                    break;
#if (!defined(EU) && !defined(JP)) || defined(PC_PORT)
                // Special conditions for BEANDEMO_00 to BEANDEMO_04
                case CND_5:
                    tmp = LOCAL_BANK_3;
                    flag = SORA_10_H00;
                    break;
                case CND_6:
                    tmp = LOCAL_BANK_3;
                    flag = SORA_11_H00;
                    break;
                case CND_7:
                    tmp = LOCAL_BANK_3;
                    flag = SORA_12_T00;
                    break;
                case CND_8:
                    tmp = LOCAL_BANK_3;
                    flag = SORA_13_H00;
                    break;
                case CND_9:
                    tmp = LOCAL_BANK_3;
                    flag = SORA_14_T00;
                    break;
                case CND_10:
                    tmp = LOCAL_BANK_4;
                    flag = KS_B15;
                    break;
#endif
            }
            if (sub_0807CB24(tmp, (REGION_IS_EU || REGION_IS_JP) ? s->flag : flag)) {
                WriteBit(&gSave.kinstones.fusionUnmarked, kinstoneId);
            }
        }
    }
}

/* This table is packed 4-byte GBA pointers; `gUnk_08001DCC[idx]` would
 * read 8 bytes on x86-64. Use Port_UnpackRomDataPtr instead. */
extern const u8 gUnk_08001DCC[];

KinstoneId GetFusionToOffer(Entity* entity) {
    u8* fuserData;
    u32 fuserId;
    u32 offeredFusion;
    u32 fuserProgress;
    u8* fuserFusionData;
    s32 randomMood;
    u32 fuserStability;
    fuserId = GetFuserId(entity);

#ifdef PC_PORT
    fuserData = (u8*)Port_UnpackRomDataPtr(gUnk_08001DCC, fuserId);
#else
    fuserData = (u8*)((u8**)gUnk_08001DCC)[fuserId];
#endif
    if (fuserData == NULL) {
        return KINSTONE_NONE;
    }
    if (GetInventoryValue(ITEM_KINSTONE_BAG) == 0 || fuserData[0] > gSave.global_progress) {
        return KINSTONE_NONE;
    }
    offeredFusion = gSave.kinstones.fuserOffers[fuserId];
    fuserProgress = gSave.kinstones.fuserProgress[fuserId];
    fuserFusionData = fuserData + fuserProgress;
    while (TRUE) { // loop through fusions for this fuser
        switch (offeredFusion) {
            case KINSTONE_NEEDS_REPLACEMENT: // offered fusion completed with someone else
            case KINSTONE_NONE:              // no fusion offered yet
                offeredFusion = fuserFusionData[5];
                if (offeredFusion == KINSTONE_NONE || offeredFusion == KINSTONE_RANDOM ||
                    CheckKinstoneFused(offeredFusion) == 0) {
                    break;
                }
            case KINSTONE_JUST_FUSED: // previous fusion completed
                fuserFusionData++;
                fuserProgress++;
                offeredFusion = fuserFusionData[5];
        }
        if (offeredFusion == KINSTONE_RANDOM) { // random shared fusion
            offeredFusion = GetRandomSharedFusion(fuserData);
        }
        if (offeredFusion == KINSTONE_NONE) {    // end of fusion list
            offeredFusion = KINSTONE_FUSER_DONE; // mark this fuser as done
            break;
        }
        if (offeredFusion == KINSTONE_JUST_FUSED) { // previous fusion completed
            continue;
        }
        if (CheckKinstoneFused(offeredFusion) == 0) {
            break;
        }
        offeredFusion = KINSTONE_NEEDS_REPLACEMENT; // already completed, try next fusion in the list
    }
    gSave.kinstones.fuserOffers[fuserId] = offeredFusion;
    gSave.kinstones.fuserProgress[fuserId] = fuserProgress;
    randomMood = Random();
    fuserStability = fuserData[1];
    if (fuserStability <= randomMood % 100) {
        return KINSTONE_NONE; // fickleness
    }
    if (offeredFusion - 1 > 99) {
        offeredFusion = KINSTONE_NONE;
    }
    return offeredFusion;
}

const u16 gUnk_080C93E0[] = {
    3,   9,   16,  22,  28,  35,  41,   48,   54,   61,   67,   74,   81,   88,   95,   102,
    110, 117, 125, 133, 141, 149, 158,  167,  176,  185,  195,  205,  215,  226,  238,  250,
    262, 276, 290, 304, 320, 336, 354,  373,  394,  415,  439,  465,  493,  525,  559,  597,
    640, 689, 744, 808, 883, 971, 1078, 1209, 1375, 1591, 1885, 2308, 2973, 4167, 6950, 20860
};

const u32 gUnk_080C9460[] = {
    0xfffffff0, 0xffffff0f, 0xfffff0ff, 0xffff0fff, 0xfff0ffff, 0xff0fffff, 0xf0ffffff, 0xfffffff,
};

const DungeonLayout gDungeonLayouts_None_None[] = {
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout* const gDungeonLayouts_None[] = {
    gDungeonLayouts_None_None,
};

const DungeonLayout gDungeonLayouts_DeepwoodShrine_1F[] = {
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_BOSS_DOOR, 0, 0, offset_gDungeonMaps_DeepwoodShrine_BossDoor },
    { AREA_DEEPWOOD_SHRINE_BOSS, ROOM_DEEPWOOD_SHRINE_BOSS_MAIN, 2, 0, offset_gDungeonMaps_DeepwoodShrineBoss_Main },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_DeepwoodShrine_B1[] = {
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_MADDERPILLAR, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Madderpillar },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_BLUE_PORTAL, 0, 0, offset_gDungeonMaps_DeepwoodShrine_BluePortal },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_STAIRS_TO_B1, 0, 0, offset_gDungeonMaps_DeepwoodShrine_StairsToB1 },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_POT_BRIDGE, 0, 0, offset_gDungeonMaps_DeepwoodShrine_PotBridge },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_DOUBLE_STATUE, 0, 0, offset_gDungeonMaps_DeepwoodShrine_DoubleStatue },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_MAP, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Map },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_BARREL, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Barrel },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_BUTTON, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Button },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_MULLDOZER, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Mulldozer },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_PILLARS, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Pillars },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_LEVER, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Lever },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_ENTRANCE, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Entrance },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_TORCHES, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Torches },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_DeepwoodShrine_B2[] = {
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_BOSS_KEY, 0, 0, offset_gDungeonMaps_DeepwoodShrine_BossKey },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_COMPASS, 0, 0, offset_gDungeonMaps_DeepwoodShrine_Compass },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_LILY_PAD_WEST, 0, 0, offset_gDungeonMaps_DeepwoodShrine_LilyPadWest },
    { AREA_DEEPWOOD_SHRINE, ROOM_DEEPWOOD_SHRINE_LILY_PAD_EAST, 0, 0, offset_gDungeonMaps_DeepwoodShrine_LilyPadEast },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout* const gDungeonLayouts_DeepwoodShrine[] = {
    gDungeonLayouts_DeepwoodShrine_1F,
    gDungeonLayouts_DeepwoodShrine_B1,
    gDungeonLayouts_DeepwoodShrine_B2,
};

const DungeonLayout gDungeonLayouts_CaveOfFlames_1F[] = {
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_ENTRANCE, 0, 0, offset_gDungeonMaps_CaveOfFlames_Entrance },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_NORTH_ENTRANCE, 0, 0, offset_gDungeonMaps_CaveOfFlames_NorthEntrance },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_COMPASS, 0, 0, offset_gDungeonMaps_CaveOfFlames_Compass },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_BOB_OMB_WALL, 0, 0, offset_gDungeonMaps_CaveOfFlames_BobOmbWall },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_CaveOfFlames_B1[] = {
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_AFTER_CANE, 0, 0, offset_gDungeonMaps_CaveOfFlames_AfterCane },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_SPINY_CHU, 0, 0, offset_gDungeonMaps_CaveOfFlames_SpinyChu },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_CART_TO_SPINY_CHU, 0, 0,
      offset_gDungeonMaps_CaveOfFlames_CartToSpinyChu },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_MAIN_CART, 0, 0, offset_gDungeonMaps_CaveOfFlames_MainCart },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_CART_WEST, 0, 0, offset_gDungeonMaps_CaveOfFlames_CartWest },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_HELMASAUR_FIGHT, 0, 0, offset_gDungeonMaps_CaveOfFlames_HelmasaurFight },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_ROLLOBITE_LAVA_ROOM, 0, 0,
      offset_gDungeonMaps_CaveOfFlames_RollobiteLavaRoom },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_MINISH_LAVA_ROOM, 0, 0,
      offset_gDungeonMaps_CaveOfFlames_MinishLavaRoom },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_CaveOfFlames_B2[] = {
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_MINISH_SPIKES, 0, 0, offset_gDungeonMaps_CaveOfFlames_MinishSpikes },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_TOMPAS_DOOM, 0, 0, offset_gDungeonMaps_CaveOfFlames_TompasDoom },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_BEFORE_GLEEROK, 0, 0, offset_gDungeonMaps_CaveOfFlames_BeforeGleerok },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_BOSSKEY_PATH1, 0, 0, offset_gDungeonMaps_CaveOfFlames_BosskeyPath1 },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_BOSSKEY_PATH2, 0, 0, offset_gDungeonMaps_CaveOfFlames_BosskeyPath2 },
    { AREA_CAVE_OF_FLAMES, ROOM_CAVE_OF_FLAMES_BOSS_DOOR, 0, 0, offset_gDungeonMaps_CaveOfFlames_BossDoor },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_CaveOfFlames_B3[] = {
    { AREA_CAVE_OF_FLAMES_BOSS, ROOM_CAVE_OF_FLAMES_BOSS_0, 2, 0, offset_gDungeonMaps_CaveOfFlamesBoss_0 },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout* const gDungeonLayouts_CaveOfFlames[] = {
    gDungeonLayouts_CaveOfFlames_1F,
    gDungeonLayouts_CaveOfFlames_B1,
    gDungeonLayouts_CaveOfFlames_B2,
    gDungeonLayouts_CaveOfFlames_B3,
};

const DungeonLayout gDungeonLayouts_FortressOfWinds_3F[] = {
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_DOUBLE_EYEGORE, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_DoubleEyegore },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_EAST_KEY_LEVER, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_EastKeyLever },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_PIT_PLATFORMS, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_PitPlatforms },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_WEST_KEY_LEVER, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_WestKeyLever },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_MAZAAL, 2, 0, offset_gDungeonMaps_FortressOfWinds_Mazaal },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_BEFORE_MAZAAL, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_BeforeMazaal },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_FortressOfWinds_2F[] = {
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_DARKNUT_ROOM, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_DarknutRoom },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_ARROW_EYE_BRIDGE, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_ArrowEyeBridge },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_NORTH_SPLIT_PATH_PIT, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_NorthSplitPathPit },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_WALLMASTER_MINISH_PORTAL, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_WallmasterMinishPortal },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_PILLAR_CLONE_BUTTONS, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_PillarCloneButtons },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_ROTATING_SPIKE_TRAPS, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_RotatingSpikeTraps },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_STALFOS, 0, 0, offset_gDungeonMaps_FortressOfWinds_Stalfos },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_ENTRANCE_MOLE_MITTS, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_EntranceMoleMitts },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_MAIN_2F, 0, 0, offset_gDungeonMaps_FortressOfWinds_Main2f },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_MINISH_HOLE, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_MinishHole },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_BOSS_KEY, 0, 0, offset_gDungeonMaps_FortressOfWinds_BossKey },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_WEST_STAIRS_2F, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_WestStairs2f },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_EAST_STAIRS_2F, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_EastStairs2f },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_5, 1, 1, offset_gDungeonMaps_FortressOfWinds_5 },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_6, 1, 1, offset_gDungeonMaps_FortressOfWinds_6 },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_FortressOfWinds_1F[] = {
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_WEST_STAIRS_1F, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_WestStairs1f },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_CENTER_STAIRS_1F, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_CenterStairs1f },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_EAST_STAIRS_1F, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_EastStairs1f },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_WIZZROBE, 0, 0, offset_gDungeonMaps_FortressOfWinds_Wizzrobe },
    { AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_HEART_PIECE, 0, 0,
      offset_gDungeonMaps_FortressOfWinds_HeartPiece },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout* const gDungeonLayouts_FortressOfWinds[] = {
    gDungeonLayouts_FortressOfWinds_3F,
    gDungeonLayouts_FortressOfWinds_2F,
    gDungeonLayouts_FortressOfWinds_1F,
};

const DungeonLayout gDungeonLayouts_TempleOfDroplets_B1[] = {
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_WEST_HOLE, 0, 0, offset_gDungeonMaps_TempleOfDroplets_WestHole },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_NORTH_SPLIT_ROOM, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_NorthSplitRoom },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_EAST_HOLE, 0, 0, offset_gDungeonMaps_TempleOfDroplets_EastHole },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_ENTRANCE, 0, 0, offset_gDungeonMaps_TempleOfDroplets_Entrance },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_NORTHWEST_STAIRS, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_NorthwestStairs },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_SCISSORS_MINIBOSS, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_ScissorsMiniboss },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_WATERFALL_NORTHWEST, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_WaterfallNorthwest },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_WATERFALL_NORTHEAST, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_WaterfallNortheast },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_ELEMENT, 0, 0, offset_gDungeonMaps_TempleOfDroplets_Element },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_ICE_CORNER, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_IceCorner },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_ICE_PIT_MAZE, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_IcePitMaze },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_HOLE_TO_BLUE_CHU_KEY, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_HoleToBlueChuKey },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_WEST_WATERFALL_SOUTHEAST, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_WestWaterfallSoutheast },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_WEST_WATERFALL_SOUTHWEST, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_WestWaterfallSouthwest },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BIG_OCTO, 2, 0, offset_gDungeonMaps_TempleOfDroplets_BigOcto },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_TO_BLUE_CHU, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_ToBlueChu },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BLUE_CHU, 0, 0, offset_gDungeonMaps_TempleOfDroplets_BlueChu },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_TempleOfDroplets_B2[] = {
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BOSS_KEY, 0, 0, offset_gDungeonMaps_TempleOfDroplets_BossKey },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_NORTH_SMALL_KEY, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_NorthSmallKey },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BLOCK_CLONE_BUTTON_PUZZLE, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_BlockCloneButtonPuzzle },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BLOCK_CLONE_PUZZLE, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_BlockClonePuzzle },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BLOCK_CLONE_ICE_BRIDGE, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_BlockCloneIceBridge },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_STAIRS_TO_SCISSORS_MINIBOSS, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_StairsToScissorsMiniboss },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_SPIKE_BAR_FLIPPER_ROOM, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_SpikeBarFlipperRoom },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_9_LANTERNS, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_9Lanterns },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_LILYPAD_ICE_BLOCKS, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_LilypadIceBlocks },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_29, 0, 0, offset_gDungeonMaps_TempleOfDroplets_29 },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_MULLDOZERS_FIRE_BARS, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_MulldozersFireBars },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_DARK_MAZE, 0, 0, offset_gDungeonMaps_TempleOfDroplets_DarkMaze },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_TWIN_MADDERPILLARS, 1, 0,
      offset_gDungeonMaps_TempleOfDroplets_TwinMadderpillars },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_AFTER_TWIN_MADDERPILLARS, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_AfterTwinMadderpillars },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BLUE_CHU_KEY_LEVER, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_BlueChuKeyLever },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_MULLDOZER_KEY, 1, 0,
      offset_gDungeonMaps_TempleOfDroplets_MulldozerKey },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BEFORE_TWIN_MADDERPILLARS, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_BeforeTwinMadderpillars },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_LILYPAD_B2_WEST, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_LilypadB2West },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_COMPASS, 0, 0, offset_gDungeonMaps_TempleOfDroplets_Compass },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_DARK_SCISSOR_BEETLES, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_DarkScissorBeetles },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_LILYPAD_B2_MIDDLE, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_LilypadB2Middle },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_ICE_MADDERPILLAR, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_IceMadderpillar },
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_FLAMEBAR_BLOCK_PUZZLE, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_FlamebarBlockPuzzle },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_TempleOfDroplets_B3[] = {
    { AREA_TEMPLE_OF_DROPLETS, ROOM_TEMPLE_OF_DROPLETS_BLUE_CHU_KEY, 0, 0,
      offset_gDungeonMaps_TempleOfDroplets_BlueChuKey },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout* const gDungeonLayouts_TempleOfDroplets[] = {
    gDungeonLayouts_TempleOfDroplets_B1,
    gDungeonLayouts_TempleOfDroplets_B2,
    gDungeonLayouts_TempleOfDroplets_B3,
};

const DungeonLayout gDungeonLayouts_PalaceOfWinds_5F[] = {
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_GYORG_TORNADO, 2, 0, offset_gDungeonMaps_PalaceOfWinds_GyorgTornado },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BOSS_KEY, 0, 0, offset_gDungeonMaps_PalaceOfWinds_BossKey },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BEFORE_BALL_AND_CHAIN_SOLDIERS, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_BeforeBallAndChainSoldiers },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_GYORG_BOSS_DOOR, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_GyorgBossDoor },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_EAST_CHEST_FROM_GYORG_BOSS_DOOR, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_EastChestFromGyorgBossDoor },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_MOBLIN_AND_WIZZROBE_FIGHT, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_MoblinAndWizzrobeFight },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_FOUR_BUTTON_STALFOS, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_FourButtonStalfos },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_FAN_AND_KEY_TO_BOSS_KEY, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_FanAndKeyToBossKey },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BALL_AND_CHAIN_SOLDIERS, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_BallAndChainSoldiers },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BOMBAROSSA_PATH, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_BombarossaPath },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_HOLE_TO_DARKNUT, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_HoleToDarknut },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_TO_BOMBAROSSA_PATH, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_ToBombarossaPath },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BOMB_WALL_INSIDE, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_BombWallInside },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BOMB_WALL_OUTSIDE, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_BombWallOutside },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_PalaceOfWinds_4F[] = {
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_CLOUD_JUMPS, 0, 0, offset_gDungeonMaps_PalaceOfWinds_CloudJumps },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BLOCK_MAZE_TO_BOSS_DOOR, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_BlockMazeToBossDoor },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_CRACKED_FLOOR_LAKITU, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_CrackedFloorLakitu },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_HEART_PIECE_BRIDGE, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_HeartPieceBridge },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_FAN_BRIDGE, 0, 0, offset_gDungeonMaps_PalaceOfWinds_FanBridge },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_TO_FAN_BRIDGE, 0, 0, offset_gDungeonMaps_PalaceOfWinds_ToFanBridge },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_RED_WARP_HALL, 0, 0, offset_gDungeonMaps_PalaceOfWinds_RedWarpHall },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_PalaceOfWinds_3F[] = {
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_PLATFORM_CLONE_RIDE, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_PlatformCloneRide },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_PIT_CORNER_AFTER_KEY, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_PitCornerAfterKey },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_PLATFORM_CROW_RIDE, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_PlatformCrowRide },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_GRATE_PLATFORM_RIDE, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_GratePlatformRide },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_POT_PUSH, 0, 0, offset_gDungeonMaps_PalaceOfWinds_PotPush },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_FLOORMASTER_LEVER, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_FloormasterLever },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_MAP, 0, 0, offset_gDungeonMaps_PalaceOfWinds_Map },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_CORNER_TO_MAP, 0, 0, offset_gDungeonMaps_PalaceOfWinds_CornerToMap },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_STAIRS_AFTER_FLOORMASTER, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_StairsAfterFloormaster },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_HOLE_TO_KINSTONE_WIZZROBE, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_HoleToKinstoneWizzrobe },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_KEY_ARROW_BUTTON, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_KeyArrowButton },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_PalaceOfWinds_2F[] = {
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_GRATES_TO_3F, 0, 0, offset_gDungeonMaps_PalaceOfWinds_GratesTo3f },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_SPINY_FIGHT, 0, 0, offset_gDungeonMaps_PalaceOfWinds_SpinyFight },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_PEAHAT_SWITCH, 0, 0, offset_gDungeonMaps_PalaceOfWinds_PeahatSwitch },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_WHIRLWIND_BOMBAROSSA, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_WhirlwindBombarossa },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_DOOR_TO_STALFOS_FIREBAR, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_DoorToStalfosFirebar },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_STALFOS_FIREBAR_HOLE, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_StalfosFirebarHole },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_SHORTCUT_DOOR_BUTTONS, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_ShortcutDoorButtons },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_TO_PEAHAT_SWITCH, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_ToPeahatSwitch },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_KINSTONE_WIZZROBE_FIGHT, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_KinstoneWizzrobeFight },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_GIBDO_STAIRS, 0, 0, offset_gDungeonMaps_PalaceOfWinds_GibdoStairs },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_SPIKE_BAR_SMALL_KEY, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_SpikeBarSmallKey },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_PalaceOfWinds_1F[] = {
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_DARKNUT_MINIBOSS, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_DarknutMiniboss },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_ROC_CAPE, 0, 0, offset_gDungeonMaps_PalaceOfWinds_RocCape },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_FIRE_BAR_GRATES, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_FireBarGrates },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_PLATFORM_RIDE_BOMBAROSSAS, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_PlatformRideBombarossas },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BRIDGE_AFTER_DARKNUT, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_BridgeAfterDarknut },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_BRIDGE_SWITCHES_CLONE_BLOCK, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_BridgeSwitchesCloneBlock },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_ENTRANCE_ROOM, 0, 0, offset_gDungeonMaps_PalaceOfWinds_EntranceRoom },
    { AREA_PALACE_OF_WINDS, ROOM_PALACE_OF_WINDS_DARK_COMPASS_HALL, 0, 0,
      offset_gDungeonMaps_PalaceOfWinds_DarkCompassHall },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout* const gDungeonLayouts_PalaceOfWinds[] = {
    gDungeonLayouts_PalaceOfWinds_5F, gDungeonLayouts_PalaceOfWinds_4F, gDungeonLayouts_PalaceOfWinds_3F,
    gDungeonLayouts_PalaceOfWinds_2F, gDungeonLayouts_PalaceOfWinds_1F,
};

const DungeonLayout gDungeonLayouts_DarkHyruleCastle_3F[] = {
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_3F_TOP_LEFT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_3fTopLeftTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_3F_TOP_RIGHT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_3fTopRightTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_3F_BOTTOM_LEFT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_3fBottomLeftTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_3F_BOTTOM_RIGHT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_3fBottomRightTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_3F_KEATON_HALL_TO_VAATI, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_3fKeatonHallToVaati },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_3F_TRIPLE_DARKNUT, 2, 0,
      offset_gDungeonMaps_DarkHyruleCastle_3fTripleDarknut },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_DarkHyruleCastle_2F[] = {
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_LEFT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fTopLeftTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_LEFT_CORNER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fTopLeftCorner },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOSS_KEY, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBossKey },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BLUE_WARP, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBlueWarp },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_RIGHT_CORNER_GHINI, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fTopRightCornerGhini },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_RIGHT_CORNER_TORCHES, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fTopRightCornerTorches },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_RIGHT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fTopRightTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_LEFT_DARKNUT, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fTopLeftDarknut },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_SPARKS, 0, 0, offset_gDungeonMaps_DarkHyruleCastle_2fSparks },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_RIGHT_DARKNUTS, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fTopRightDarknuts },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_LEFT, 0, 0, offset_gDungeonMaps_DarkHyruleCastle_2fLeft },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_RIGHT, 0, 0, offset_gDungeonMaps_DarkHyruleCastle_2fRight },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_LEFT_DARKNUTS, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBottomLeftDarknuts },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOSS_DOOR, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBossDoor },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_RIGHT_DARKNUT, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBottomRightDarknut },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_LEFT_CORNER_PUZZLE, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBottomLeftCornerPuzzle },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_ENTRANCE, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fEntrance },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_RIGHT_CORNER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBottomRightCorner },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_LEFT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBottomLeftTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_LEFT_GHINI, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBottomLeftGhini },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_RIGHT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_2fBottomRightTower },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_DarkHyruleCastle_1F[] = {
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_ENTRANCE, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fEntrance },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_TOP_LEFT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fTopLeftTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_THRONE_ROOM, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fThroneRoom },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_COMPASS, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fCompass },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_TOP_RIGHT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fTopRightTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_BEFORE_THRONE, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fBeforeThrone },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_LOOP_TOP_LEFT, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fLoopTopLeft },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_LOOP_TOP, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fLoopTop },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_LOOP_TOP_RIGHT, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fLoopTopRight },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_LOOP_LEFT, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fLoopLeft },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_LOOP_RIGHT, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fLoopRight },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_LOOP_BOTTOM_LEFT, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fLoopBottomLeft },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_LOOP_BOTTOM, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fLoopBottom },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_LOOP_BOTTOM_RIGHT, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fLoopBottomRight },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_BOTTOM_LEFT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fBottomLeftTower },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_1F_BOTTOM_RIGHT_TOWER, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_1fBottomRightTower },
    { 0, 0, 0, 0, 0 },
};

const DungeonLayout gDungeonLayouts_DarkHyruleCastle_B1[] = {
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_ENTRANCE, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B1Entrance },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_BELOW_THRONE, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B1BelowThrone },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_BELOW_COMPASS, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B1BelowCompass },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_BEFORE_THRONE, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B1BeforeThrone },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_TO_PRISON, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B1ToPrison },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_BOMB_WALL, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B1BombWall },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_KEATONS, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B1Keatons },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_TO_PRISON_FIREBAR, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B1ToPrisonFirebar },
    {
        AREA_DARK_HYRULE_CASTLE,
        ROOM_DARK_HYRULE_CASTLE_B1_CANNONS,
        0,
        0,
        offset_gDungeonMaps_DarkHyruleCastle_B1Cannons,
    },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_LEFT, 0, 0, offset_gDungeonMaps_DarkHyruleCastle_B1Left },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_RIGHT, 0, 0, offset_gDungeonMaps_DarkHyruleCastle_B1Right },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B1_MAP, 0, 0, offset_gDungeonMaps_DarkHyruleCastle_B1Map },
    { 0, 0, 0, 0, 0 },
};
const DungeonLayout gDungeonLayouts_DarkHyruleCastle_B2[] = {
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B2_TO_PRISON, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B2ToPrison },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B2_PRISON, 0, 0, offset_gDungeonMaps_DarkHyruleCastle_B2Prison },
    { AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_B2_DROPDOWN, 0, 0,
      offset_gDungeonMaps_DarkHyruleCastle_B2Dropdown },
    { 0, 0, 0, 0, 0 },
};

const DungeonLayout* const gDungeonLayouts_DarkHyruleCastle[] = {
    gDungeonLayouts_DarkHyruleCastle_3F, gDungeonLayouts_DarkHyruleCastle_2F, gDungeonLayouts_DarkHyruleCastle_1F,
    gDungeonLayouts_DarkHyruleCastle_B1, gDungeonLayouts_DarkHyruleCastle_B2,
};

const DungeonLayout* const* const gDungeonLayouts[] = {
    gDungeonLayouts_None,
    gDungeonLayouts_DeepwoodShrine,
    gDungeonLayouts_CaveOfFlames,
    gDungeonLayouts_FortressOfWinds,
    gDungeonLayouts_TempleOfDroplets,
    gDungeonLayouts_PalaceOfWinds,
    gDungeonLayouts_DarkHyruleCastle,
};

const DungeonFloorMetadata gDungeonFloorMetadatas[] = {
    { 1, 2, 2 }, { 3, 3, 3 }, { 4, 3, 0 }, { 3, 5, 5 }, { 3, 2, 2 }, { 5, 7, 7 }, { 5, 5, 5 }, { 1, 3, 3 },
    { 1, 3, 3 }, { 1, 3, 3 }, { 1, 3, 3 }, { 1, 3, 3 }, { 1, 3, 3 }, { 1, 3, 3 }, { 1, 3, 3 }, { 1, 3, 3 },
};

void (*const gFuseActions[])(void) = {
    Fuse_Action0,
    Fuse_Action1,
    Fuse_Action2,
    Fuse_Action3,
};

const KinstoneWorldEvent gKinstoneWorldEvents[] = {
    { 15, 44, 45, 8, 0, 0, 0, 0 }, { 4, 8, 1, 0, 0, 1, 2, 0 },    { 4, 9, 1, 0, 0, 2, 2, 0 },
    { 4, 10, 1, 0, 0, 3, 2, 0 },   { 4, 10, 1, 0, 0, 3, 2, 0 },   { 4, 9, 1, 0, 0, 2, 2, 0 },
    { 4, 13, 2, 0, 0, 6, 2, 0 },   { 4, 14, 2, 0, 0, 7, 2, 0 },   { 4, 15, 2, 0, 0, 8, 2, 0 },
    { 4, 16, 3, 0, 0, 9, 2, 0 },   { 0, 18, 5, 8, 20, 11, 2, 2 }, { 0, 18, 5, 8, 77, 11, 0, 4 },
    { 0, 18, 5, 8, 65, 11, 2, 5 }, { 0, 18, 5, 8, 7, 11, 1, 3 },  { 0, 18, 5, 8, 87, 11, 2, 0 },
    { 0, 18, 5, 8, 92, 11, 2, 8 }, { 0, 18, 5, 8, 37, 11, 0, 3 }, { 0, 18, 5, 8, 55, 11, 0, 5 },
    { 0, 18, 5, 8, 62, 11, 2, 5 }, { 0, 19, 5, 8, 63, 12, 0, 5 }, { 0, 19, 5, 8, 88, 12, 2, 4 },
    { 0, 19, 5, 8, 66, 12, 2, 5 }, { 0, 19, 5, 8, 10, 12, 2, 2 }, { 0, 20, 5, 8, 70, 13, 2, 6 },
    { 0, 19, 5, 8, 42, 12, 0, 5 }, { 0, 19, 5, 8, 38, 12, 0, 3 }, { 0, 19, 5, 8, 68, 12, 2, 6 },
    { 0, 20, 5, 8, 76, 13, 0, 4 }, { 0, 20, 5, 8, 91, 13, 2, 4 }, { 0, 20, 5, 8, 67, 13, 2, 5 },
    { 0, 20, 5, 8, 43, 13, 2, 5 }, { 0, 20, 5, 8, 41, 13, 0, 7 }, { 0, 20, 5, 8, 36, 13, 0, 3 },
    { 0, 20, 5, 8, 50, 13, 2, 7 }, { 1, 21, 6, 8, 39, 14, 0, 7 }, { 1, 21, 6, 8, 69, 14, 2, 6 },
    { 1, 21, 6, 8, 72, 14, 2, 6 }, { 1, 22, 6, 8, 82, 15, 2, 4 }, { 1, 22, 6, 8, 84, 15, 2, 4 },
    { 1, 21, 6, 8, 56, 14, 2, 5 }, { 1, 21, 6, 8, 78, 14, 2, 5 }, { 1, 21, 6, 8, 81, 14, 2, 4 },
    { 1, 21, 6, 8, 83, 14, 2, 4 }, { 1, 21, 6, 8, 85, 14, 2, 4 }, { 1, 21, 6, 8, 90, 14, 2, 4 },
    { 1, 22, 6, 8, 57, 15, 0, 5 }, { 1, 22, 6, 8, 71, 15, 2, 6 }, { 1, 22, 6, 8, 86, 15, 2, 4 },
    { 1, 22, 6, 8, 79, 15, 2, 5 }, { 1, 22, 6, 8, 89, 15, 2, 4 }, { 1, 22, 6, 8, 58, 15, 0, 5 },
    { 1, 22, 6, 8, 80, 15, 2, 4 }, { 2, 23, 7, 8, 40, 16, 1, 7 }, { 2, 23, 7, 8, 46, 16, 0, 5 },
    { 2, 23, 7, 8, 13, 16, 2, 2 }, { 2, 23, 7, 8, 16, 16, 1, 2 }, { 2, 23, 7, 8, 19, 16, 2, 2 },
    { 2, 23, 7, 8, 23, 16, 2, 2 }, { 2, 23, 7, 8, 47, 16, 2, 7 }, { 2, 23, 7, 8, 2, 16, 1, 3 },
    { 2, 23, 7, 8, 5, 16, 1, 3 },  { 2, 23, 7, 8, 9, 16, 1, 3 },  { 2, 23, 7, 8, 75, 16, 2, 7 },
    { 2, 23, 7, 8, 45, 16, 1, 5 }, { 2, 23, 7, 8, 51, 16, 2, 5 }, { 2, 23, 7, 8, 59, 16, 2, 5 },
    { 2, 23, 7, 8, 64, 16, 2, 5 }, { 2, 24, 7, 8, 11, 17, 2, 2 }, { 2, 24, 7, 8, 14, 17, 2, 2 },
    { 2, 24, 7, 8, 17, 17, 2, 2 }, { 2, 24, 7, 8, 21, 17, 2, 2 }, { 2, 24, 7, 8, 24, 17, 1, 2 },
    { 2, 24, 7, 8, 48, 17, 1, 7 }, { 2, 24, 7, 8, 3, 17, 1, 3 },  { 2, 24, 7, 8, 6, 17, 1, 3 },
    { 2, 24, 7, 8, 73, 17, 2, 7 }, { 2, 24, 7, 8, 49, 17, 1, 7 }, { 2, 24, 7, 8, 52, 17, 2, 5 },
    { 2, 24, 7, 8, 60, 17, 2, 5 }, { 2, 25, 7, 8, 12, 18, 2, 2 }, { 2, 25, 7, 8, 15, 18, 2, 2 },
    { 2, 25, 7, 8, 18, 18, 2, 2 }, { 2, 25, 7, 8, 22, 18, 2, 2 }, { 2, 25, 7, 8, 25, 18, 2, 2 },
    { 2, 25, 7, 8, 1, 18, 1, 3 },  { 2, 25, 7, 8, 4, 18, 1, 3 },  { 2, 25, 7, 8, 8, 18, 1, 3 },
    { 2, 25, 7, 8, 74, 18, 2, 7 }, { 2, 25, 7, 8, 44, 18, 1, 5 }, { 2, 25, 7, 8, 53, 18, 2, 5 },
    { 2, 25, 7, 8, 54, 18, 2, 5 }, { 2, 25, 7, 8, 61, 18, 2, 5 }, { 2, 23, 7, 8, 26, 16, 2, 2 },
    { 2, 24, 7, 8, 27, 17, 2, 2 }, { 2, 25, 7, 8, 28, 18, 2, 2 }, { 2, 23, 7, 8, 29, 16, 2, 2 },
    { 2, 24, 7, 8, 30, 17, 1, 2 }, { 2, 25, 7, 8, 31, 18, 2, 2 }, { 2, 24, 7, 8, 32, 17, 2, 2 },
    { 2, 24, 7, 8, 33, 17, 1, 2 }, { 2, 25, 7, 8, 34, 18, 2, 2 }, { 4, 26, 1, 8, 0, 1, 0, 0 },
    { 4, 27, 1, 8, 0, 2, 0, 0 },   { 4, 28, 1, 8, 0, 3, 0, 0 },   { 4, 28, 1, 8, 0, 3, 0, 0 },
    { 4, 27, 1, 8, 0, 2, 0, 0 },   { 4, 31, 2, 8, 0, 6, 0, 0 },   { 4, 32, 2, 8, 0, 7, 0, 0 },
    { 4, 33, 2, 8, 0, 8, 0, 0 },   { 4, 34, 3, 8, 0, 9, 0, 0 },   { 0, 36, 5, 8, 0, 11, 0, 0 },
    { 0, 37, 5, 8, 0, 12, 0, 0 },  { 0, 38, 5, 8, 0, 13, 0, 0 },  { 1, 39, 6, 8, 0, 14, 0, 0 },
    { 1, 40, 6, 8, 0, 15, 0, 0 },  { 2, 41, 7, 8, 0, 16, 0, 0 },  { 2, 42, 7, 8, 0, 17, 0, 0 },
    { 2, 43, 7, 8, 0, 18, 0, 0 },
};

// Runtime twins: baseline above is the USA body. Select these at the readers by region.
const KinstoneWorldEvent gKinstoneWorldEvents_eu[] = {
    { 15, 44, 45, 8, 0, 0, 0, 0 }, { 4, 8, 1, 0, 0, 1, 2, 0 },    { 4, 9, 1, 0, 0, 2, 2, 0 },
    { 4, 10, 1, 0, 0, 3, 2, 0 },   { 4, 10, 1, 0, 0, 3, 2, 0 },   { 4, 9, 1, 0, 0, 2, 2, 0 },
    { 4, 13, 2, 0, 0, 6, 2, 0 },   { 4, 14, 2, 0, 0, 7, 2, 0 },   { 4, 15, 2, 0, 0, 8, 2, 0 },
    { 4, 16, 3, 0, 0, 9, 2, 0 },   { 0, 18, 5, 8, 20, 11, 3, 2 }, { 0, 18, 5, 8, 77, 11, 0, 4 },
    { 0, 18, 5, 8, 65, 11, 2, 5 }, { 0, 18, 5, 8, 7, 11, 1, 3 },  { 0, 18, 5, 8, 87, 11, 2, 0 },
    { 0, 18, 5, 8, 92, 11, 2, 8 }, { 0, 18, 5, 8, 37, 11, 0, 3 }, { 0, 18, 5, 8, 55, 11, 0, 5 },
    { 0, 18, 5, 8, 62, 11, 2, 5 }, { 0, 19, 5, 8, 63, 12, 0, 5 }, { 0, 19, 5, 8, 88, 12, 3, 4 },
    { 0, 19, 5, 8, 66, 12, 2, 5 }, { 0, 19, 5, 8, 10, 12, 3, 2 }, { 0, 20, 5, 8, 70, 13, 2, 6 },
    { 0, 19, 5, 8, 42, 12, 0, 5 }, { 0, 19, 5, 8, 38, 12, 0, 3 }, { 0, 19, 5, 8, 68, 12, 2, 6 },
    { 0, 20, 5, 8, 76, 13, 0, 4 }, { 0, 20, 5, 8, 91, 13, 3, 4 }, { 0, 20, 5, 8, 67, 13, 2, 5 },
    { 0, 20, 5, 8, 43, 13, 3, 5 }, { 0, 20, 5, 8, 41, 13, 1, 7 }, { 0, 20, 5, 8, 36, 13, 0, 3 },
    { 0, 20, 5, 8, 50, 13, 2, 7 }, { 1, 21, 6, 8, 39, 14, 0, 7 }, { 1, 21, 6, 8, 69, 14, 2, 6 },
    { 1, 21, 6, 8, 72, 14, 2, 6 }, { 1, 22, 6, 8, 82, 15, 2, 4 }, { 1, 22, 6, 8, 84, 15, 2, 4 },
    { 1, 21, 6, 8, 56, 14, 2, 5 }, { 1, 21, 6, 8, 78, 14, 2, 5 }, { 1, 21, 6, 8, 81, 14, 2, 4 },
    { 1, 21, 6, 8, 83, 14, 2, 4 }, { 1, 21, 6, 8, 85, 14, 2, 4 }, { 1, 21, 6, 8, 90, 14, 2, 4 },
    { 1, 22, 6, 8, 57, 15, 0, 5 }, { 1, 22, 6, 8, 71, 15, 2, 6 }, { 1, 22, 6, 8, 86, 15, 2, 4 },
    { 1, 22, 6, 8, 79, 15, 2, 5 }, { 1, 22, 6, 8, 89, 15, 3, 4 }, { 1, 22, 6, 8, 58, 15, 0, 5 },
    { 1, 22, 6, 8, 80, 15, 2, 4 }, { 2, 23, 7, 8, 40, 16, 1, 7 }, { 2, 23, 7, 8, 46, 16, 0, 5 },
    { 2, 23, 7, 8, 13, 16, 3, 2 }, { 2, 23, 7, 8, 16, 16, 1, 2 }, { 2, 23, 7, 8, 19, 16, 3, 2 },
    { 2, 23, 7, 8, 23, 16, 3, 2 }, { 2, 23, 7, 8, 47, 16, 3, 7 }, { 2, 23, 7, 8, 2, 16, 1, 3 },
    { 2, 23, 7, 8, 5, 16, 1, 3 },  { 2, 23, 7, 8, 9, 16, 1, 3 },  { 2, 23, 7, 8, 75, 16, 3, 7 },
    { 2, 23, 7, 8, 45, 16, 1, 5 }, { 2, 23, 7, 8, 51, 16, 2, 5 }, { 2, 23, 7, 8, 59, 16, 1, 5 },
    { 2, 23, 7, 8, 64, 16, 1, 5 }, { 2, 24, 7, 8, 11, 17, 3, 2 }, { 2, 24, 7, 8, 14, 17, 3, 2 },
    { 2, 24, 7, 8, 17, 17, 3, 2 }, { 2, 24, 7, 8, 21, 17, 3, 2 }, { 2, 24, 7, 8, 24, 17, 1, 2 },
    { 2, 24, 7, 8, 48, 17, 3, 7 }, { 2, 24, 7, 8, 3, 17, 1, 3 },  { 2, 24, 7, 8, 6, 17, 1, 3 },
    { 2, 24, 7, 8, 73, 17, 3, 7 }, { 2, 24, 7, 8, 49, 17, 1, 7 }, { 2, 24, 7, 8, 52, 17, 2, 5 },
    { 2, 24, 7, 8, 60, 17, 3, 5 }, { 2, 25, 7, 8, 12, 18, 3, 2 }, { 2, 25, 7, 8, 15, 18, 3, 2 },
    { 2, 25, 7, 8, 18, 18, 3, 2 }, { 2, 25, 7, 8, 22, 18, 3, 2 }, { 2, 25, 7, 8, 25, 18, 3, 2 },
    { 2, 25, 7, 8, 1, 18, 1, 3 },  { 2, 25, 7, 8, 4, 18, 1, 3 },  { 2, 25, 7, 8, 8, 18, 1, 3 },
    { 2, 25, 7, 8, 74, 18, 3, 7 }, { 2, 25, 7, 8, 44, 18, 1, 5 }, { 2, 25, 7, 8, 53, 18, 2, 5 },
    { 2, 25, 7, 8, 54, 18, 2, 5 }, { 2, 25, 7, 8, 61, 18, 1, 5 }, { 2, 23, 7, 8, 26, 16, 3, 2 },
    { 2, 24, 7, 8, 27, 17, 3, 2 }, { 2, 25, 7, 8, 28, 18, 3, 2 }, { 2, 23, 7, 8, 29, 16, 3, 2 },
    { 2, 24, 7, 8, 30, 17, 1, 2 }, { 2, 25, 7, 8, 31, 18, 3, 2 }, { 2, 24, 7, 8, 32, 17, 3, 2 },
    { 2, 24, 7, 8, 33, 17, 1, 2 }, { 2, 25, 7, 8, 34, 18, 3, 2 }, { 4, 26, 1, 8, 0, 1, 0, 0 },
    { 4, 27, 1, 8, 0, 2, 0, 0 },   { 4, 28, 1, 8, 0, 3, 0, 0 },   { 4, 28, 1, 8, 0, 3, 0, 0 },
    { 4, 27, 1, 8, 0, 2, 0, 0 },   { 4, 31, 2, 8, 0, 6, 0, 0 },   { 4, 32, 2, 8, 0, 7, 0, 0 },
    { 4, 33, 2, 8, 0, 8, 0, 0 },   { 4, 34, 3, 8, 0, 9, 0, 0 },   { 0, 36, 5, 8, 0, 11, 0, 0 },
    { 0, 37, 5, 8, 0, 12, 0, 0 },  { 0, 38, 5, 8, 0, 13, 0, 0 },  { 1, 39, 6, 8, 0, 14, 0, 0 },
    { 1, 40, 6, 8, 0, 15, 0, 0 },  { 2, 41, 7, 8, 0, 16, 0, 0 },  { 2, 42, 7, 8, 0, 17, 0, 0 },
    { 2, 43, 7, 8, 0, 18, 0, 0 },
};

const KinstoneWorldEvent gKinstoneWorldEvents_jp[] = {
    { 15, 44, 45, 8, 0, 0, 0, 0 }, { 4, 8, 1, 0, 0, 1, 2, 0 },    { 4, 9, 1, 0, 0, 2, 2, 0 },
    { 4, 10, 1, 0, 0, 3, 2, 0 },   { 4, 10, 1, 0, 0, 3, 2, 0 },   { 4, 9, 1, 0, 0, 2, 2, 0 },
    { 4, 13, 2, 0, 0, 6, 2, 0 },   { 4, 14, 2, 0, 0, 7, 2, 0 },   { 4, 15, 2, 0, 0, 8, 2, 0 },
    { 4, 16, 3, 0, 0, 9, 2, 0 },   { 0, 18, 5, 8, 20, 11, 3, 2 }, { 0, 18, 5, 8, 77, 11, 0, 4 },
    { 0, 18, 5, 8, 65, 11, 2, 5 }, { 0, 18, 5, 8, 7, 11, 1, 3 },  { 0, 18, 5, 8, 87, 11, 2, 0 },
    { 0, 18, 5, 8, 92, 11, 2, 8 }, { 0, 18, 5, 8, 37, 11, 0, 3 }, { 0, 18, 5, 8, 55, 11, 0, 5 },
    { 0, 18, 5, 8, 62, 11, 2, 5 }, { 0, 19, 5, 8, 63, 12, 0, 5 }, { 0, 19, 5, 8, 88, 12, 3, 4 },
    { 0, 19, 5, 8, 66, 12, 2, 5 }, { 0, 19, 5, 8, 10, 12, 3, 2 }, { 0, 20, 5, 8, 70, 13, 2, 6 },
    { 0, 19, 5, 8, 42, 12, 0, 5 }, { 0, 19, 5, 8, 38, 12, 0, 3 }, { 0, 19, 5, 8, 68, 12, 2, 6 },
    { 0, 20, 5, 8, 76, 13, 0, 4 }, { 0, 20, 5, 8, 91, 13, 3, 4 }, { 0, 20, 5, 8, 67, 13, 2, 5 },
    { 0, 20, 5, 8, 43, 13, 3, 5 }, { 0, 20, 5, 8, 41, 13, 0, 7 }, { 0, 20, 5, 8, 36, 13, 0, 3 },
    { 0, 20, 5, 8, 50, 13, 2, 7 }, { 1, 21, 6, 8, 39, 14, 0, 7 }, { 1, 21, 6, 8, 69, 14, 2, 6 },
    { 1, 21, 6, 8, 72, 14, 2, 6 }, { 1, 22, 6, 8, 82, 15, 2, 4 }, { 1, 22, 6, 8, 84, 15, 2, 4 },
    { 1, 21, 6, 8, 56, 14, 2, 5 }, { 1, 21, 6, 8, 78, 14, 2, 5 }, { 1, 21, 6, 8, 81, 14, 2, 4 },
    { 1, 21, 6, 8, 83, 14, 2, 4 }, { 1, 21, 6, 8, 85, 14, 2, 4 }, { 1, 21, 6, 8, 90, 14, 2, 4 },
    { 1, 22, 6, 8, 57, 15, 0, 5 }, { 1, 22, 6, 8, 71, 15, 2, 6 }, { 1, 22, 6, 8, 86, 15, 2, 4 },
    { 1, 22, 6, 8, 79, 15, 2, 5 }, { 1, 22, 6, 8, 89, 15, 2, 4 }, { 1, 22, 6, 8, 58, 15, 0, 5 },
    { 1, 22, 6, 8, 80, 15, 2, 4 }, { 2, 23, 7, 8, 40, 16, 1, 7 }, { 2, 23, 7, 8, 46, 16, 0, 5 },
    { 2, 23, 7, 8, 13, 16, 3, 2 }, { 2, 23, 7, 8, 16, 16, 1, 2 }, { 2, 23, 7, 8, 19, 16, 3, 2 },
    { 2, 23, 7, 8, 23, 16, 3, 2 }, { 2, 23, 7, 8, 47, 16, 3, 7 }, { 2, 23, 7, 8, 2, 16, 1, 3 },
    { 2, 23, 7, 8, 5, 16, 1, 3 },  { 2, 23, 7, 8, 9, 16, 1, 3 },  { 2, 23, 7, 8, 75, 16, 3, 7 },
    { 2, 23, 7, 8, 45, 16, 1, 5 }, { 2, 23, 7, 8, 51, 16, 2, 5 }, { 2, 23, 7, 8, 59, 16, 3, 5 },
    { 2, 23, 7, 8, 64, 16, 3, 5 }, { 2, 24, 7, 8, 11, 17, 3, 2 }, { 2, 24, 7, 8, 14, 17, 3, 2 },
    { 2, 24, 7, 8, 17, 17, 3, 2 }, { 2, 24, 7, 8, 21, 17, 3, 2 }, { 2, 24, 7, 8, 24, 17, 1, 2 },
    { 2, 24, 7, 8, 48, 17, 1, 7 }, { 2, 24, 7, 8, 3, 17, 1, 3 },  { 2, 24, 7, 8, 6, 17, 1, 3 },
    { 2, 24, 7, 8, 73, 17, 3, 7 }, { 2, 24, 7, 8, 49, 17, 1, 7 }, { 2, 24, 7, 8, 52, 17, 2, 5 },
    { 2, 24, 7, 8, 60, 17, 3, 5 }, { 2, 25, 7, 8, 12, 18, 3, 2 }, { 2, 25, 7, 8, 15, 18, 3, 2 },
    { 2, 25, 7, 8, 18, 18, 3, 2 }, { 2, 25, 7, 8, 22, 18, 3, 2 }, { 2, 25, 7, 8, 25, 18, 3, 2 },
    { 2, 25, 7, 8, 1, 18, 1, 3 },  { 2, 25, 7, 8, 4, 18, 1, 3 },  { 2, 25, 7, 8, 8, 18, 1, 3 },
    { 2, 25, 7, 8, 74, 18, 3, 7 }, { 2, 25, 7, 8, 44, 18, 1, 5 }, { 2, 25, 7, 8, 53, 18, 2, 5 },
    { 2, 25, 7, 8, 54, 18, 2, 5 }, { 2, 25, 7, 8, 61, 18, 3, 5 }, { 2, 23, 7, 8, 26, 16, 3, 2 },
    { 2, 24, 7, 8, 27, 17, 3, 2 }, { 2, 25, 7, 8, 28, 18, 3, 2 }, { 2, 23, 7, 8, 29, 16, 3, 2 },
    { 2, 24, 7, 8, 30, 17, 1, 2 }, { 2, 25, 7, 8, 31, 18, 3, 2 }, { 2, 24, 7, 8, 32, 17, 3, 2 },
    { 2, 24, 7, 8, 33, 17, 1, 2 }, { 2, 25, 7, 8, 34, 18, 3, 2 }, { 4, 26, 1, 8, 0, 1, 0, 0 },
    { 4, 27, 1, 8, 0, 2, 0, 0 },   { 4, 28, 1, 8, 0, 3, 0, 0 },   { 4, 28, 1, 8, 0, 3, 0, 0 },
    { 4, 27, 1, 8, 0, 2, 0, 0 },   { 4, 31, 2, 8, 0, 6, 0, 0 },   { 4, 32, 2, 8, 0, 7, 0, 0 },
    { 4, 33, 2, 8, 0, 8, 0, 0 },   { 4, 34, 3, 8, 0, 9, 0, 0 },   { 0, 36, 5, 8, 0, 11, 0, 0 },
    { 0, 37, 5, 8, 0, 12, 0, 0 },  { 0, 38, 5, 8, 0, 13, 0, 0 },  { 1, 39, 6, 8, 0, 14, 0, 0 },
    { 1, 40, 6, 8, 0, 15, 0, 0 },  { 2, 41, 7, 8, 0, 16, 0, 0 },  { 2, 42, 7, 8, 0, 17, 0, 0 },
    { 2, 43, 7, 8, 0, 18, 0, 0 },
};

// For sub_080A4418
// TODO these are gGlobalGfxAndPalettes offsets with the size of 0x80
const u32 gUnk_080CA06C[] = { 139808, 139808, 140320, 140832, 141344, 141856, 142368, 142880, 143904, 144928, 145952,
                              146976, 148000, 149024, 150048, 151072, 152096, 153120, 154144, 155168, 156192, 157216,
                              158240, 159264, 160288, 161312, 143392, 144416, 145440, 146464, 147488, 148512, 149536,
                              150560, 151584, 152608, 153632, 154656, 155680, 156704, 157728, 158752, 159776, 160800 };

// Runtime twin: baseline above is the USA body. Select at the reader when REGION_IS_EU.
const u32 gUnk_080CA06C_eu[] = {
    139744, 139744, 140256, 140768, 141280, 141792, 142304, 142816, 143840, 144864, 145888,
    146912, 147936, 148960, 149984, 151008, 152032, 153056, 154080, 155104, 156128, 157152,
    158176, 159200, 160224, 161248, 143328, 144352, 145376, 146400, 147424, 148448, 149472,
    150496, 151520, 152544, 153568, 154592, 155616, 156640, 157664, 158688, 159712, 160736
};

const u8 SharedFusions[] = {
    0x18, 0x2D, 0x35, 0x36, 0x37, 0x39, 0x3C, 0x44, 0x46, 0x47, 0x4E, 0x50, 0x53, 0x55, 0x56, 0x58, 0x5F, 0x60, 0, 0,
};

// Get a random kinstone
u32 GetRandomSharedFusion(u8* fuserData) {
    s32 r = (s32)Random() % 18;
    u32 i;
    for (i = 0; i < 18; ++i) {
        u32 kinstoneId = SharedFusions[r];
        if (!CheckKinstoneFused(kinstoneId))
            return kinstoneId;
        r = (r + 1) % 18;
    }
    return KINSTONE_JUST_FUSED;
}

#ifdef PC_PORT
/* Second-screen raw-data accessors (port/port_second_screen_dungeonmap.c).
 * Same rationale as Port_GetRawPaletteGroupData next to LoadPaletteGroup
 * above — the ROM tables and their entry types (gPaletteGroups/PaletteGroup,
 * gGfxGroups/GfxItem, the room-header helper) are file-private here, so the
 * raw readers live in this translation unit too. Appended at the end of the
 * file, and kept append-only, so hand-merging against upstream stays a
 * non-event. */

/* Raw RGB555 colors LoadPaletteGroup would put in destination palette bank
 * `destPaletteNum` (0-15 BG, 16-31 OBJ) for `group`, straight from ROM and
 * independent of the live gPaletteBuffer. Walks the whole chained group —
 * Port_GetRawPaletteGroupData above resolves only the first entry, which
 * isn't enough for pause-menu groups whose interesting banks sit in later
 * chain links (e.g. group 184's dungeon-map bank 12). Later entries win on
 * overlap, matching load order. Returns NULL when no entry covers the bank;
 * outNumColors is the 16 colors of that one bank. */
const u8* Port_GetRawPaletteGroupBankData(u32 group, u32 destPaletteNum, u32* outNumColors) {
    const PaletteGroup* paletteGroup;
    const u8* found = NULL;
    u32 guard;

    if (outNumColors != NULL) {
        *outNumColors = 0;
    }
    if (group >= 208) { /* PALETTE_GROUPS_COUNT_MAX (port_rom.c) */
        return NULL;
    }
    paletteGroup = gPaletteGroups[group];
    if (paletteGroup == NULL) {
        return NULL;
    }
    for (guard = 0; guard < 64; guard++) {
        u32 pg = ROM_U32(*(const u32*)paletteGroup);
        u32 dest = (pg >> 16) & 0xFF;
        u32 numPalettes = (pg >> 24) & 0xF;
        if (numPalettes == 0) {
            numPalettes = 16;
        }
        if (destPaletteNum >= dest && destPaletteNum < dest + numPalettes) {
            found = &gGlobalGfxAndPalettes[((pg & 0xFFFF) + (destPaletteNum - dest)) * 32];
        }
        if (((pg >> 24) & 0x80) == 0) {
            break;
        }
        paletteGroup++;
    }
    if (found != NULL && outNumColors != NULL) {
        *outNumColors = 16;
    }
    return found;
}

/* Raw bytes gfx group `group` would DMA to VRAM address `vramAddr`
 * (LoadGfxGroup's uncompressed path), provided [vramAddr, vramAddr+numBytes)
 * sits fully inside one entry. Only unconditional entries (ctrl 0x7) count —
 * the language-gated variants would need gSaveHeader, i.e. live save state,
 * which these accessors deliberately never read — and LZ77 entries (negative
 * size) are skipped to keep this a pure pointer lookup. Later entries win on
 * overlap, matching load order. */
const u8* Port_GetRawGfxSpanForVram(u32 group, u32 vramAddr, u32 numBytes) {
    const GfxItem* gfxItem;
    const u8* found = NULL;
    u32 guard;

    if (group >= 133) { /* GFX_GROUPS_COUNT_MAX (port_rom.c) */
        return NULL;
    }
    gfxItem = gGfxGroups[group];
    if (gfxItem == NULL) {
        return NULL;
    }
    for (guard = 0; guard < 64; guard++) {
        u32 gi = ROM_U32(gfxItem->unk0.raw);
        u32 ctrl = (gi >> 24) & 0xF;
        u32 dest = ROM_U32(gfxItem->dest);
        s32 size = (s32)ROM_U32(gfxItem->unk8);
        if (ctrl == 0xD) { /* hard list end, as in LoadGfxGroup */
            break;
        }
        if (ctrl == 0x7 && size > 0 && dest <= vramAddr && vramAddr + numBytes <= dest + (u32)size) {
            found = &gGlobalGfxAndPalettes[(gi & 0xFFFFFF) + (vramAddr - dest)];
        }
        if (((gi >> 24) & 0x80) == 0) {
            break;
        }
        gfxItem++;
    }
    return found;
}

/* Room header (map placement + size) for (area, room), bounds- and
 * ROM-pointer-checked. Common_GetAreaRoomHeaderSafe is static — this is its
 * exported face for the port layer. */
const RoomHeader* Port_GetRoomHeaderSafe(u32 area, u32 room) {
    return Common_GetAreaRoomHeaderSafe(area, room);
}

/* Second-screen theme accessors (port_second_screen_theme.c) — like
 * Port_GetRawPaletteGroupData above, these live here because
 * gPaletteGroups/PaletteGroup and gGfxGroups/GfxItem are file-private to
 * this translation unit. Read-only walks over ROM-const tables; no live
 * engine state is touched, so they are safe from the second-screen render
 * thread once the ROM tables are resolved (NULL before that). */

/* Raw RGB555 colors of one entry of a palette group's chain (entry 0 is
 * what Port_GetRawPaletteGroupData returns). Needed because pause-menu
 * palette groups chain several entries at different dest banks (e.g. group
 * 182: BG chrome banks first, OBJ icon banks second). Returns NULL and
 * zeroes the outputs when the group/entry doesn't exist. */
const u8* Port_GetRawPaletteGroupEntryData(u32 group, u32 entryIdx, u32* outNumColors, u32* outDestPaletteNum) {
    const PaletteGroup* paletteGroup = gPaletteGroups[group];
    if (outNumColors) {
        *outNumColors = 0;
    }
    if (outDestPaletteNum) {
        *outDestPaletteNum = 0;
    }
    if (paletteGroup == NULL) {
        return NULL;
    }
    while (1) {
        u32 pg = ROM_U32(*(const u32*)paletteGroup);
        u32 numPalettes = (pg >> 24) & 0xF;
        if (numPalettes == 0) {
            numPalettes = 16;
        }
        if (entryIdx == 0) {
            if (outNumColors) {
                *outNumColors = numPalettes * 16;
            }
            if (outDestPaletteNum) {
                *outDestPaletteNum = (pg >> 16) & 0xFF;
            }
            return &gGlobalGfxAndPalettes[(pg & 0xFFFF) * 32];
        }
        if (((pg >> 24) & 0x80) == 0) {
            return NULL; /* chain ended before entryIdx */
        }
        paletteGroup++;
        entryIdx--;
    }
}

/* Resolves a GBA VRAM address to the ROM bytes a gfx group would load
 * there, without loading anything: walks gGfxGroups[group] like
 * LoadGfxGroup and returns a pointer into gGlobalGfxAndPalettes for the
 * first uncompressed entry whose dest span covers vramAddr (outAvail =
 * bytes remaining in that span). Language-conditional entries are treated
 * as matching regardless of language — the chrome/HUD tile entries the
 * second screen needs are all unconditional (ctrl 7), and skipping the
 * gSaveHeader read keeps this ROM-const for the render thread. Returns
 * NULL for compressed entries and unresolved groups. */
const u8* Port_ResolveGfxGroupVram(u32 group, u32 vramAddr, u32* outAvail) {
    const GfxItem* gfxItem;
    if (outAvail) {
        *outAvail = 0;
    }
    if (group >= 133) {
        return NULL;
    }
    gfxItem = gGfxGroups[group];
    if (gfxItem == NULL || gGlobalGfxAndPalettes == NULL) {
        return NULL;
    }
    while (1) {
        u32 gi = ROM_U32(gfxItem->unk0.raw);
        u32 ctrl = (gi >> 24) & 0xF;
        u32 terminator = (gi >> 24) & 0x80;
        if (ctrl == 0xD) {
            return NULL;
        }
        {
            u32 dest = ROM_U32(gfxItem->dest);
            s32 size = (s32)ROM_U32(gfxItem->unk8);
            if (size > 0 && vramAddr >= dest && vramAddr < dest + (u32)size) {
                if (outAvail) {
                    *outAvail = dest + (u32)size - vramAddr;
                }
                return &gGlobalGfxAndPalettes[(gi & 0xFFFFFF) + (vramAddr - dest)];
            }
        }
        gfxItem++;
        if (!terminator) {
            break;
        }
    }
    return NULL;
}

/* A kinstone piece is 0x80 words of 4bpp tiles (16 tiles, a 32x32 block),
 * which is the fixed size sub_080A4418 DMAs. */
#define KINSTONE_PIECE_BYTES (0x80u * sizeof(u32))
#define KINSTONE_PIECE_GFX_COUNT ARRAY_COUNT(gUnk_080CA06C)

/* One kinstone piece's OBJ tiles, decoded into the caller's buffer.
 *
 * The kinstone menus don't load these with a gfx group — sub_080A4418
 * (src/menu/kinstoneMenu.c) streams the block named by
 * gKinstoneWorldEvents[id].gfxOffsetPiece straight into an OBJ VRAM slot it
 * picked at runtime, so there is no group to resolve and the tiles have to
 * be produced the same way here. gUnk_080CA06C's entries are offsets into
 * gGlobalGfxAndPalettes with the top bit meaning LZ77-compressed, exactly as
 * that function reads them; a whole piece is 0x80 words either way.
 *
 * Returns the bytes written (KINSTONE_PIECE_BYTES on success, 0 when the
 * offset is out of range, the ROM tables aren't resolved yet, or the buffer
 * is too small). ROM-const reads only, so the second-screen render thread
 * may call it once the ROM is up. */
u32 Port_GetKinstonePieceTiles(u32 gfxOffset, u8* out, u32 outBytes) {
    const u32* table = gUnk_080CA06C;
    u32 entry, compressed;
    extern const u32 gUnk_080CA06C_eu[];

    if (out == NULL || outBytes < KINSTONE_PIECE_BYTES || gGlobalGfxAndPalettes == NULL) {
        return 0;
    }
    if (REGION_IS_EU) {
        table = gUnk_080CA06C_eu;
    }
    if (table == NULL || gfxOffset >= KINSTONE_PIECE_GFX_COUNT) {
        return 0;
    }
    entry = ROM_U32(table[gfxOffset]);
    compressed = entry & 0x80000000u;
    if (compressed) {
        /* LZ77UnCompVram writes 16 bits at a time and would run past the end
         * on a malformed header, so the size is checked before unpacking. */
        const u8* src = &gGlobalGfxAndPalettes[entry & ~0x80000000u];
        u32 unpacked = (ROM_U32(*(const u32*)src) >> 8) & 0xFFFFFFu;
        if (unpacked == 0 || unpacked > outBytes) {
            return 0;
        }
        LZ77UnCompVram((void*)src, out);
        return unpacked;
    }
    MemCopy((void*)&gGlobalGfxAndPalettes[entry], out, KINSTONE_PIECE_BYTES);
    return KINSTONE_PIECE_BYTES;
}
#endif
