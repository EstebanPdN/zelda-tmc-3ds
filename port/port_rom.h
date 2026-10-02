#pragma once
#include <string.h>
#include "port_config.h"
#include "port_types.h"
#include "structures.h"
#include "map.h"

// ROM data buffer
extern u8* gRomData;
extern u32 gRomSize;

#ifdef PC_PORT
/*
 * Host-pointer plausibility guard: reject NULL,
 * low/half-pointer-write garbage, raw GBA addresses that leaked through
 * unconverted, kernel-space, and sign-extended negatives; accept anything
 * that could be a live host allocation. Use to gate a deref on paths that
 * (unlike IsColliding) have no other range guard — e.g. the player
 * interactable scan in src/playerUtils.c::sub_080784E4.
 *
 * Bounds are per-ABI, NOT per-distro:
 *  - 3DS: query the ARM11 memory map. Persistent engine pointers cannot live
 *    in the numeric GBA address window; accepting that window would turn raw
 *    GBA addresses into host pointers because the application stack overlaps
 *    cartridge space. The separately allocated ROM buffer is accepted first.
 *  - Windows: user mode is the low 128 TB; heap can sit as low as ~0x10000.
 *  - Other 64-bit (Linux/Android/macOS): accept (4 GiB, 2^48). The old
 *    lower bound of 2^44 was an x86_64-Linux-only artifact — Android
 *    aarch64 commonly runs a 39-bit VA kernel (all pointers < 2^39!), so
 *    that bound rejected EVERY valid pointer on device: NPC talk, combat
 *    collision, and animation-range checks all silently failed. >4 GiB
 *    still rejects NULL/garbage/GBA addresses; PIE binaries and mmap on
 *    all supported targets live above 4 GiB.
 */
static inline int Port_IsValidHostPtr(const void* p) {
    uintptr_t a = (uintptr_t)p;
#if defined(TMC_3DS)
    extern int Platform3DS_IsNativeAddress(uintptr_t value);
    uintptr_t rom = (uintptr_t)gRomData;
    if (p == NULL)
        return 0;
    if (gRomData != NULL && a >= rom && a < rom + (uintptr_t)gRomSize)
        return 1;
    if (a >= 0x02000000u && a < 0x0A000000u)
        return 0;
    return Platform3DS_IsNativeAddress(a);
#elif defined(_WIN32)
    return a >= 0x10000ULL && a < 0x800000000000ULL;
#else
    return a > 0xFFFFFFFFULL && a < 0x1000000000000ULL;
#endif
}
#endif /* PC_PORT */

// Load the ROM file and set up ROM-backed symbols
void Port_LoadRom(const char* path);

/*
 * Probe the same candidate locations Port_LoadRom would and return a
 * pointer to a static buffer holding the absolute path of the first
 * reachable ROM file, or NULL if none of the known candidate names
 * are openable. Probe order matches Port_LoadRom's load order so a
 * successful probe guarantees a successful load. Intended for the
 * pre-window check in port_main.c so we can show an SDL message box
 * (and exit cleanly) before any window is created.
 */
const char* Port_FindBaseRomPath(void);
const char* Port_GetLoadedRomPath(void);

/* Surface a fatal ROM error as a stderr line + SDL message box, then exit.
 * Safe before SDL_CreateWindow (NULL parent). Reused by the region
 * cross-check in port_main.c so a mismatch is visible in the GUI, not just
 * on stderr. */
void Port_FatalRomError(const char* title, const char* message);

// Re-resolve a single area's room/tile/property tables from immutable ROM offsets.
void Port_RefreshAreaData(u32 area);

bool32 Port_IsAreaTablePtrReadable(u32 area, const void* ptr);

// ROM access logging - logs unique ROM addresses accessed at runtime
void Port_LogRomAccess(u32 gba_addr, const char* caller);
void Port_PrintRomAccessSummary(void);

/*
 * Read a packed 32-bit GBA ROM pointer from a base address at the given index.
 * On GBA, pointer tables store 4-byte pointers; on 64-bit PC, sizeof(void*)==8,
 * so we can't index them directly.  This reads 4 bytes at base + index*4,
 * resolves ROM data pointers to native, and returns NULL for GBA Thumb function
 * pointers (bit 0 set) which can't be called on PC.
 */
void* Port_ReadPackedRomPtr(const void* base, u32 index);

/* Read one entry from a region-selected packed pointer table in the active
 * ROM. tableOffset is a RomOffsets field, not a compiled USA symbol. */
void* Port_ReadActiveRomPtrTable(u32 tableOffset, u32 index);

/**
 * Resolve a GBA ROM data address to a native PC pointer.
 * Returns &gRomData[gba_addr - 0x08000000] for valid ROM addresses, NULL otherwise.
 */
static inline void* Port_ResolveRomData(u32 gba_addr) {
    if (gba_addr >= 0x08000000u && gba_addr < 0x08000000u + gRomSize)
        return &gRomData[gba_addr - 0x08000000u];
    return NULL;
}

/*
 * Translate a baked USA script ROM address (the GBA_script_* constants in
 * port_scripts.h) to the active region's retail address. Identity for USA and
 * for unknown addresses. Defined in the generated port_script_addrs.c.
 *
 * Only the port's own compiled-in USA script constants need translating —
 * addresses read out of the loaded ROM's own bytecode are already in the
 * active region's address space and must NOT be passed through this.
 */
u32 Port_TranslateScriptAddr(u32 gba_addr);

/*
 * Resolve a baked USA script ROM address to a native PC pointer, translating it
 * to the active region first. Use this (not Port_ResolveRomData) for the
 * port-injected GBA_script_* addresses: PORT_SCRIPT(), ENTITY_SCRIPT storage
 * resolved in sub_0804AF0C, and the gForestMinishScriptGBAAddrs table.
 */
static inline void* Port_ResolveScript(u32 gba_addr) {
    return Port_ResolveRomData(Port_TranslateScriptAddr(gba_addr));
}

/*
 * Provenance-aware script resolve for EntityData::spritePtr. EntityData can be
 * (a) a compiled C table / data_const_stubs.c blob — spritePtr is a baked USA
 * script address that must be translated — or (b) an entity list resolved out
 * of the loaded ROM — spritePtr is already region-native and translating it
 * MIS-translates whenever a native EU/JP address collides with a different
 * script's USA key (30 EU / 5 JP known collisions). Discriminate by where the
 * EntityData record itself lives.
 */
static inline void* Port_ResolveEntityScript(const void* entityData, u32 spritePtr) {
    uintptr_t p = (uintptr_t)entityData;
    uintptr_t base = (uintptr_t)gRomData;
    if (gRomData && p >= base && p < base + gRomSize)
        return Port_ResolveRomData(spritePtr); /* ROM-native: no translation */
    return Port_ResolveScript(spritePtr);      /* compiled blob: USA-baseline */
}

/**
 * Read entry [idx] from a packed-GBA-pointer table stored as a raw u8 array.
 *
 * Many `gUnk_08xxxxxx` tables in port/data_const_stubs.c are byte arrays of
 * 4-byte GBA pointers. Game code declares them externally as `T*[]` so
 * `arr[idx]` reads sizeof(T*) bytes — fine on the 32-bit GBA, broken on
 * x86-64 (reads 8 bytes, gets two GBA addresses concatenated → garbage).
 *
 * Use this helper at PC call sites to manually unpack the 4-byte entry and
 * resolve to a native pointer via the ROM mmap. (#16, #19 root cause.)
 */
static inline void* Port_PackedRomEntry(const void* base, u32 idx) {
    u32 raw;
    memcpy(&raw, (const u8*)base + idx * 4, 4);
    return Port_ResolveRomData(raw);
}

static inline u16 Port_ReadU16(const void* data) {
    const u8* raw = (const u8*)data;
    return (u16)(raw[0] | (raw[1] << 8));
}

static inline u32 Port_ReadU32(const void* data) {
    const u8* raw = (const u8*)data;
    return (u32)raw[0] | ((u32)raw[1] << 8) | ((u32)raw[2] << 16) | ((u32)raw[3] << 24);
}

/* Resolve one entry of the ROM's packed collision-mask pointer table. Each
 * target is a 16-row u16 bitmap. This helper is deliberately buffer-based so
 * region selection can be regression-tested without a retail ROM. */
static inline const u16* Port_ResolveCollisionShapeFromRom(const u8* romData, u32 romSize, u32 tableOffset,
                                                           u32 index) {
    u32 entryBytes;
    u32 gbaAddress;
    u32 dataOffset;

    if (romData == NULL || index >= 40u || tableOffset > romSize) {
        return NULL;
    }
    entryBytes = (index + 1u) * sizeof(u32);
    if (entryBytes > romSize - tableOffset) {
        return NULL;
    }
    gbaAddress = Port_ReadU32(romData + tableOffset + index * sizeof(u32));
    if (gbaAddress < 0x08000000u) {
        return NULL;
    }
    dataOffset = gbaAddress - 0x08000000u;
    if ((dataOffset & 1u) != 0 || dataOffset > romSize || 16u * sizeof(u16) > romSize - dataOffset) {
        return NULL;
    }
    return (const u16*)(romData + dataOffset);
}

/* Read one u16 traversal/layer-property record from a region-native ROM
 * table.  USA places this table at 0x360; EU moves it to 0x3A8 because its
 * startup pointer block is larger. */
static inline u16 Port_ReadTileTypePropertyFromRom(const u8* romData, u32 romSize, u32 tableOffset, u32 tileType) {
    u32 entryOffset;

    if (romData == NULL || tableOffset > romSize || tileType > (UINT32_MAX - tableOffset) / sizeof(u16)) {
        return 0;
    }
    entryOffset = tableOffset + tileType * sizeof(u16);
    if (entryOffset > romSize || sizeof(u16) > romSize - entryOffset) {
        return 0;
    }
    return Port_ReadU16(romData + entryOffset);
}

/*
 * Read entry [index] from a packed-GBA-pointer ROM table and resolve to
 * a native pointer. Equivalent to Port_PackedRomEntry but kept for the
 * call sites that game code uses (matheo/master); the two helpers exist
 * because they were introduced in parallel branches before being merged.
 */
static inline void* Port_UnpackRomDataPtr(const void* table, u32 index) {
    return Port_ResolveRomData(Port_ReadU32((const u8*)table + index * 4));
}

void* Port_ResolveAreaTileSetFromRom(u32 area, u32 tileSetId);
void* Port_ResolveAreaRoomMapFromRom(u32 area, u32 room);
void* Port_ResolveAreaPropertiesFromRom(u32 area, u32 room);
void* Port_ResolveAreaExitsFromRom(u32 area, u32 room);

/* Translate an offset in the original 32-bit GBA MapLayer layout to the
 * corresponding offset in the native build. The only layout difference is
 * the leading bgSettings pointer: four bytes on GBA/3DS and eight on PC64. */
static inline size_t Port_MapLayerNativeOffset(u32 gbaOffset) {
    const size_t gbaPointerSize = sizeof(u32);
    if (gbaOffset < gbaPointerSize) {
        return gbaOffset;
    }
    return (size_t)gbaOffset + offsetof(MapLayer, mapData) - gbaPointerSize;
}

/*
 * Resolve a raw GBA EWRAM address (0x02xxxxxx) to a native PC pointer.
 *
 * On GBA, globals like gMapBottom/gMapTop live in EWRAM at fixed addresses.
 * On PC, they are standalone C globals NOT inside the gEwram[] buffer.
 * gba_TryMemPtr(0x02xxxxxx) returns &gEwram[offset], which is WRONG for them.
 *
 * This function checks for known EWRAM globals first, applying struct-layout
 * adjustments where needed (e.g. MapLayer's bgSettings pointer is 4 bytes on
 * GBA/3DS but 8 on 64-bit PC). Falls back to gba_TryMemPtr for unknown
 * addresses.
 */
void* Port_ResolveEwramPtr(u32 gba_addr);

/*
 * Decode a GBA-format Font struct (24 bytes, 32-bit pointers) into
 * a native Font struct (with 64-bit pointers, properly resolved).
 *
 * GBA Font layout (24 bytes):
 *   [0..3]  u32 dest        (EWRAM/BG buffer pointer)
 *   [4..7]  u32 gfx_dest    (VRAM pointer)
 *   [8..11] u32 buffer_loc  (EWRAM pointer)
 *   [12..15] u32 _c
 *   [16..17] u16 gfx_src
 *   [18] u8 width
 *   [19] u8 bitfield (right_align:1, sm_border:1, unused:1, draw_border:1, border_type:4)
 *   [20] u8 fill_type
 *   [21] u8 charColor
 *   [22] u8 _16
 *   [23] u8 stylized
 */
void Port_DecodeFontGBA(const void* gba_data, Font* out);

/*
 * Detect if a Font pointer actually points to a raw 24-byte GBA blob
 * rather than a native 64-bit Font struct.
 *
 * Heuristic: bytes [4..7] would be the high 32 bits of a native pointer,
 * which on x86_64 is always in the 0x00000000-0x00007FFF range.
 * For GBA data, bytes [4..7] are gfx_dest (VRAM: 0x06xxxxxx),
 * which falls in the 0x02000000-0x07FFFFFF range.
 */
static inline bool Port_IsFontGBAEncoded(const void* data) {
    const u8* raw = (const u8*)data;
    u32 word1 = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);
    return (word1 >= 0x02000000u && word1 < 0x08000000u);
}

/*
 * Return a stable, ROM-resolved SpritePtr entry for the given sprite index.
 * Returns NULL if the index is outside the loaded sprite table.
 */
const SpritePtr* Port_GetSpritePtr(u16 sprite_idx);

/*
 * Remap a sprite index used by fixed UI/menu tables for EU ROM layout quirks.
 * EU item sprite 322 is shifted to 321 and HUD-button sprite 505 to 504,
 * while gameplay sprite indices remain active-ROM-native. Only call this from
 * fixed item/HUD/menu paths.
 */
u16 Port_RemapSpriteIndex(u16 sprite_idx);

/* Resolve one 16x16 pixel-level collision mask through the active ROM's own
 * packed pointer table. This is region-sensitive even though the mask
 * payloads themselves are region-invariant. */
const u16* Port_GetCollisionShapeData(u32 index);
u16 Port_GetTileTypeProperty(u32 tileType);

/*
 * Decode one MapDataDefinition entry into a native-layout struct.
 *
 * GBA layout: 12 bytes packed {u32 src, u32 dest_gba_addr, u32 size}.
 * Native layout (64-bit PC): 24 bytes with pointer-widening padding.
 *
 * Sniffs whether the input lies inside gRomData. If yes, unpacks via
 * Port_ReadU32 and resolves dest via Port_ResolveEwramPtr.  If no, copies
 * native layout directly with memcpy. The dest pointer in `out` is always
 * a valid native pointer (or NULL when unmapped).
 */
static inline void Port_DecodeMapDataDefinition(const void* entry, MapDataDefinition* out) {
    const u8* raw = (const u8*)entry;
    if (gRomData && raw >= gRomData && raw < gRomData + gRomSize) {
        u32 dest_gba = Port_ReadU32(raw + 4);
        out->src = Port_ReadU32(raw + 0);
        out->dest = dest_gba ? Port_ResolveEwramPtr(dest_gba) : NULL;
        out->size = Port_ReadU32(raw + 8);
    } else {
        memcpy(out, entry, sizeof *out);
    }
}

void Port_ApplyLanguage(void);
