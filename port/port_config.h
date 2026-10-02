/*
 * port_config.h — ROM region detection and offset configuration.
 *
 * Supports USA (BZME), EU (BZMP), and JP (BZMJ) ROMs with runtime
 * auto-detection. All region-specific ROM offsets are stored in RomOffsets
 * and selected at load time based on the game code at ROM offset 0xAC.
 *
 * JP NOTE: the JP path is wired but its offset values are ROM-derived and
 * cannot be produced without a JP baserom (BZMJ, matching tmc_jp.sha1) and a
 * JP build. kRomOffsets_JP below is a placeholder until then — see
 * docs/JP_PORT_ENABLEMENT.md. A JP build also requires the generated
 * port_offset_JP.h, so it will not compile until that file exists.
 */
#pragma once
#include "port_types.h"

/* ---- ROM region enum ---- */
typedef enum {
    ROM_REGION_UNKNOWN = 0,
    ROM_REGION_USA, /* Game code: BZME */
    ROM_REGION_EU,  /* Game code: BZMP */
    ROM_REGION_JP,  /* Game code: BZMJ — offsets UNVERIFIED (ROM-gated) */
} RomRegion;

/* The checked-in area-table asset index is generated from the USA layout.
 * Other regions must keep the active ROM as the authority: identical logical
 * room properties can live at different offsets, and USA-indexed extraction
 * can otherwise turn valid regional data slots into NULL callbacks/data. */
static inline u32 Port_ShouldUseAreaAssetCacheForRegion(RomRegion region) {
    return region == ROM_REGION_USA;
}

/* Packed u32 pointers to the 40 pixel-level collision masks used by
 * sub_080086D8. The mask payloads are identical between USA and EU, but the
 * EU linker moves both this table and every target by 0x98 bytes. */
#define PORT_COLLISION_SHAPE_PTRS_USA 0x0000823Cu
#define PORT_COLLISION_SHAPE_PTRS_EU 0x000082D4u

/* u16 tile traversal/layer properties read by sub_080B1B84/1BA4.  The
 * payload is identical, but EU relocates it after a larger startup pointer
 * block. */
#define PORT_TILE_TYPE_PROPERTIES_USA 0x00000360u
#define PORT_TILE_TYPE_PROPERTIES_EU 0x000003A8u

/* Fixed UI definitions are compiled from the USA baseline in the fat binary.
 * EU removes one sprite-table entry before both the shared item sheet and the
 * HUD button sheet. Gameplay entity indices remain active-ROM-native and
 * must not pass through this helper. */
static inline u16 Port_RemapFixedUiSpriteIndexForRegion(RomRegion region, u16 spriteIndex) {
    if (region == ROM_REGION_EU && (spriteIndex == 322u || spriteIndex == 505u)) {
        return spriteIndex - 1u;
    }
    return spriteIndex;
}

/* ---- Region-specific ROM symbol offsets ---- */
typedef struct {
    /* Major data tables (absolute ROM offsets, i.e. GBA_addr - 0x08000000) */
    u32 gfxAndPalettes;   /* gGlobalGfxAndPalettes */
    u32 gfxGroups;        /* gGfxGroups pointer table */
    u32 paletteGroups;    /* gPaletteGroups pointer table */
    u32 objPalettes;      /* OBJ palette offset table */
    u32 frameObjLists;    /* gFrameObjLists sprite frame data */
    u32 extraFrameOffsets; /* gExtraFrameOffsets multipart-sprite positioning */
    u32 fixedTypeGfx;     /* gFixedTypeGfxData */
    u32 spritePtrs;       /* gSpritePtrs */
    u32 collisionMatrix;  /* gCollisionMtx and adjacent collision settings */
    u32 collisionShapePtrs; /* packed pointers to 16x16 pixel-level collision masks */
    u32 tileTypeProperties; /* gUnk_08000360: u16 traversal/layer flags by tile type */
    u32 figurines;        /* gFigurines packed ROM table */
    u32 fuserEnemyData;   /* GetFuserData enemy table */
    u32 fuserNpcData;     /* GetFuserData NPC table */
    u32 lakeHyliaEnemies; /* Lake Hylia default entity list */
    u32 lakeHyliaCleared; /* Lake Hylia post-dungeon entity list */
    u32 lilypadRails;     /* gLilypadRails pointer table */
    u32 songTable;        /* gSongTable */
    u32 translations;     /* gTranslations */
    u32 text09230;        /* font/text pointer table 1 */
    u32 text09244;        /* font/text raw data 1 */
    u32 text09248;        /* font/text glyph table */
    u32 text0926C;        /* font/text raw data 2 */
    u32 text092AC;        /* font/text border glyphs */
    u32 text092D4;        /* font/text raw data 3 */
    u32 text0942E;        /* font/text raw data 4 */
    u32 text094CE;        /* font/text raw data 5 */
    u32 uiData;           /* UI misc data */
    u32 fadeData;         /* brightness/fade tables */
    u32 overlaySizeTable; /* OBJ size/clipping table */
    u32 mapDataBase;      /* gAreaRoomMap_None — base of map/asset data section */

    /* Area data tables (pointer tables indexed by area ID) */
    u32 areaRoomHeaders;   /* gAreaRoomHeaders — pointer table to RoomHeader arrays (0x90 entries) */
    u32 areaTileSets;      /* gAreaTileSets — pointer table (ptr → ptr) (0x40 entries) */
    u32 areaTileSetsCount; /* number of entries in gAreaTileSets (0x90 for both regions) */
    u32 areaRoomMaps;      /* gAreaRoomMaps — pointer table (ptr → ptr) (0x90 entries) */
    u32 areaTable;         /* gAreaTable — pointer table (ptr → ptr) (0x90 entries) */
    u32 areaTiles;         /* gAreaTiles — pointer table (ptr) (0x90 entries) */
    u32 exitLists;         /* gExitLists — pointer table (ptr → ptr) (0x90 entries) */
    u32 bgAnimTable;       /* gUnk_080B755C — pointer table (ptr) */
    u32 localFlagBanks;    /* gLocalFlagBanks — u16 array (raw data) */
    u32 townspersonSpriteLoadPtrs; /* gUnk_0810B6EC — SpriteLoadData* table (21 entries) for
                                    * Hyrule-Town NPCs. Region-relocated; a USA-pinned offset
                                    * yields all-NULL ptrs on JP/EU → NULL deref crash on town entry. */
    u32 guardPatrolData;    /* gUnk_0810F6BC — packed pointers for non-scripted guard behavior */
    u32 innWestEntities;    /* gUnk_080D6A74 — packed room-entity-list pointers */
    u32 innMiddleEntities;  /* gUnk_080D6B18 — packed room-entity-list pointers */
    u32 innEastEntities;    /* gUnk_080D6BB8 — packed room-entity-list pointers */
    u32 simonEntityLists;   /* gUnk_080F0CB8 — packed simulation entity-list pointers */
    u32 simonEnemyPatterns; /* gUnk_080F0D58 — packed simulation pattern pointers */
    u32 simonChestPatterns; /* gUnk_080F0E08 — packed simulation chest-pattern pointers */
    u32 gustJarAnimTable;   /* gUnk_08132714 — packed animation-data pointers */
    u32 gustJarHitbox;      /* gUnk_08132B28 — Hitbox */

    /* Table counts (same for both regions, but kept per-region for safety) */
    u32 gfxGroupsCount;
    u32 paletteGroupsCount;
    u32 objPalettesCount;
    u32 frameObjListsSize;
    u32 fixedTypeGfxCount;
    u32 spritePtrsCount;

    /* ROM expected size */
    u32 expectedRomSize;

    /* Game code for verification (4 chars) */
    char gameCode[5];
} RomOffsets;

/* ---- Global state ---- */
extern RomRegion gRomRegion;
extern const RomOffsets* gRomOffsets;

/* ---- Predefined offset tables ---- */
extern const RomOffsets kRomOffsets_USA;
extern const RomOffsets kRomOffsets_EU;
/* JP table is a placeholder until generated from build/JP/tmc_jp.map — its
 * ROM-derived address fields are 0. Port_DetectRomRegion refuses to run a JP
 * build against it while unpopulated. See docs/JP_PORT_ENABLEMENT.md. */
extern const RomOffsets kRomOffsets_JP;

/* Detect region from loaded ROM data and set gRomRegion + gRomOffsets.
 * Returns the detected region. */
RomRegion Port_DetectRomRegion(const u8* romData, u32 romSize);
