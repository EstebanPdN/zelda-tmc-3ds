
#include "area.h"
#include "backgroundAnimations.h"
#include "beanstalkSubtask.h"
#include "collision.h"
#include "color.h"
#include "common.h"
#include "port_rng.h"
#include "entity.h"
#include "enemy.h"
#include "fade.h"

#define gMapDataBottomSpecial gMapDataBottomSpecial_HIDDEN
#include "fileselect.h"
#undef gMapDataBottomSpecial
#include "hitbox.h"
#include "kinstone.h"
#include "main.h"
#include "map.h"
#include "manager/diggingCaveEntranceManager.h"
#include "menu.h"
#include "message.h"
#include "npc.h"
#include "physics.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "screen.h"
#include "script.h"
#include "sound.h"
#include "structures.h"

#include "port_entity_ctx.h"
#include "port_collision_fidelity.h"
#include "port_gba_mem.h"
#include "port_rom.h"
#include "port_runtime_config.h"
#include "port_widescreen.h"

#include <string.h>

extern u32 CalculateDirectionTo(u32, u32, u32, u32);

uint16_t gPortIntrCheck;
void* gPortIntrVector;
struct SoundInfo* gPortSoundInfoPtr;

// Data globals
GfxSlotList gGFXSlots;
Message gMessage;
TextRender gTextRender;
u16 gPaletteBuffer[0x200];
Input gInput;
u32 gRand;
Screen gScreen;
OAMCommand gOamCmd;
Main gMain;
FadeControl gFadeControl;
OAMControls gOAMControls;
SoundPlayingInfo gSoundPlayingInfo;

u32 gUsedPalettes;

// Pointers
struct_02000010 gUnk_02000010;
u8 gUnk_02000030[0x10]; /* EWRAM marker, 16 bytes gap */
struct_02000040 gUnk_02000040;
void* gUnk_020000B0 = NULL; /* Entity* pointer (8 bytes on 64-bit) */
struct_gUnk_020000C0 gUnk_020000C0[0x30];
Palette gUnk_02001A3C;
u8 gUnk_02006F00[0x4000] __attribute__((aligned(4)));                    /* BG tilemap buffer (16 KB) */
u16 gUnk_0200B640;                                                       /* scroll state scalar */
u16 gUnk_02017830[0x138] __attribute__((aligned(4)));                    /* palette rotation buffer (624 bytes) */
u16 gUnk_02017AA0[0xA00] __attribute__((aligned(4)));                    /* HBlank DMA double buffer, 2×0xA00 bytes */
struct BgAffineDstData gUnk_02017BA0[0x140] __attribute__((aligned(4))); /* BG2 affine ref lines */
LinkedList2* gUnk_02018EA0 = NULL;
struct_02018EB0 gUnk_02018EB0;
s16 gUnk_02018EE0[0x800] __attribute__((aligned(4))); /* window rasterization scratch */
u16 gUnk_02021F00[0x10];                              /* lever timer→length map (32 bytes) */
u8 gUnk_020227DC[0xC];                                /* text number buffer slot 0 */
struct_020227E8 gUnk_020227E8[1];                     /* text variable slot 1 (8 bytes) */
u8 gUnk_020227F0[0x8];                                /* text variable slot 2 (8 bytes) */
u8 gUnk_020227F8[0x8];                                /* text variable slot 3 (8 bytes) */
u8 gUnk_02022800[0x20] __attribute__((aligned(4)));   /* text variable slot 4 (larger backing buffer) */
u16 gUnk_02022830[0xC00] __attribute__((aligned(4))); /* also reused as a temporary MapDataDefinition */
u8 gUnk_02024048 = 0;                                 /* pending sound count (used by DrawEntity) */
u16 gUnk_020246B0[0xC00] __attribute__((aligned(4))); /* scroll tilemap buffer */
u8 gUnk_020342F8[0x100] __attribute__((aligned(4)));  /* delayedEntityLoad array */
struct_02034480 gUnk_02034480;
u8 gUnk_02034492[0x10] __attribute__((aligned(4))); /* u8 array (pauseMenu) */
u8 gUnk_020344A0[8] __attribute__((aligned(4)));    /* figurineMenu */
struct_020354C0 gUnk_020354C0[32];
/* gUnk_02035542 is aliased to gzHeap+2 via macro in common.c (PC_PORT) */
/* Aliased to gEwram[0x36A58] in text.c */
u32 gUnk_02036A58_storage = 0x02036A58;
/* Aliased to gEwram[0x36AD8] in text.c */
u32 gUnk_02036AD8_storage = 0x02036AD8;
u32 gUnk_02036BB8 = 0x02036BB8;
// ========== Global data ==========

// Core systems
UI gUI;
HUD gHUD;
// gMenu / gChooseFileState / gIntroState share memory (EWRAM 0x02000080)
u8 _gMenuSharedStorage[0x40] __attribute__((aligned(8))) = { 0 };
Area gArea;
SaveFile gSave;
VBlankDMA gVBlankDMA;
u8 gUpdateVisibleTiles;

// Player
PlayerEntity gPlayerEntity;
PlayerState gPlayerState;
Entity* gPlayerClones[3];
ScriptExecutionContext gPlayerScriptExecutionContext;

// Entities
GenericEntity gEntities[MAX_ENTITIES];
GenericEntity gAuxPlayerEntities[MAX_AUX_PLAYER_ENTITIES];
LinkedList gEntityLists[9];
LinkedList gEntityListsBackup[9];
u8 gEntCount;
u8 gManagerCount;

// Side table for 64-bit ScriptExecutionContext pointers
// (avoids overflowing PlayerEntity's 4-byte unk_84 field with 8-byte pointers)
ScriptExecutionContext* gEntityScriptCtxTable[PC_MAX_ENTITY_SLOTS] = { 0 };
CarriedEntity gCarriedEntity;
ItemBehavior gActiveItems[MAX_ACTIVE_ITEMS];
PriorityHandler gPriorityHandler;
PossibleInteraction gPossibleInteraction;

// Room / Map
RoomControls gRoomControls;
MapLayer gMapBottom;
MapLayer gMapTop;
RoomTransition gRoomTransition;
RoomVars gRoomVars;
RoomMemory gRoomMemory[8];
RoomMemory* gCurrentRoomMemory;
void** gCurrentRoomProperties;

// Script
ActiveScriptInfo gActiveScriptInfo;
ScriptExecutionContext gScriptExecutionContextArray[0x20];

// Misc
BgAnimation gBgAnimations[MAX_BG_ANIMATIONS];
u8 gTextGfxBuffer[0xD00];
u8 gPaletteBufferBackup[0x400];
u8 gCollidableCount;
Entity* gCollidableList[MAX_COLLIDABLE_ENTITIES]; /* GBA reserves 80 slots — see entity.h */
u32 gUnk_02000020;

// gFrameObjLists — sprite frame data (200KB, self-relative offsets)
u32 gFrameObjLists[50016];

// gMapData — map data blob, backed by ROM data.
// On GBA this is a label in .rodata at gAreaRoomMap_None (~14MB region).
// On PC, we use a large buffer filled from ROM in Port_LoadRom().
// Source files use &gMapData + offset, so this must be an array (not a pointer).
#ifdef TMC_N64
/* #N64: the ~14 MB ROM map-data window can't live in 8 MB RDRAM. Temporary 1 MB
 * placeholder so the binary links and boots to the title (which doesn't read map
 * data). Phase 3 backs &gMapData with the embedded cart ROM (PI/DFS), not a RAM copy. */
u8 gMapData[0x100000] __attribute__((aligned(4))); /* 1 MB placeholder */
#else
u8 gMapData[0xE00000] __attribute__((aligned(4))); /* ~14 MB */
#endif

// gCollisionMtx — On GBA, the collision matrix label sits at 0x080B7B74 with
// only 1210 bytes of named data, but collision.c declares it as
// `extern ColSettings gCollisionMtx[173*34]` (each ColSettings = 12 bytes).
// Code in collision.c redirect-handlers indexing at e.g. 0x11AA accesses
// byte offset 0x11AA*12 = 54264 — way beyond 1210 bytes.  On GBA this
// reads ROM data that follows the label.  We allocate the full 173*34*12
// bytes and copy from ROM during Port_InitCollisionMtx().
u8 gCollisionMtx[173 * 34 * 12];

UpdateContext gUpdateContext;

u8 gInteractableObjects[0x300]
    __attribute__((aligned(8))); /* 32 InteractableObject entries * 24 bytes on 64-bit = 768 = 0x300 */

// === Additional data stubs ===

Palette gPaletteList[0x10];

// Hitbox data is now provided by src/data/hitbox.c (properly initialized)

// NPC data
NPCStruct gNPCData[NPC_DATA_CAPACITY];

/* ---------- Townsperson NPC function pointer tables ---------- */
/* These were .4byte tables in ROM pointing to Thumb function addresses.
 * On PC we define them as proper C function pointer arrays. */
extern void sub_08061BC8(Entity*);
extern void sub_08061C00(Entity*);
extern void sub_08061CEC(/* TownspersonEntity* */ Entity*);
extern void sub_08061D64(/* TownspersonEntity* */ Entity*);
extern void sub_08061E24(/* TownspersonEntity* */ Entity*);
extern void sub_08061E50(/* TownspersonEntity* */ Entity*);

void (*const gUnk_0810B774[])(Entity*) = {
    sub_08061BC8,
    sub_08061C00,
};
void (*const gUnk_0810B77C[])(Entity*) = {
    sub_08061CEC,
    sub_08061D64,
    sub_08061E24,
    sub_08061E50,
};

/* ---------- Pause menu / subtask function pointer tables ---------- */
/* From data/const/subtask.s — .4byte tables referencing C functions. */

/* PauseMenu_Screen_10 dispatch (pauseMenu.c) */
extern void sub_080A59AC(void);
extern void sub_080A59C8(void);
extern void sub_080A5A54(void);
extern void sub_080A5A90(void);
void (*const gUnk_08128D14[])(void) = {
    sub_080A59AC,
    sub_080A59C8,
    sub_080A5A54,
    sub_080A5A90,
};

/* PauseMenu_Screen_9 dispatch (pauseMenu.c) */
extern void sub_080A5AF4(void);
extern void sub_080A5B34(void);
extern void sub_080A5BB8(void);
void (*const gUnk_08128D24[])(void) = {
    sub_080A5AF4,
    sub_080A5B34,
    sub_080A5BB8,
};

/* PauseMenu_Screen_5 dispatch (pauseMenu.c) */
extern void sub_080A5C44(u32, u32, u32); /* actual signature, but called as void(void) from dispatch */
extern void sub_080A5C9C(void);
void (*const gUnk_08128D30[])(void) = {
    (void (*)(void))sub_080A5C44,
    sub_080A5C9C,
};

/* PauseMenu_Screen_7 dispatch (pauseMenu.c) */
extern void sub_080A6024(void);
extern void sub_080A6044(void);
void (*const gUnk_08128D58[])(void) = {
    sub_080A6024,
    sub_080A6044,
};

/* PauseMenu_Screen_8 dispatch (pauseMenu.c) */
extern void sub_080A6108(void);
extern void sub_080A612C(void);
void (*const gUnk_08128DB0[])(void) = {
    sub_080A6108,
    sub_080A612C,
};

/* PauseMenu_Screen_4 dispatch (pauseMenu.c) */
extern void sub_080A6290(void);
extern void sub_080A62E0(void);
void (*const gUnk_08128DCC[])(void) = {
    sub_080A6290,
    sub_080A62E0,
};

/* PauseMenu_Screen_6 dispatch (pauseMenuScreen6.c) */
extern void sub_080A6650(void);
extern void sub_080A667C(void);
void (*const gUnk_08128E78[])(void) = {
    sub_080A6650,
    sub_080A667C,
};

/* Subtask local map hint dispatch (subtaskLocalMapHint.c) */
extern void sub_080A6B04(void);
extern void sub_080A6C1C(void);
void (*const gUnk_08128F1C[])(void) = {
    sub_080A6B04,
    sub_080A6C1C,
};

// Pause menu
PauseMenuOptions gPauseMenuOptions;

SpritePtr gSpritePtrs[512];
/* port_rom.c reads up to this many entries from ROM at startup. The
 * compile-time table caps at 329 but TMC references indices up to
 * ~510 (Moldorm at 484, others). Keep in sync with the gSpritePtrs[]
 * array size above. */
const u32 kMaxSpritePtrs = 512;
/* Updated by port_rom.c after the runtime extension. Used by
 * Port_GetSpritePtr instead of the compile-time spritePtrsCount. */
u32 gSpritePtrsLoadedCount = 0;

// Map data — both are u16[0x4000] tilemaps (0x8000 bytes) reused for file-select overlay
u16 gMapDataBottomSpecial[0x4000] __attribute__((aligned(4)));
u16 gMapDataTopSpecial[0x4000] __attribute__((aligned(4)));
u32 gDungeonMap[0x800];

// Heap
u8 gzHeap[0x1000];

// Kinstone fusion
FuseInfo gFuseInfo;

// Room / tile entities
TileEntity gSmallChests[8];
SpecialTileEntry gTilesForSpecialTiles[MAX_SPECIAL_TILES];

// Collision
// gUnk_03003C70 now defined in src/collision.c

// IWRAM scratch
u8 gUnk_03000420[0x800] __attribute__((aligned(4)));
u8 gUnk_03000C30;
u8 gUnk_03001020[sizeof(Screen)] __attribute__((aligned(4)));

// Sound player data
/* On 64-bit PC: pointer fields grow from 4 to 8 bytes, so MusicPlayerInfo
 * is 96 bytes (vs GBA's 80) and MusicPlayerTrack is 112 (vs 80). The
 * hardcoded `0x50 * N` stride undersizes the buffer — and worse, the
 * gMusicPlayers table in sound.c references tracks up to `&gMPlayTracks[0x51]`
 * (player MUSIC_PLAYER_BGM uses 12 tracks starting at offset 0x46 → ends
 * at 0x51), so the array needs at least 0x52 tracks of storage. The
 * original `[0x50 * 16] = 1280-byte` declaration was wrong for *both*
 * size axes. When MPlayOpen + per-player MPlayStart writes touch tracks
 * outside the buffer, they zero adjacent globals — including
 * gAreaRoomHeaders, which made the windcrest-pin loop in
 * subtaskFastTravel.c NULL-deref (#53 second-stage). Caught with a
 * tripwire pinpointing m4aSoundInit. */
#include "gba/m4a.h"
#define M4A_MAX_INFO_INDEX 0x1C  /* gMPlayInfos uses 0x00..0x1B */
#define M4A_MAX_INFO2_INDEX 0x4  /* gMPlayInfos2 uses 0x0..0x3 */
#define M4A_MAX_TRACK_INDEX 0x52 /* gMPlayTracks uses 0x00..0x51 (BGM = 0x46+12) */
u8 gMPlayInfos[M4A_MAX_INFO_INDEX * sizeof(MusicPlayerInfo)] __attribute__((aligned(8)));
u8 gMPlayInfos2[M4A_MAX_INFO2_INDEX * sizeof(MusicPlayerInfo)] __attribute__((aligned(8)));
u8 gMPlayTracks[M4A_MAX_TRACK_INDEX * sizeof(MusicPlayerTrack)] __attribute__((aligned(8)));
u8 gMPlayMemAccArea[0x10] __attribute__((aligned(4)));

// BGM song headers (ROM data stubs)
#define SONG_STUB(name) const SongHeader name = { 0 }
SONG_STUB(bgmBeanstalk);
SONG_STUB(bgmBeatVaati);
SONG_STUB(bgmBossTheme);
SONG_STUB(bgmCastleCollapse);
SONG_STUB(bgmCastleMotif);
SONG_STUB(bgmCastleTournament);
SONG_STUB(bgmCastorWilds);
SONG_STUB(bgmCaveOfFlames);
SONG_STUB(bgmCloudTops);
SONG_STUB(bgmCredits);
SONG_STUB(bgmCrenelStorm);
SONG_STUB(bgmCuccoMinigame);
SONG_STUB(bgmDarkHyruleCastle);
SONG_STUB(bgmDeepwoodShrine);
SONG_STUB(bgmDiggingCave);
SONG_STUB(bgmDungeon);
SONG_STUB(bgmElementGet);
SONG_STUB(bgmElementTheme);
SONG_STUB(bgmElementalSanctuary);
SONG_STUB(bgmEzloGet);
SONG_STUB(bgmEzloStory);
SONG_STUB(bgmEzloTheme);
SONG_STUB(bgmFairyFountain);
SONG_STUB(bgmFairyFountain2);
SONG_STUB(bgmFestivalApproach);
SONG_STUB(bgmFightTheme);
SONG_STUB(bgmFightTheme2);
SONG_STUB(bgmFileSelect);
SONG_STUB(bgmFortressOfWinds);
SONG_STUB(bgmGameover);
SONG_STUB(bgmHouse);
SONG_STUB(bgmHyruleCastle);
SONG_STUB(bgmHyruleCastleNointro);
SONG_STUB(bgmHyruleField);
SONG_STUB(bgmHyruleTown);
SONG_STUB(bgmIntroCutscene);
SONG_STUB(bgmLearnScroll);
SONG_STUB(bgmLostWoods);
SONG_STUB(bgmLttpTitle);
SONG_STUB(bgmMinishCap);
SONG_STUB(bgmMinishVillage);
SONG_STUB(bgmMinishWoods);
SONG_STUB(bgmMtCrenel);
SONG_STUB(bgmPalaceOfWinds);
SONG_STUB(bgmPicoriFestival);
SONG_STUB(bgmRoyalCrypt);
SONG_STUB(bgmRoyalValley);
SONG_STUB(bgmSavingZelda);
SONG_STUB(bgmSecretCastleEntrance);
SONG_STUB(bgmStory);
SONG_STUB(bgmSwiftbladeDojo);
SONG_STUB(bgmSyrupTheme);
SONG_STUB(bgmTempleOfDroplets);
SONG_STUB(bgmTitleScreen);
SONG_STUB(bgmUnused);
SONG_STUB(bgmVaatiMotif);
SONG_STUB(bgmVaatiReborn);
SONG_STUB(bgmVaatiTheme);
SONG_STUB(bgmVaatiTransfigured);
SONG_STUB(bgmVaatiWrath);
SONG_STUB(bgmWindRuins);
#undef SONG_STUB

// Linker symbols (unused on PC)
u8 RAMFUNCS_END[4];
u8 gCopyToEndOfEwram_End[4];
u8 gCopyToEndOfEwram_Start[4];
u8 gEndOfEwram[4];
u8 sub_080B197C[4];
u8 ram_sub_080B197C[4];
u32 ram_MakeFadeBuff256;

/*
 * C reimplementation of ram_sub_080B197C (ARM IWRAM function).
 * Called when gUpdateVisibleTiles == 1 (initial full-screen tile fill).
 * Copies a 32×23 tile region from mapSpecial to the BG buffer.
 *
 * mapSpecial: u16 tilemap with 128 entries per row (8×8 pixel tiles).
 * bgBuffer:   passed as gBGxBuffer + 0x20 (u16 units, +0x40 bytes).
 *             The GBA code subtracts 0x40 bytes to start writing from row 0.
 */
static void ram_sub_080B197C_c(u16* mapSpecial, u16* bgBuffer) {
    u16 xdiff = gRoomControls.scroll_x - gRoomControls.origin_x;
    u16 ydiff = gRoomControls.scroll_y - gRoomControls.origin_y;

    /* Tile position in 16×16 units → each maps to 2×2 sub-tiles of 8×8.
     * Byte offset = (col16 + row16 * 128) * 4, because each 16×16 tile
     * is 2 sub-tile columns (×2 bytes each = 4 bytes) and each 16×16 row
     * spans 2 sub-tile rows (128 entries × 2 bytes × 2 = 512 bytes). */
    u32 col16 = xdiff >> 4;
    u32 row16 = ydiff >> 4;
    u8* src = (u8*)mapSpecial + (col16 + row16 * 128) * 4;

    /* On GBA gMapDataTopSpecial/gMapDataBottomSpecial are adjacent EWRAM and
     * deep scroll positions deliberately walked this copy off one buffer into
     * its neighbor (stable garbage on the offscreen seam). On PC they are
     * separate globals — clamp each 64-byte row to the 0x8000-byte buffer;
     * skipped rows leave zeros (transparent tiles) on the seam instead. */
    const u8* lo = (const u8*)mapSpecial;
    const u8* hi = lo + 0x8000;

#define COPY_ROW_CLAMPED()                 \
    do {                                   \
        if (src >= lo && src + 64 <= hi) { \
            memcpy(dst, src, 64);          \
        }                                  \
    } while (0)

    /* bgBuffer was passed as gBGxBuffer + 0x20 (in u16 units).
     * The original code does "subs r1, #0x40" to get to row 0. */
    u16* dst = bgBuffer - 0x20;

    if (ydiff < 8) {
        /* First row: copy without advancing src */
        COPY_ROW_CLAMPED(); /* 32 u16 = 64 bytes */
        dst += 32;
        /* 22 more rows: first reuses same src, then advances */
        for (int i = 0; i < 22; i++) {
            COPY_ROW_CLAMPED();
            src += 0x100; /* next 8×8 map row = 128 u16 = 256 bytes */
            dst += 32;
        }
    } else {
        /* Start one map row earlier */
        src -= 0x100;
        /* 23 consecutive rows */
        for (int i = 0; i < 23; i++) {
            COPY_ROW_CLAMPED();
            src += 0x100;
            dst += 32;
        }
    }
#undef COPY_ROW_CLAMPED
}

/* Declared in screenTileMap.c (already compiled as C on PC) */
extern void sub_0807D280(u16* mapSpecial, u16* bgBuffer);
extern void sub_0807D46C(u16* mapSpecial, u16* bgBuffer);
extern void sub_0807D6D8(u16* mapSpecial, u16* bgBuffer);

/*
 * Collision direction masks from gUnk_0800275C (now in data_stubs_autogen.c)
 * Local alias for convenient u16 access.
 */
extern const u8 gUnk_0800275C[64];
#define gCollisionDirectionMasks ((const u16*)gUnk_0800275C)

/* Collision parameter tables used by extended tile collision lookups. */
extern const u8 gUnk_080082DC[];
extern const u8 gUnk_0800833C[];
extern const u8 gUnk_0800839C[];
extern const u8 gUnk_080083FC[];
extern const u8 gUnk_0800845C[];
extern const u8 gUnk_080084BC[];
extern const u8 gUnk_0800851C[];

static const u8* sActiveCollisionParams = gUnk_080082DC;
u32 GetCollisionDataAtTilePos(u32 tilePos, u32 layer);

static const u8* SelectPlayerCollisionTable(void) {
    const u8* table = gUnk_080083FC;
    if (gPlayerState.swim_state != 0) {
        if (gPlayerState.flags & PL_MINISH) {
            table = gUnk_0800839C;
        }
        return table;
    }

    table = gUnk_0800845C;
    if (gPlayerState.jump_status != 0) {
        return table;
    }
    if (gPlayerState.flags & PL_PARACHUTE) {
        return table;
    }
    if (gPlayerState.flags & PL_MINISH) {
        return gUnk_0800833C;
    }

    table = gUnk_080084BC;
    if (gPlayerState.gustJarState == 0 && gPlayerState.heldObject == 0) {
        table = gUnk_0800851C;
        if (gPlayerState.attachedBeetleCount == 0) {
            table = gUnk_080082DC;
        }
    }
    return table;
}

/*
 * TileCollisionLookup — core tile collision lookup (port of sub_080086D8).
 *
 * Takes pixel coordinates (room-relative), looks up the collision data
 * for the tile at that position, and returns 0 (passable) or 1 (blocked).
 *
 * For tile collision types 0x00–0x0F: 2×2 sub-tile collision pattern.
 *   Bit 0 = bottom-right, bit 1 = bottom-left, bit 2 = top-right, bit 3 = top-left.
 *   The sub-tile quadrant is selected by bits 3 of x and y.
 *
 * For tile collision type 0xFF: always blocked.
 * For tile collision types 0x10–0xFE: extended pixel-level collision
 * resolves the packed pointer table through the active ROM profile.
 */
static u32 TileCollisionLookup(u32 px, u32 py, Entity* entity) {
    u32 tilePos = ((px & 0x3F0) >> 4) + ((py & 0x3F0) << 2);
    u8 tileType = (u8)GetCollisionDataAtTilePos(tilePos, entity->collisionLayer);

    if (gPlayerState.swim_state != 0 && gPlayerState.floor_type != 0x18 && tileType < 0x10) {
        tileType = 0x0F;
    }

    if (tileType < 0x10) {
        u8 bits = tileType;
        if ((py & 8) == 0) {
            bits >>= 2;
        }
        if ((px & 8) == 0) {
            bits >>= 1;
        }
        return bits & 1;
    }

    if (tileType == 0xFF) {
        return 1;
    }

    u8 idx = sActiveCollisionParams[tileType - 0x10];
    const u16* table = Port_GetCollisionShapeData(idx);
    if (table == NULL) {
        return 0;
    }

    u16 row = table[py & 0x0F];
    row >>= (0x0F ^ (px & 0x0F));
    return row & 1;
}

/*
 * sub_080085CC — fill entity->collisions by probing 8 points around entity hitbox.
 * (from Thumb asm at 0x080085CC)
 *
 * Probes are placed on the 4 edges of the collision box:
 *   Right edge:  (baseX + halfW, baseY ± vStep)
 *   Left edge:   (baseX - halfW, baseY ± vStep)
 *   Bottom edge: (baseX ± hStep, baseY + halfH)
 *   Top edge:    (baseX ± hStep, baseY - halfH)
 *
 * Hitbox layout (bytes 2–5 of Hitbox struct = unk2[0..3]):
 *   unk2[0] = halfW  (half-width for collision)
 *   unk2[1] = vStep  (vertical probe step)
 *   unk2[2] = hStep  (horizontal probe step)
 *   unk2[3] = halfH  (half-height for collision)
 *
 * Result is a 16-bit mask stored in entity->collisions:
 *   Bit 14: right edge, south probe
 *   Bit 13: right edge, north probe
 *   Bit 10: left edge, south probe
 *   Bit  9: left edge, north probe
 *   Bit  6: bottom edge, east probe
 *   Bit  5: bottom edge, west probe
 *   Bit  2: top edge, east probe
 *   Bit  1: top edge, west probe
 */
void sub_080085CC(Entity* ent) {
    Hitbox* hb = (Hitbox*)port_resolve_addr((uintptr_t)ent->hitbox);
    if (hb == NULL) {
        ent->collisions = 0;
        return;
    }

    sActiveCollisionParams = SelectPlayerCollisionTable();

    s32 baseX = (s32)(u16)ent->x.HALF.HI - (s32)gRoomControls.origin_x + hb->offset_x;
    s32 baseY = (s32)(u16)ent->y.HALF.HI - (s32)gRoomControls.origin_y + hb->offset_y;

    u32 sideX = (u8)hb->unk2[0];
    u32 sideY = (u8)hb->unk2[1];
    u32 innerX = (u8)hb->unk2[2];
    u32 innerY = (u8)hb->unk2[3];

    u16 collisions = 0;
    collisions |= (u16)(TileCollisionLookup((u32)(baseX + (s32)sideX), (u32)(baseY + (s32)sideY), ent) << 14);
    collisions |= (u16)(TileCollisionLookup((u32)(baseX + (s32)sideX), (u32)(baseY - (s32)sideY), ent) << 13);
    collisions |= (u16)(TileCollisionLookup((u32)(baseX - (s32)sideX), (u32)(baseY + (s32)sideY), ent) << 10);
    collisions |= (u16)(TileCollisionLookup((u32)(baseX - (s32)sideX), (u32)(baseY - (s32)sideY), ent) << 9);
    collisions |= (u16)(TileCollisionLookup((u32)(baseX + (s32)innerX), (u32)(baseY + (s32)innerY), ent) << 6);
    collisions |= (u16)(TileCollisionLookup((u32)(baseX - (s32)innerX), (u32)(baseY + (s32)innerY), ent) << 5);
    collisions |= (u16)(TileCollisionLookup((u32)(baseX + (s32)innerX), (u32)(baseY - (s32)innerY), ent) << 2);
    collisions |= (u16)(TileCollisionLookup((u32)(baseX - (s32)innerX), (u32)(baseY - (s32)innerY), ent) << 1);
    ent->collisions = collisions;
}

u32 sub_080086D8(u32 roomX, u32 roomY, const u8* params) {
    sActiveCollisionParams = params;
    return TileCollisionLookup(roomX, roomY, &gPlayerEntity.base);
}

u32 sub_080086B4(u32 roomX, u32 roomY, const u8* params) {
    return sub_080086D8(roomX, roomY, params);
}

/*
 * CalcCollisionDirectionOLD — adjust movement direction based on collision data
 * When an entity is moving in a cardinal direction and hitting a wall,
 * this returns a perpendicular direction to slide along the wall.
 * (from Thumb asm at 0x08002864)
 *
 * r0 = direction (0–31), r1 = collisions (entity->collisions)
 * Returns adjusted direction or original if no slide.
 */
static u32 CalcCollisionDirectionOLD(u32 direction, u32 collisions) {
    u32 quadrant = direction >> 3; /* 0=N, 1=E, 2=S, 3=W */

    switch (quadrant) {
        case 0: /* North */
            if (!(collisions & 0x000E))
                return direction;
            if (!(collisions & 0xE004))
                return 0x08; /* East */
            if (!(collisions & 0x0E02))
                return 0x18; /* West */
            return direction;

        case 1: /* East */
            if (!(collisions & 0xE000))
                return direction;
            if (!(collisions & 0x200E))
                return 0x00; /* North */
            if (!(collisions & 0x40E0))
                return 0x10; /* South */
            return direction;

        case 2: /* South */
            if (!(collisions & 0x00E0))
                return direction;
            if (!(collisions & 0xE040))
                return 0x08; /* East */
            if (!(collisions & 0x0E20))
                return 0x18; /* West */
            return direction;

        case 3: /* West */
            if (!(collisions & 0x0E00))
                return direction;
            if (!(collisions & 0x020E))
                return 0x00; /* North */
            if (!(collisions & 0x04E0))
                return 0x10; /* South */
            return direction;

        default:
            return direction;
    }
}

/*
 * LinearMoveDirectionOLD — move entity by speed in given direction, checking collisions.
 * (from Thumb asm at 0x080027EA)
 *
 * Updates entity->x.WORD and entity->y.WORD using sine table lookup.
 * Returns bitmask: bit 0 = X moved, bit 1 = Y moved.
 */
u32 LinearMoveDirectionOLD(Entity* ent, u32 speed, u32 direction) {
    u32 moved = 0;

    if (direction & 0x80)
        return 0; /* DIR_NOT_MOVING */

    u16 collisions = ent->collisions;

    /* If direction is exactly cardinal (no sub-cardinal bits), try wall sliding */
    if ((direction & 7) == 0) {
        u32 adjusted = CalcCollisionDirectionOLD(direction, collisions);
        if (adjusted != direction) {
            direction = adjusted;
            speed = 0x100; /* slow down when sliding */
        }
    }

    /* Look up collision mask for this direction */
    u16 colMask = gCollisionDirectionMasks[direction & 0x1F];
    u16 masked = collisions & colMask;

    /* X movement */
    if (!(masked & 0xEE00)) {
        s16 sinVal = gSineTable[direction * 8];
        if (sinVal != 0) {
            moved |= 1;
            s32 dx = FixedMul(sinVal, (s16)speed) << 8;
            ent->x.WORD += dx;
        }
    }

    /* Y movement */
    if (!(masked & 0x00EE)) {
        s16 cosVal = gSineTable[direction * 8 + 64];
        if (cosVal != 0) {
            moved |= 2;
            s32 dy = FixedMul(cosVal, (s16)speed) << 8;
            ent->y.WORD -= dy;
        }
    }

    return moved;
}

/*
 * sub_0800857C — player movement wrapper.
 * Calls sub_080085CC (collision tile update) then LinearMoveDirectionOLD.
 * (from Thumb asm at 0x0800857C)
 */
void sub_0800857C(Entity* ent) {
    if (((u8)ent->type2 & 0x80) == 0 && (gPlayerState.jump_status & 0x80) == 0) {
        sub_080085CC(ent);
    }
    LinearMoveDirectionOLD(ent, ent->speed, ent->direction);
}

/*
 * sub_080085B0 — collision tile update wrapper.
 * (from Thumb asm at 0x080085B0)
 * Calls sub_080085CC — tile collision detection for the player.
 */
void sub_080085B0(Entity* ent) {
    sub_080085CC(ent);
}

/*
 * sub_08008AA0 — set player velocity direction from sine table.
 * (from Thumb asm at 0x08008AA0)
 *   Reads gPlayerState.direction, looks up sin/cos, stores in vel_x/vel_y.
 */
void sub_08008AA0(Entity* ent) {
    (void)ent;
    if (gPlayerState.floor_type == 1)
        return;
    u8 dir = gPlayerState.direction;
    if (dir == 0xFF)
        return;
    gPlayerState.vel_x = gSineTable[dir * 8];
    gPlayerState.vel_y = -gSineTable[dir * 8 + 64];
}

/*
 * sub_08008AC6 — check if player should respawn (fallen off edge, etc.)
 * Port of Thumb asm at 0x08008AC6; swim/flag guards and
 * GetNonCollidedSide/RespawnPlayer flow match the original routine.
 */
static u32 GetNonCollidedSide(Entity* ent) {
    u16 c = ent->collisions;
    for (int i = 0; i < 4; i++) {
        if ((c & 0x000E) == 0) {
            return 0;
        }
        c >>= 4;
    }
    return 1;
}

void sub_08008AC6(Entity* ent) {
    if ((gPlayerState.swim_state & 0x0F) != 0) {
        return;
    }
    if ((gPlayerState.flags & gUnk_02000020) != 0) {
        return;
    }
    if (GetNonCollidedSide(ent) != 0) {
        ent->iframes = (s8)0xE2;
        RespawnPlayer();
    }
}

/*
 * GetNextFunction — entity state machine dispatcher
 * Returns 0-5 based on entity combat/action state.
 * (from Thumb asm at 0x0800279C)
 */
u32 GetNextFunction(Entity* this) {
    u8 gustJarState = this->gustJarState;
    u8 contactFlags = this->contactFlags;

    /* Peahat (and other gust-jar-killable enemies) reach Subaction5 with
     * health=0 + gustJarState=0x04 + ENT_COLLIDE clear, expecting the
     * death sequence to take over. The gj=0x04 short-circuit at top would
     * send them right back to OnGrabbed forever — corpse never despawns
     * (#20). When dead, prefer the death-cascade dispatch so GenericDeath
     * runs and the DEATH_FX cleanup chain fires.
     *
     * AcroBandit's chain unwind lives in OnCollision and only runs on the
     * death frame (contactFlags bit 7 + health hits 0); short-circuiting
     * straight to OnDeath would leave the surviving bandits stuck in
     * Action4 with stale parent pointers (#35). Death-fall animation runs
     * via OnKnockback while knockbackDuration is nonzero, so OnKnockback
     * needs to keep dispatching during dying frames too. Fall through to
     * OnDeath only when neither is active. */
    if (this->health == 0) {
        if (!(gustJarState & 4) && (contactFlags & 0x80))
            return 1; /* OnCollision: chain unwind + death-state setup */
        if (this->knockbackDuration != 0)
            return 2; /* OnKnockback: death-fall animation */
        if (this->action == 0 && this->subAction == 0)
            return 0;
        if (gustJarState & 8)
            return 5;
        if (this->confusedTime != 0)
            return 4;
        return 3; /* OnDeath */
    }

    /* GBA-original alive dispatch order: gust-jar grab > contact >
     * knockback > confused > tick. Matches the Thumb asm at 0x0800279C.
     *
     * #54: dizzy-stars FX never leaves a boomerang-stunned enemy. The
     * confusedTime check below was previously omitted, so GenericConfused
     * never ran on living enemies — confusedTime never decremented, the
     * FX_STARS object's parent (the enemy) stayed alive forever, and
     * sub_08084694 (the FX's update) only self-deletes when its parent
     * is gone. Re-introducing the dispatch lets GenericConfused tick
     * confusedTime down and call EnemyDetachFX at the end of stun, which
     * orphans the FX so its next tick deletes it. Empirically all FX_STARS
     * spawners under src/enemy use EnemyCreateFX (stored in
     * entity->child), so the existing detach path covers them. */
    if (gustJarState & 4)
        return 5;

    if (contactFlags & 0x80)
        return 1;

    if (this->knockbackDuration != 0)
        return 2;

    if (this->confusedTime != 0)
        return 4;

    if (this->action == 0 && this->subAction == 0)
        return 0;

    return 0;
}

/*
 * Random — simple 32-bit PRNG (from ARM asm at 0x08000E50)
 *   state = ROR(state * 3, 13);  return state >> 1;
 */
u32 Random(void) {
    gRand = Port_Rng_Advance(gRand);
    return Port_Rng_Output(gRand);
}

/*
 * CheckBits — test whether `count` consecutive bits starting at `bitIndex`
 *             are all set in the byte array `base`.  (from ARM asm at 0x080B20EC)
 *   Returns 1 if ALL bits set, 0 if any bit is clear.
 */
u32 CheckBits(void* base, u32 bitIndex, u32 count) {
    const u8* ptr = (const u8*)base + (bitIndex / 8);
    u32 bit = bitIndex % 8;
    for (u32 i = 0; i < count; i++) {
        if (!(ptr[0] & (1u << bit)))
            return 0;
        bit++;
        if (bit >= 8) {
            bit = 0;
            ptr++;
        }
    }
    return 1;
}

/*
 * Mod — modulo (SWI 0x06 wrapper): returns num % denom
 */
s32 Mod(s32 num, s32 denom) {
    if (denom == 0)
        return 0;
    return num % denom;
}

/*
 * SumDropProbabilities — vector add 3 arrays of 16 s16 values.
 *   out[i] = a[i] + b[i] + c[i]   for i = 0..15
 */
void SumDropProbabilities(s16* out, const s16* a, const s16* b, const s16* c) {
    for (int i = 0; i < 16; i++) {
        out[i] = a[i] + b[i] + c[i];
    }
}

/*
 * SumDropProbabilities2 — vector add, clamp each to >= 0, return scalar sum.
 *   out[i] = max(0, a[i] + b[i] + c[i])   for i = 0..15
 *   returns sum of all out[i]
 */
u32 SumDropProbabilities2(s16* out, const s16* a, const s16* b, const s16* c) {
    u32 sum = 0;
    for (int i = 0; i < 16; i++) {
        s16 val = a[i] + b[i] + c[i];
        if (val < 0)
            val = 0;
        out[i] = val;
        sum += val;
    }
    return sum;
}

/*
 * UpdateScrollVram — copies map tile data from gMapDataBottomSpecial / gMapDataTopSpecial
 * into gBG1Buffer / gBG2Buffer depending on gUpdateVisibleTiles.
 *
 * Replaces the ARM veneer at 0x08000108 and the IWRAM sub_080B197C.
 */
void UpdateScrollVram(void) {
    typedef void (*ScrollVramFunc)(u16*, u16*);
    static const ScrollVramFunc funcs[] = {
        NULL,               /* 0: unused (returns immediately) */
        ram_sub_080B197C_c, /* 1: initial full-screen fill */
        sub_0807D280,       /* 2: scroll update mode */
        sub_0807D46C,       /* 3: scroll update mode */
        sub_0807D6D8,       /* 4: scroll update mode */
    };

    u8 mode = gUpdateVisibleTiles;
    if (mode == 0 || mode > 4)
        return;

    ScrollVramFunc func = funcs[mode];

    /* Bottom layer → BG1 */
    if (gMapBottom.bgSettings != NULL) {
        func(gMapDataBottomSpecial, &gBG1Buffer[0x20]);
    }

    /* Top layer → BG2 */
    if (gMapTop.bgSettings != NULL) {
        func(gMapDataTopSpecial, &gBG2Buffer[0x20]);
    }
}

#if defined(MODE1_GBA_WIDTH) && (MODE1_GBA_WIDTH > 240)
#include "cpu/mode1.h"

/*
 * Widescreen Phase 2 (Option A) — port-side shadow tilemap.
 *
 * The PPU (libs/ViruaPPU/src/mode1.c::render_text_bg_line) reads tile
 * entries from these for display cols >= MODE1_GBA_BG_CLIP_X on 32-tile
 * BGs, bypassing VRAM — the 32-tile screenblock only spans 256 px of
 * world and wraps past that, so the reveal columns can't come from VRAM.
 *
 * Each shadow is MODE1_WS_SHADOW_ROWS rows x MODE1_WS_SHADOW_COLS u16
 * entries, indexed by tile_row (mod 32, mirroring the engine's vertical
 * rolling) and (tile_col - base) mod 32 in the PPU. Populated each
 * scroll-update from gMapData{Bottom,Top}Special (128x128 BG-tiles) at
 * world cols [cam_bg_col + MODE1_GBA_BG_CLIP_X/8 .. + COLS).
 *
 * Gated #if width>240: the native build never compiles this and the PPU
 * shadow pointers stay NULL (render falls back to clip-at-240). */
static u16 sWsShadowBG1[MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS];
static u16 sWsShadowBG2[MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS];

/* ---- Runtime widescreen gate --------------------------------------------
 * `--widescreen_width=N` only reserves a wider framebuffer. True widescreen
 * is still WIP, so a persisted runtime option decides whether gameplay uses
 * the wider camera/reveal. Disabled, non-gameplay, one-screen (<=240px)
 * rooms, and rooms whose painted content can't fill the room-capped view all
 * fall back to a native 240x160 frame (presented pillarboxed, never
 * stretched); rooms between 240px and the window target run true-wide at
 * their full room width. The camera/culling helpers below report 240 in the
 * fallback state so the engine remains GBA-clean. */
/* ---- Per-room content width (issue: wide room, narrow content) ----------
 * gRoomControls.width can exceed the actual painted map (e.g. a 480-px room
 * whose floor ends ~270 px in). Widescreen then parks void columns on screen
 * permanently (a GBA player only saw them transiently at the right edge).
 * Scan both map buffers for the rightmost non-empty tile column inside the
 * room rect; if that content can't fill the wide view even with the camera
 * pinned left, fall back to a native 240 frame (presented pillarboxed) —
 * which shows exactly what GBA showed. Ratcheted per room (only grows) so
 * late-streaming map data can't flip the mode back and forth mid-room. */
static u32 sWsContentKey = 0xffffffffu;
static s32 sWsContentPx = 0;

/* Key identifying the room the content cache belongs to. Checked by
 * FallbackNative so a stale cache (previous room) is never consulted —
 * InitializeCamera runs at room entry BEFORE the next vblank rescan. */
static u32 Port_WidescreenRoomKey(void) {
    return ((u32)gRoomControls.area << 24) | ((u32)gRoomControls.room << 16) |
           ((u32)(u16)gRoomControls.origin_x ^ ((u32)(u16)gRoomControls.origin_y << 1));
}

static s32 Port_WidescreenScanContentPx(void) {
    enum { kMapStride = 128, kMapRows = 128 };
    s32 tiles_w = (s32)gRoomControls.width / 8;
    s32 tiles_h = (s32)gRoomControls.height / 8;
    s32 c, r;
    if (tiles_w > kMapStride)
        tiles_w = kMapStride;
    if (tiles_h > kMapRows)
        tiles_h = kMapRows;
    for (c = tiles_w - 1; c >= 0; c--) {
        const u16* bot = gMapDataBottomSpecial + c;
        const u16* top = gMapDataTopSpecial + c;
        for (r = 0; r < tiles_h; r++) {
            if (bot[(size_t)r * kMapStride] != 0 || top[(size_t)r * kMapStride] != 0) {
                return (c + 1) * 8;
            }
        }
    }
    return 0;
}

/* ---- True widescreen: window-aspect-driven view width -------------------
 * MODE1_GBA_WIDTH is the framebuffer CAPACITY (hard cap); the presented
 * view width tracks the live window aspect so the frame fills the monitor
 * exactly: 16:9 -> 284 px, 21:9 -> 373 -> cap, 4:3/3:2 -> 240 (authentic).
 * Published once per frame from the present path; rounded to a multiple of
 * 8 so tile/HUD math stays exact. TMC_WS_VIEW_WIDTH=<px> overrides for
 * headless captures (dummy driver windows don't track a real monitor). */
static int sWsWindowW = 0;
static int sWsWindowH = 0;

void Port_Widescreen_SetWindowPixels(int w, int h) {
    sWsWindowW = w;
    sWsWindowH = h;
}

int Port_Widescreen_TargetViewWidth(void) {
    static int env_w = -1;
    int w;
    if (env_w < 0) {
        const char* e = getenv("TMC_WS_VIEW_WIDTH");
        env_w = 0;
        if (e && *e) {
            int v = atoi(e);
            if (v >= 240 && v <= MODE1_GBA_WIDTH)
                env_w = v;
        }
    }
    if (env_w > 0) {
        return env_w;
    }
    if (sWsWindowW <= 0 || sWsWindowH <= 0) {
        return 240;
    }
    /* Exact aspect fit: the width that makes the 160-line frame fill the
     * window (no rounding — every consumer is pixel-based, and exact fit
     * beats up-to-7px side bars). */
    w = (int)(((long long)sWsWindowW * 160) / sWsWindowH);
    if (w < 240)
        w = 240;
    if (w > MODE1_GBA_WIDTH)
        w = MODE1_GBA_WIDTH;
    return w;
}

/* Widest view the current room can honestly feed: the window-aspect target
 * capped by the room's own width. Rooms narrower than the window target but
 * wider than 240 (0x100/0x110 rooms — most dungeons and larger interiors)
 * still get true widescreen at their full room width; the presenter
 * pillarboxes the small remainder instead of stretching. */
static s32 Port_WidescreenEffectiveTarget(void) {
    s32 target = Port_Widescreen_TargetViewWidth();
    s32 roomW = (s32)gRoomControls.width;
    if (roomW < target) {
        target = roomW;
    }
    return target;
}

int Port_Widescreen_FallbackNative(void) {
    s32 eff;
    if (!Port_Config_WidescreenEnabled()) {
        return 1;
    }
    if (Port_Widescreen_TargetViewWidth() <= 240) {
        return 1; /* window is 3:2/4:3 — native view already fills it */
    }
    eff = Port_WidescreenEffectiveTarget();
    if (eff <= 240) {
        return 1; /* one-screen room (<=240px): no extra world to reveal */
    }
    /* Room wider than its painted content: the map can't fill even the
     * room-capped view with the camera pinned left -> permanent void strip.
     * Render native 240 (GBA-identical framing, presented pillarboxed).
     * 0 = not scanned yet -> trust width. Key mismatch = cache belongs to
     * the PREVIOUS room -> trust width. */
    if (sWsContentKey == Port_WidescreenRoomKey() && sWsContentPx > 0 && sWsContentPx < eff) {
        return 1;
    }
    return 0;
}

int Port_Widescreen_IsActive(void) {
    /* No message/controlMode gate here: snapping the viewport wide->240 for
     * every textbox (and back on close) was the single worst widescreen UX
     * issue. Dialogue now stays wide — the PPU centers the BG0 textbox via
     * the ws_msg_* knobs (published in Port_Widescreen_UpdateShadows), and
     * overlay screens that replace the map BGs (prologue storybook, pause
     * menu, ...) are caught by Port_Widescreen_ShadowsLive() instead: no
     * matching BG -> no shadow -> present native 240. */
    return (gMain.task == TASK_GAME && Port_Config_WidescreenEnabled() && !Port_Widescreen_FallbackNative()) ? 1 : 0;
}

int Port_Widescreen_EffectiveViewWidth(void) {
    s32 eff;
    if (!Port_Widescreen_IsActive()) {
        return 240;
    }
    eff = Port_WidescreenEffectiveTarget();
    return eff > 240 ? (int)eff : 240;
}

/* Single source of truth for the camera's rest x (see port_widescreen.h).
 * Clamp order (lo wins over hi) matches the GBA branches it replaces. */
int Port_Widescreen_CameraRestX(int target_x) {
    int viewW = Port_Widescreen_EffectiveViewWidth();
    int lo = (int)gRoomControls.origin_x;
    int hi = lo + (int)gRoomControls.width - viewW;
    int want = target_x - viewW / 2;
    if (want > hi) {
        want = hi;
    }
    if (want < lo) {
        want = lo;
    }
    return want;
}

/* True while at least one BG shadow is registered for the current frame —
 * i.e. the map BGs are actually what the PPU is rendering. Overlay screens
 * (prologue storybook, pause/menu subscreens) swap the BG control regs away
 * from the map settings, the shadow match fails, and this drops to 0 so the
 * present path falls back to a native 240 frame instead of showing a black
 * right third. */
int Port_Widescreen_ShadowsLive(void) {
    int i;
    for (i = 0; i < MODE1_GBA_BG_COUNT; i++) {
        if (virtuappu_mode1_ws_shadow[i] != NULL) {
            return 1;
        }
    }
    return 0;
}

int Port_Widescreen_HudRightAnchor(void) {
    if (!Port_Widescreen_IsActive()) {
        return 0;
    }
    if (gHUD.hideFlags != HUD_HIDE_NONE) {
        return 0;
    }
    return 1;
}

static void Port_WidescreenShadow_Populate(int bg_index, u16* mapSpecial, u16* shadow) {
    /* Populate the port-side shadow tilemap the PPU reads for the reveal
     * columns (display x >= MODE1_GBA_BG_CLIP_X). The engine's 32-tile VRAM
     * screenblock only spans ~256 px of world and wraps past that, so the
     * reveal columns can't come from VRAM; we mirror, from gMapData*Special,
     * exactly what the engine fill (ram_sub_080B197C_c) would have placed.
     *
     * Engine ground truth (ram_sub_080B197C_c, common case ydiff>=8):
     *   VRAM buffer cell (row sr, col c) = mapSpecial[2*row16 - 1 + sr][2*col16 + c]
     *   with col16 = xdiff>>4, row16 = ydiff>>4, mapSpecial stride 128 u16.
     * The PPU reads the shadow at [local_row][shadow_idx] where
     *   local_row  = ((line + BGVOFS) % 256)/8 % 32   (== the VRAM buffer row)
     *   shadow_idx = (tile_col - base) % 32           (tile_col == VRAM col at x)
     *
     * Therefore:
     *   (a) the shadow ROW index IS the VRAM buffer row sr, so fill shadow[sr]
     *       with map row (2*row16 - 1 + sr) — NOT (world_row & 31), which reads
     *       camera-shifted wrong rows.
     *   (b) the consumer base MUST equal the VRAM tile_col of the first reveal
     *       column (display CLIP_X) = CLIP_X/8 + (BGHOFS>=8 ? 1 : 0), so
     *       shadow_idx lands on reveal column index d. BGHOFS = (scroll-origin)
     *       & 0xf (UpdateScreenShake); its upper half carries one extra tile.
     * Getting either wrong shifts/wraps the reveal into stale cells — the
     * far-edge garbage. (No residency gate: the area tileset is resident in
     * char VRAM regardless of the 32-col tilemap window, so gating on that
     * window blacked legitimately-loaded reveal tiles. The PPU's 0x7C1F skip
     * still drops genuinely-unstreamed-palette pixels to clean black.) */
    s16 xdiff = (s16)(gRoomControls.scroll_x - gRoomControls.origin_x);
    s16 ydiff = (s16)(gRoomControls.scroll_y - gRoomControls.origin_y);
    s32 row16 = ydiff >> 4;
    /* First reveal world tile col, continuing the native edge:
     * 2*col16 + CLIP/8 + (BGHOFS>=8) == (xdiff>>3) + CLIP/8. */
    s32 ws_base_world_col = (xdiff >> 3) + (MODE1_GBA_BG_CLIP_X / 8);
    virtuappu_mode1_ws_shadow_base_tile[bg_index] = (MODE1_GBA_BG_CLIP_X / 8) + (((xdiff & 0xf) >= 8) ? 1 : 0);

    enum { kMapStride = 128, kMapRows = 128 };
    /* Clamp to the ROOM rect, not just the 128-tile buffer: the buffers are
     * reused across rooms without clearing, so cells past the current room's
     * extent hold the previous room's tiles — sampling them leaked stale
     * graphics into the reveal near room edges. Outside the room = entry 0
     * (transparent; composite force-blacks it), same as GBA's void. */
    s32 room_tiles_w = (s32)gRoomControls.width / 8;
    s32 room_tiles_h = (s32)gRoomControls.height / 8;
    if (room_tiles_w > kMapStride)
        room_tiles_w = kMapStride;
    if (room_tiles_h > kMapRows)
        room_tiles_h = kMapRows;
    for (int sr = 0; sr < MODE1_WS_SHADOW_ROWS; sr++) {
        u16* row_dst = shadow + (size_t)sr * MODE1_WS_SHADOW_COLS;
        s32 world_row = 2 * row16 - 1 + sr;
        if (world_row < 0 || world_row >= room_tiles_h) {
            for (int C = 0; C < MODE1_WS_SHADOW_COLS; C++)
                row_dst[C] = 0;
            continue;
        }
        u16* row_src = mapSpecial + (size_t)world_row * kMapStride;
        for (int C = 0; C < MODE1_WS_SHADOW_COLS; C++) {
            s32 world_col = ws_base_world_col + C;
            u16 entry = (world_col >= 0 && world_col < room_tiles_w) ? row_src[world_col] : (u16)0;
            row_dst[C] = entry;
        }
    }
    virtuappu_mode1_ws_shadow[bg_index] = shadow;
}

/* Which PPU BG index renders a given map: the PPU selects a BG's tilemap by
 * its BGCNT screen_base, so the map whose bgSettings->control screen_base
 * matches gScreen.bgN.control is rendered as BG N. (In the field this is
 * bottom->BG2, top->BG1 — the reverse of the buffer numbering, which is why a
 * hardcoded shadow[1]=bottom/shadow[2]=top was swapped and showed garbage.) */
static int Port_WidescreenPpuBgForControl(u32 control) {
    u32 sb = (control >> 8) & 0x1fu;
    if (((gScreen.bg0.control >> 8) & 0x1fu) == sb)
        return 0;
    if (((gScreen.bg1.control >> 8) & 0x1fu) == sb)
        return 1;
    if (((gScreen.bg2.control >> 8) & 0x1fu) == sb)
        return 2;
    if (((gScreen.bg3.control >> 8) & 0x1fu) == sb)
        return 3;
    return -1;
}

/* Called per-VBlank from src/interrupts.c::UpdateDisplayControls. */
void Port_Widescreen_UpdateShadows(void) {
    for (int i = 0; i < MODE1_GBA_BG_COUNT; i++)
        virtuappu_mode1_ws_shadow[i] = NULL;
    virtuappu_mode1_ws_hud_right_anchor = 0;
    virtuappu_mode1_ws_msg_shift = 0;

    /* Refresh the per-room content-width signal BEFORE the IsActive gate
     * (FallbackNative consumes it). Keyed on area|room|origin; ratcheted so
     * late-streaming map data can only widen, never flip wide->native
     * mid-room. ~16K u16 reads worst case, once per vblank — negligible. */
    if (gMain.task == TASK_GAME && Port_Config_WidescreenEnabled()) {
        u32 key = Port_WidescreenRoomKey();
        if (key != sWsContentKey) {
            sWsContentKey = key;
            sWsContentPx = 0;
        }
        s32 px = Port_WidescreenScanContentPx();
        if (px > sWsContentPx) {
            sWsContentPx = px;
            if (getenv("TMC_WS_TRACE") != NULL) {
                fprintf(stderr, "[ws] area=0x%02x room=0x%02x width=%d content_px=%d -> %s\n",
                        (unsigned)gRoomControls.area, (unsigned)gRoomControls.room, (int)gRoomControls.width,
                        (int)sWsContentPx, Port_Widescreen_FallbackNative() ? "native-240" : "wide");
            }
        }
    }

    if (!Port_Widescreen_IsActive()) {
        return;
    }
    virtuappu_mode1_ws_hud_right_anchor = Port_Widescreen_HudRightAnchor();

    /* Publish the live textbox rect so the PPU can center it (BG0 composes
     * the box for a 240-px canvas). The engine frame (DispMessageFrame /
     * DeleteWindow, src/message.c) spans (W+2) x (H+2) BG0 tiles STARTING at
     * tile (textWindowPosX, textWindowPosY) — border tiles are drawn inward
     * from that corner, not around it. Publishing a rect short of the real
     * frame left the right border outside the shifted copy (overdrawn by the
     * interior) and the bottom border row outside the y-band (torn by the
     * HUD right-anchor remap). Clamp to the native canvas. */
    if ((gMessage.state & MESSAGE_ACTIVE) != 0) {
        int x0 = (int)gMessage.textWindowPosX * 8;
        int x1 = ((int)gMessage.textWindowPosX + (int)gMessage.textWindowWidth + 2) * 8;
        int y0 = (int)gMessage.textWindowPosY * 8;
        int y1 = ((int)gMessage.textWindowPosY + (int)gMessage.textWindowHeight + 2) * 8;
        if (x0 < 0)
            x0 = 0;
        if (x1 > 240)
            x1 = 240;
        if (y0 < 0)
            y0 = 0;
        if (y1 > 160)
            y1 = 160;
        if (x1 > x0 && y1 > y0) {
            virtuappu_mode1_ws_msg_x0 = x0;
            virtuappu_mode1_ws_msg_x1 = x1;
            virtuappu_mode1_ws_msg_y0 = y0;
            virtuappu_mode1_ws_msg_y1 = y1;
            virtuappu_mode1_ws_msg_shift = (Port_Widescreen_EffectiveViewWidth() - 240) / 2;
        }
    }

    if (gMapBottom.bgSettings != NULL) {
        int bg = Port_WidescreenPpuBgForControl(gMapBottom.bgSettings->control);
        if (bg >= 0)
            Port_WidescreenShadow_Populate(bg, gMapDataBottomSpecial, sWsShadowBG1);
    }
    if (gMapTop.bgSettings != NULL) {
        int bg = Port_WidescreenPpuBgForControl(gMapTop.bgSettings->control);
        if (bg >= 0)
            Port_WidescreenShadow_Populate(bg, gMapDataTopSpecial, sWsShadowBG2);
    }
}
#else
void Port_Widescreen_UpdateShadows(void) { /* no-op at native 240 */
}
int Port_Widescreen_FallbackNative(void) {
    return 1;
}
int Port_Widescreen_IsActive(void) {
    return 0;
}
int Port_Widescreen_EffectiveViewWidth(void) {
    return 240;
}
int Port_Widescreen_HudRightAnchor(void) {
    return 0;
}
int Port_Widescreen_ShadowsLive(void) {
    return 0;
}
void Port_Widescreen_SetWindowPixels(int w, int h) {
    (void)w;
    (void)h;
}
int Port_Widescreen_TargetViewWidth(void) {
    return 240;
}
int Port_Widescreen_CameraRestX(int target_x) {
    /* GBA-exact: center at 120, clamp view inside the room. */
    int lo = (int)gRoomControls.origin_x;
    int hi = lo + (int)gRoomControls.width - 240;
    int want = target_x - 120;
    if (want > hi) {
        want = hi;
    }
    if (want < lo) {
        want = lo;
    }
    return want;
}
#endif

/* Touch-overlay helper: 1 when the R button currently has a CONTEXT
 * action (speak/read/check/open/lift/grab/throw/drop/grow/shrink) —
 * i.e. pressing R right now does something specific. ROLL and NONE
 * don't count: roll is available during any walk, so highlighting it
 * would keep the button lit almost permanently and destroy the signal.
 * rActionPlayerState overrides the interact-object frame when set
 * (same precedence the HUD renderer uses). Read by the Android touch
 * overlay to glow the R face button. */
int Port_TouchControls_RActionAvailable(void) {
    unsigned r = gHUD.rActionPlayerState != 0 ? gHUD.rActionPlayerState : gHUD.rActionInteractObject;
    switch (r) {
        case R_ACTION_CANCEL:
        case R_ACTION_DROP:
        case R_ACTION_THROW:
        case R_ACTION_READ:
        case R_ACTION_CHECK:
        case R_ACTION_OPEN:
        case R_ACTION_SPEAK:
        case R_ACTION_GRAB:
        case R_ACTION_LIFT:
        case R_ACTION_GROW:
        case R_ACTION_SHRINK:
            return 1;
        default:
            return 0;
    }
}

/*
 * UpdateSpriteForCollisionLayer — sets OBJ priority bits based on entity's collision layer.
 * (from Thumb asm at 0x08016A04)
 *
 * Table embedded in ROM:
 *   Layer 0: spriteRendering b3=0x80 (priority 2), spriteOrientation flipY=0x80 (priority 2)
 *   Layer 1: same as layer 0
 *   Layer 2: spriteRendering b3=0x40 (priority 1), spriteOrientation flipY=0x40 (priority 1)
 *   Layer 3: same as layer 2
 *
 * This ensures entities on the bottom layer render behind the top BG layer (tree canopy, roofs)
 * and entities on the top layer render in front of the top BG layer.
 */
void UpdateSpriteForCollisionLayer(Entity* entity) {
    if (entity == NULL) {
        return;
    }

    /* On this engine, b3/flipY are 2-bit fields (0..3), not raw bit masks. */
    if (entity->collisionLayer == 2 || entity->collisionLayer == 3) {
        entity->spriteRendering.b3 = 1;
        entity->spriteOrientation.flipY = 1;
    } else {
        entity->spriteRendering.b3 = 2;
        entity->spriteOrientation.flipY = 2;
    }
}

// Area / room data — now provided by src/data/areaMetadata.c
// const AreaHeader gAreaMetadata[256]; // removed: real data in areaMetadata.c
/**
 * GravityUpdate — port of ARM thumb routine at 0x08003FC4.
 * Applies gravity to an entity's z-axis each frame.
 */
u32 GravityUpdate(Entity* entity, u32 gravity) {
    s32 z = (s32)entity->z.WORD - (s32)entity->zVelocity;
    if (z < 0) {
        entity->z.WORD = (u32)z;
        entity->zVelocity = (s32)entity->zVelocity - (s32)gravity;
        return (u32)z;
    } else {
        entity->z.WORD = 0;
        entity->zVelocity = 0;
        return 0;
    }
}

/**
 * BounceUpdate — port of Thumb routine at 0x080044EC.
 * Like GravityUpdate but with bouncing: when entity hits ground,
 * calculates a reduced bounce velocity.
 *
 * Returns: 2 = airborne, 1 = just bounced, 0 = done bouncing
 */
u32 BounceUpdate(Entity* entity, u32 acceleration) {
    s32 z = (s32)entity->z.WORD - (s32)entity->zVelocity;
    if (z < 0) {
        /* Still airborne */
        entity->z.WORD = (u32)z;
        entity->zVelocity = (s32)entity->zVelocity - (s32)acceleration;
        return 2;
    }
    /* Hit ground — calculate bounce */
    entity->z.WORD = 1; /* z=1 signals "just landed" (player can't do certain actions at z!=0) */
    s32 vel = (s32)entity->zVelocity - (s32)acceleration;
    vel = -vel;
    vel >>= 1;
    u32 uvel = (u32)vel;
    uvel = uvel + (uvel >> 2); /* vel * 1.25 — damped bounce */
    u32 result;
    if ((uvel >> 12) >= 0xC) {
        result = 1; /* Still has enough energy to bounce */
    } else {
        result = 0; /* Done bouncing */
        uvel = 0;
    }
    entity->zVelocity = uvel;
    return result;
}

/**
 * GetFacingDirection — port of Thumb routine at 0x080045C4.
 * Calculates direction (0-31 in 5-bit system) from entity A to entity B.
 * Falls through to CalculateDirectionTo in ASM.
 */
u32 GetFacingDirection(Entity* from, Entity* to) {
    return CalculateDirectionTo((s16)from->x.HALF.HI, (s16)from->y.HALF.HI, (s16)to->x.HALF.HI, (s16)to->y.HALF.HI);
}

/**
 * sub_080045B4 — port of Thumb routine at 0x080045B4.
 * Calculates direction from entity position to absolute (targetX, targetY).
 * In ASM: shuffles args then tail-calls ram_CalcCollisionDirection.
 */
u32 sub_080045B4(Entity* entity, u32 targetX, u32 targetY) {
    return CalculateDirectionTo((s16)entity->x.HALF.HI, (s16)entity->y.HALF.HI, (s16)targetX, (s16)targetY);
}

/**
 * EntityInRectRadius — port of Thumb routine at 0x080041A0.
 * Checks if two entities are within rectangular proximity AND share collision layers.
 *
 * Returns 1 if both axis-distance checks pass and entities share layer bits 0-1.
 * A radius of 0 on an axis skips that axis check (always passes).
 */
u32 EntityInRectRadius(Entity* a, Entity* b, u32 xRadius, u32 yRadius) {
    /* Collision layer check: both must share bits 0-1 */
    u8 sharedLayers = a->collisionLayer & b->collisionLayer;
    if ((sharedLayers & 3) == 0)
        return 0;

    /* X axis check */
    if (xRadius != 0) {
        s32 deltaX = (s16)a->x.HALF.HI - (s16)b->x.HALF.HI;
        u32 offsetX = (u32)(deltaX + (s32)xRadius);
        if (offsetX > xRadius * 2)
            return 0;
    }

    /* Y axis check */
    if (yRadius != 0) {
        s32 deltaY = (s16)a->y.HALF.HI - (s16)b->y.HALF.HI;
        u32 offsetY = (u32)(deltaY + (s32)yRadius);
        if (offsetY > yRadius * 2)
            return 0;
    }

    return 1;
}

/**
 * CheckPlayerInRegion — port of Thumb routine at 0x0800293E.
 * Checks whether the player entity is within a room-relative rectangle.
 *
 * The rectangle is centered at (x, y) with half-extents (halfWidth, halfHeight),
 * all relative to the room origin (gRoomControls.origin_x/y).
 */
u32 CheckPlayerInRegion(u32 x, u32 y, u32 halfWidth, u32 halfHeight) {
    s32 playerRelX = (s32)gPlayerEntity.base.x.HALF.HI - (s32)gRoomControls.origin_x;
    s32 playerRelY = (s32)gPlayerEntity.base.y.HALF.HI - (s32)gRoomControls.origin_y;

    /* Unsigned range check: (x - (playerRelX - halfWidth)) must be < 2*halfWidth */
    u32 offsetX = (u32)((s32)x - (playerRelX - (s32)halfWidth));
    if (offsetX >= halfWidth * 2)
        return 0;

    u32 offsetY = (u32)((s32)y - (playerRelY - (s32)halfHeight));
    if (offsetY >= halfHeight * 2)
        return 0;

    return 1;
}

/* ================================================================
 * Tile lookup functions — port of ARM routines in intr.s
 *
 * On GBA these use indirection tables (gActTilePtrs, gMapDataPtrs,
 * gCollisionDataPtrs) that point into gMapBottom / gMapTop fields.
 * On PC we can access the struct fields directly.
 *
 * Layer mapping:
 *   0, 1, 3 → gMapBottom
 *   2       → gMapTop
 *
 * Coordinate conversion (ARM shifts):
 *   lsl #22, lsr #26  ≡  (x & 0x3FF) >> 4  →  tile index 0..63
 * ================================================================ */

static inline MapLayer* GetMapLayerForLayer(u32 layer) {
    return (layer == 2) ? &gMapTop : &gMapBottom;
}

static inline u32 NormalizeTilePos(u32 tilePos) {
    return tilePos & 0x0FFF; /* 64x64 tilemap */
}

static inline u32 TilePosFromRoomTile(u32 roomTileX, u32 roomTileY) {
    return (roomTileX & 0x3F) | ((roomTileY & 0x3F) << 6);
}

/* World pixel → room-relative tile position */
static inline u32 WorldToTilePos(u32 worldX, u32 worldY) {
    u32 roomX = worldX - gRoomControls.origin_x;
    u32 roomY = worldY - gRoomControls.origin_y;
    u32 tileX = ((roomX << 22) >> 26); /* (roomX & 0x3FF) >> 4 */
    u32 tileY = ((roomY << 22) >> 26);
    return tileX + (tileY << 6); /* tileX + tileY * 64 */
}

/* Room pixel → tile position */
static inline u32 RoomToTilePos(u32 roomX, u32 roomY) {
    u32 tileX = ((roomX << 22) >> 26);
    u32 tileY = ((roomY << 22) >> 26);
    return tileX + (tileY << 6);
}

/* ---------- ActTile family ---------- */

/**
 * GetActTileAtTilePos — get act tile at a raw tile position.
 * (port of arm_GetActTileAtTilePos at 0x080B1AE0)
 */
u32 GetActTileAtTilePos(u16 tilePos, u8 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    return ml->actTiles[NormalizeTilePos(tilePos)];
}

/**
 * GetActTileAtRoomTile — get act tile at room tile coordinates.
 * (port of arm_GetActTileAtRoomTile)
 */
u32 GetActTileAtRoomTile(u32 roomTileX, u32 roomTileY, u32 layer) {
    u32 tilePos = TilePosFromRoomTile(roomTileX, roomTileY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    return ml->actTiles[tilePos];
}

/**
 * GetActTileAtRoomCoords — get act tile at room pixel coordinates.
 * (port of arm_GetActTileAtRoomCoords)
 */
u32 GetActTileAtRoomCoords(u32 roomX, u32 roomY, u32 layer) {
    u32 tilePos = RoomToTilePos(roomX, roomY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    return ml->actTiles[NormalizeTilePos(tilePos)];
}

/**
 * GetActTileAtWorldCoords — get act tile at world pixel coordinates.
 * (port of arm_GetActTileAtWorldCoords)
 */
u32 GetActTileAtWorldCoords(u32 worldX, u32 worldY, u32 layer) {
    u32 tilePos = WorldToTilePos(worldX, worldY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    return ml->actTiles[NormalizeTilePos(tilePos)];
}

/**
 * GetActTileAtEntity — get act tile under an entity.
 * (port of arm_GetActTileAtEntity)
 */
u32 GetActTileAtEntity(Entity* entity) {
    u32 tilePos = WorldToTilePos(entity->x.HALF.HI, entity->y.HALF.HI);
    MapLayer* ml = GetMapLayerForLayer(entity->collisionLayer);
    if (ml == NULL)
        return 0;
    return ml->actTiles[NormalizeTilePos(tilePos)];
}

/**
 * GetActTileRelativeToEntity — get act tile at entity position + offset.
 * (port of arm_GetActTileRelativeToEntity)
 */
u32 GetActTileRelativeToEntity(Entity* entity, s32 xOffset, s32 yOffset) {
    u32 worldX = (u16)entity->x.HALF.HI + xOffset;
    u32 worldY = (u16)entity->y.HALF.HI + yOffset;
    u32 tilePos = WorldToTilePos(worldX, worldY);
    MapLayer* ml = GetMapLayerForLayer(entity->collisionLayer);
    if (ml == NULL)
        return 0;
    return ml->actTiles[NormalizeTilePos(tilePos)];
}

/**
 * GetActTileForTileType — convert a tileType to its act tile value.
 * (port of arm_GetActTileForTileType at 0x080B1B54)
 *
 * tileType < 0x4000 → gMapTileTypeToActTile[tileType]
 * tileType >= 0x4000 → gMapSpecialTileToActTile[tileType - 0x4000]
 */
extern const u8 gMapTileTypeToActTile[];
extern const u8 gMapSpecialTileToActTile[];
extern const u16 gUnk_080B7A3E[];
u32 GetActTileForTileType(u32 tileType) {
    if (tileType < 0x4000)
        return GetMapTileTypeToActTile(tileType);
    else
        return gMapSpecialTileToActTile[tileType - 0x4000];
}

/* ---------- CollisionData family ---------- */

/**
 * GetCollisionDataAtTilePos — get collision byte at a raw tile position.
 * (port of arm_GetCollisionDataAtTilePos)
 */
u32 GetCollisionDataAtTilePos(u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    return ml->collisionData[NormalizeTilePos(tilePos)];
}

u32 GetCollisionDataAtRoomTile(u32 roomTileX, u32 roomTileY, u32 layer) {
    return GetCollisionDataAtTilePos(TilePosFromRoomTile(roomTileX, roomTileY), layer);
}

u32 GetCollisionDataAtRoomCoords(u32 roomX, u32 roomY, u32 layer) {
    u32 tilePos = RoomToTilePos(roomX, roomY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    return ml->collisionData[NormalizeTilePos(tilePos)];
}

u32 GetCollisionDataAtWorldCoords(u32 worldX, u32 worldY, u32 layer) {
    u32 tilePos = WorldToTilePos(worldX, worldY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    return ml->collisionData[NormalizeTilePos(tilePos)];
}

u32 GetCollisionDataAtEntity(Entity* entity) {
    u32 tilePos = WorldToTilePos(entity->x.HALF.HI, entity->y.HALF.HI);
    MapLayer* ml = GetMapLayerForLayer(entity->collisionLayer);
    if (ml == NULL)
        return 0;
    return ml->collisionData[NormalizeTilePos(tilePos)];
}

u32 GetCollisionDataRelativeTo(Entity* entity, s32 xOffset, s32 yOffset) {
    u32 worldX = (u16)entity->x.HALF.HI + xOffset;
    u32 worldY = (u16)entity->y.HALF.HI + yOffset;
    u32 tilePos = WorldToTilePos(worldX, worldY);
    MapLayer* ml = GetMapLayerForLayer(entity->collisionLayer);
    if (ml == NULL)
        return 0;
    return ml->collisionData[NormalizeTilePos(tilePos)];
}

/* ---------- TileType family ---------- */

/**
 * GetTileTypeAtTilePos — get tile type at a raw tile position.
 * (port of arm_GetTileTypeAtTilePos at 0x080B1A60)
 *
 * Reads mapData[tilePos] → tileIndex.
 * If tileIndex >= 0x4000 → return tileIndex (special tile reference).
 * Otherwise → return tileTypes[tileIndex].
 */
u32 GetTileTypeAtTilePos(u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    u16 tileIndex = ml->mapData[NormalizeTilePos(tilePos)];
    if (tileIndex >= 0x4000)
        return tileIndex;
    return ml->tileTypes[tileIndex];
}

u32 GetTileTypeAtRoomTile(u32 roomTileX, u32 roomTileY, u32 layer) {
    return GetTileTypeAtTilePos(TilePosFromRoomTile(roomTileX, roomTileY), layer);
}

u32 GetTileTypeAtRoomCoords(u32 roomX, u32 roomY, u32 layer) {
    u32 tilePos = RoomToTilePos(roomX, roomY);
    return GetTileTypeAtTilePos(tilePos, layer);
}

u32 GetTileTypeAtWorldCoords(s32 worldX, s32 worldY, u32 layer) {
    u32 tilePos = WorldToTilePos((u32)worldX, (u32)worldY);
    return GetTileTypeAtTilePos(tilePos, layer);
}

u32 GetTileTypeAtEntity(Entity* entity) {
    u32 tilePos = WorldToTilePos(entity->x.HALF.HI, entity->y.HALF.HI);
    return GetTileTypeAtTilePos(tilePos, entity->collisionLayer);
}

u32 GetTileTypeRelativeToEntity(Entity* entity, s32 xOffset, s32 yOffset) {
    u32 worldX = (u16)entity->x.HALF.HI + xOffset;
    u32 worldY = (u16)entity->y.HALF.HI + yOffset;
    u32 tilePos = WorldToTilePos(worldX, worldY);
    return GetTileTypeAtTilePos(tilePos, entity->collisionLayer);
}

/* ---------- sub_080B1B84 / sub_080B1BA4 — tile data lookup ---------- */

/**
 * sub_080B1B84 — look up tile property data (u16) from gUnk_08000360 table.
 * (port of arm_sub_080B1B84 at 0x080B1B84)
 *
 * Calls GetTileTypeAtTilePos, then indexes into gUnk_08000360 or gUnk_080B7A3E
 * (based on whether tileType < 0x4000 or not) as a u16 array.
 */
u32 sub_080B1B84(u32 tilePos, u32 layer) {
    u32 tileType = GetTileTypeAtTilePos(tilePos, layer);
    const u16* table;
    if (tileType < 0x4000) {
        return Port_GetTileTypeProperty(tileType);
    } else {
        table = gUnk_080B7A3E;
    }
    return table[tileType & 0x3FFF];
}

/**
 * sub_080B1BA4 — like sub_080B1B84 but AND result with a mask (r2).
 * (port of arm_sub_080B1BA4 at 0x080B1BA4)
 *
 * #PC_PORT issue #139: when the lantern queries a tile for the "ignitable"
 * flag (mask 0x40), it passes the player's collisionLayer. In some rooms
 * (Grimblade dojo room 5 area 0x25) the lightable TILE_TYPE_117 tiles only
 * live on LAYER_TOP while the player walks on LAYER_BOTTOM, so the check
 * silently fails. If the queried layer returns 0 for the masked flag, fall
 * back to the OTHER layer — keeps existing behaviour when the property is
 * already present on the queried layer.
 */
u32 sub_080B1BA4(u32 tilePos, u32 layer, u32 mask) {
    u32 tileType = GetTileTypeAtTilePos(tilePos, layer);
    u32 r;
    if (tileType < 0x4000) {
        r = Port_GetTileTypeProperty(tileType) & mask;
    } else {
        r = gUnk_080B7A3E[tileType & 0x3FFF] & mask;
    }
#ifdef PC_PORT
    if (r == 0 && Port_ShouldFallbackTilePropertyLayer(mask)) {
        u32 other = (layer == 2) ? 1 : 2;
        u32 tt2 = GetTileTypeAtTilePos(tilePos, other);
        r = (tt2 < 0x4000) ? (Port_GetTileTypeProperty(tt2) & mask)
                           : (gUnk_080B7A3E[tt2 & 0x3FFF] & mask);
    }
#endif
    return r;
}

/* ---------- SetCollisionData, SetActTileAtTilePos, GetTileIndex, SetTile, CloneTile ---------- */

extern const u8 gMapSpecialTileToActTile[];
extern const u8 gMapSpecialTileToCollisionData[];
extern const u8 gMapTileTypeToCollisionData[];
extern void UnregisterInteractTile(u32, u32);
extern void RegisterInteractTile(u32, u32, u32);

/**
 * SetCollisionData — set collision data at a tile position.
 * (port of Thumb veneer at 0x08000148)
 *
 * gCollisionDataPtrs layout (by layer):
 *   0 → gMapBottom.collisionData
 *   1 → gMapBottom.collisionData
 *   2 → gMapTop.collisionData
 *   3 → gMapBottom.collisionData
 */
void SetCollisionData(u32 collisionData, u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return;
    ml->collisionData[NormalizeTilePos(tilePos)] = collisionData;
}

/**
 * SetActTileAtTilePos — set act tile at a tile position.
 * (port of Thumb veneer at 0x080001D0)
 *
 * gActTilePtrs layout (by layer):
 *   0 → gMapBottom.actTiles
 *   1 → gMapBottom.actTiles
 *   2 → gMapTop.actTiles
 *   3 → gMapBottom.actTiles
 */
void SetActTileAtTilePos(u32 actTile, u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return;
    ml->actTiles[NormalizeTilePos(tilePos)] = actTile;
}

/**
 * GetTileIndex — get the tile index (mapData value) at a tile position.
 * (port of Thumb veneer at 0x080001DA)
 *
 * Returns mapData[tilePos] for the specified layer.
 */
u32 GetTileIndex(u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    if (ml == NULL)
        return 0;
    return ml->mapData[NormalizeTilePos(tilePos)];
}

/**
 * SetTile — set a tile at a position and update collision/actTile data.
 * (port of Thumb veneer at 0x0800015C)
 *
 * If tileIndex >= 0x4000 (special tile):
 *   - Uses gMapSpecialTileToActTile / gMapSpecialTileToCollisionData
 *   - Calls UnregisterInteractTile + RegisterInteractTile for tile entity management
 * Else (normal tile):
 *   - Looks up tileType from tileTypes[tileIndex]
 *   - Uses gMapTileTypeToActTile / gMapTileTypeToCollisionData
 *   - Calls UnregisterInteractTile
 */
void SetTile(u32 tileIndex, u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    u32 pos = NormalizeTilePos(tilePos);
    u16 oldTile;

    if (ml == NULL)
        return;

    oldTile = ml->mapData[pos];
    ml->mapData[pos] = (u16)tileIndex;

    if (tileIndex >= 0x4000) {
        /* Special tile */
        u32 specialIdx = tileIndex - 0x4000;
        SetActTileAtTilePos(gMapSpecialTileToActTile[specialIdx], pos, layer);
        SetCollisionData(gMapSpecialTileToCollisionData[specialIdx], pos, layer);
        UnregisterInteractTile(pos, layer);
        RegisterInteractTile(oldTile, pos, layer);
    } else {
        /* Normal tile — look up tile type from tileTypes table */
        u16 tileType = ml->tileTypes[tileIndex];
        SetActTileAtTilePos(GetMapTileTypeToActTile(tileType), pos, layer);
        SetCollisionData(gMapTileTypeToCollisionData[tileType], pos, layer);
        UnregisterInteractTile(pos, layer);
    }
}

/**
 * CloneTile — look up the tile index from a tile type and call SetTile.
 * (port of Thumb veneer at 0x08000152)
 *
 * Looks up tileIndices[tileType] for the given layer to get the actual tile index,
 * then falls through to SetTile(tileIndex, tilePos, layer).
 */
void CloneTile(u32 tileType, u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    u16 tileIndex = ml->tileIndices[tileType];
    SetTile(tileIndex, tilePos, layer);
}

/* ---------- ResolveCollisionLayer, CheckOnLayerTransition, UpdateCollisionLayer ---------- */

/**
 * Transition tile table entry for layer transitions.
 * (from ARM asm at 0x08016A90 / gTransitionTiles)
 */
typedef struct {
    u16 actTile;
    u8 preservedLayer;
    u8 destinationLayer;
} TransitionTileEntry;

static const TransitionTileEntry sTransitionTiles[] = {
    { 0x0A, 2, 1 }, { 0x09, 2, 1 }, { 0x0C, 1, 2 }, { 0x0B, 1, 2 },
    { 0x52, 3, 3 }, { 0x27, 3, 3 }, { 0x26, 3, 3 }, { 0x0000, 0, 0 }, /* terminator */
};

/**
 * Table of act tiles to check for ResolveCollisionLayer.
 * (from ARM asm above gTransitionTiles)
 */
static const TransitionTileEntry sResolveCollisionLayerTiles[] = {
    { 0x2A, 3, 3 }, { 0x2D, 3, 3 }, { 0x2B, 3, 3 }, { 0x2C, 3, 3 },   { 0x4C, 3, 3 },
    { 0x4E, 3, 3 }, { 0x4D, 3, 3 }, { 0x4F, 3, 3 }, { 0x0000, 0, 0 }, /* terminator */
};

/**
 * ResolveCollisionLayer — determine which collision layer an entity should be on
 * based on the tile under it.
 * (port of ARM asm at 0x08016A30)
 */
u32 ResolveCollisionLayer(Entity* entity) {
    if (entity->collisionLayer == 0) {
        u32 tileType = GetTileTypeAtWorldCoords(entity->x.HALF.HI, entity->y.HALF.HI, 2);
        u8 newLayer = 1;
        if (tileType != 0) {
            u32 actTile = GetActTileForTileType(tileType);
            newLayer = 2;
            /* Check against the resolve table */
            const TransitionTileEntry* p = sResolveCollisionLayerTiles;
            while (p->actTile != 0) {
                if (actTile == p->actTile) {
                    newLayer = p->destinationLayer;
                    break;
                }
                p++;
            }
        }
        entity->collisionLayer = newLayer;
    }
    UpdateSpriteForCollisionLayer(entity);
    return 0;
}

/**
 * CheckOnLayerTransition — check if entity is standing on a layer-transition tile.
 * (port of ARM asm at 0x08016A68)
 *
 * Returns the act tile at the entity's position (preserved in r0 in ARM code).
 */
u32 CheckOnLayerTransition(Entity* entity) {
    u32 actTile = GetActTileAtEntity(entity);
    const TransitionTileEntry* p = sTransitionTiles;
    while (p->actTile != 0) {
        if (p->actTile == actTile) {
            entity->collisionLayer =
                Port_ApplyCollisionLayerTransition(entity->collisionLayer, p->preservedLayer, p->destinationLayer);
            return actTile;
        }
        p++;
    }
    return actTile;
}

/**
 * UpdateCollisionLayer — check layer transition and update sprite priority.
 * (port of ARM asm at 0x08016AB4)
 */
u32 UpdateCollisionLayer(Entity* entity) {
    u32 actTile = CheckOnLayerTransition(entity);
    UpdateSpriteForCollisionLayer(entity);
    return actTile;
}

/**
 * GetTileHazardType — check if the entity is standing on a hazardous tile.
 * (port of ARM asm at 0x080043E8)
 *
 * Hazard list:
 *   0x0D → 1 (hole)
 *   0x10 → 2 (lava)
 *   0x11 → 2 (lava)
 *   0x5A → 3 (swamp)
 *   0x13 → 4 (water/drown)
 *
 * Returns 0 if entity has no action, is in air (z < 0), or not on a hazard tile.
 */
u32 GetTileHazardType(Entity* entity) {
    static const struct {
        u16 actTile;
        u16 hazardType;
    } sHazardList[] = {
        { 0x0D, 1 }, { 0x10, 2 }, { 0x11, 2 }, { 0x5A, 3 }, { 0x13, 4 }, { 0x00, 0 }, /* terminator */
    };

    if (entity->action == 0)
        return 0;

    u32 actTile = UpdateCollisionLayer(entity);

    /* Check z position — if entity is in the air, no hazard */
    if ((s16)entity->z.HALF.HI < 0)
        return 0;

    if (actTile == 0)
        return 0;

    for (int i = 0; sHazardList[i].actTile != 0; i++) {
        if (actTile == sHazardList[i].actTile)
            return sHazardList[i].hazardType;
    }
    return 0;
}

RoomHeader* gAreaRoomHeaders[256];
void* gAreaRoomMaps[256];
void* gAreaTable[256];
void* gAreaTileSets[256];
void* gAreaTiles[256];

// Function pointer tables
/* gSubtasks -- defined in port_stubs.c as proper function pointer table */
// ButtonUIElement_Actions — defined in ui.c with proper function pointers
// EzloNagUIElement_Actions — defined in ui.c with proper function pointers
// gUIElementDefinitions — defined in ui.c with proper UIElementDefinition type

/* Native function-pointer tables — the original `void*[16]` zero-stubs were
 * an unfinished placeholder. On GBA the table is 5 packed 4-byte function
 * pointers in ROM; the C dispatcher (`Subtask_FastTravel_Functions[idx]()`)
 * strides 8 bytes per index on x86-64 and a NULL stub array means every
 * dispatch hits NULL → SIGSEGV with RIP=0. The fix mirrors gleerok 9d5f55a5
 * — a real native array of decompiled C functions. */
extern void Subtask_FastTravel_0(void);
extern void Subtask_FastTravel_1(void);
extern void Subtask_FastTravel_2(void);
extern void Subtask_FastTravel_3(void);
extern void Subtask_FastTravel_4(void);
void (*const Subtask_FastTravel_Functions[])(void) = {
    Subtask_FastTravel_0, Subtask_FastTravel_1, Subtask_FastTravel_2, Subtask_FastTravel_3, Subtask_FastTravel_4,
};
/* Subtask_MapHint_Functions — the matching usage in src/subtask/subtaskMapHint.c
 * defines a *static local* array of the same name with proper native
 * function pointers, so this global is dead code. Kept only because
 * stubs_autogen.c still mentions the symbol; once stubs_autogen is
 * regenerated the entire `void* []` line below can go away. */
void* Subtask_MapHint_Functions[16];

// Exit lists / transitions — now provided by src/data/transitions.c
// gExitLists and gExitList_RoyalValley_ForestMaze removed

// Various game data
u32 gFixedTypeGfxData[528];
// gCaveBorderMapData — now provided by src/data/caveBorderMapData.c
// gOverworldLocations — now provided by src/data/areaMetadata.c
u16* gMoreSpritePtrs[16];
u8 gExtraFrameOffsets[4352];
u8 gShakeOffsets[256];
u16 gDungeonNames[64];
/* gFigurines — now provided by port/port_figurines.c (Figurine[137] table,
 * resolved from ROM after Port_LoadRom). The 512-byte stub here used to
 * SEGV the figurine viewer (#57): every fig->pal / fig->gfx was NULL. */
void* gLilypadRails[32];
// gMapActTileToSurfaceType — now provided by src/data/mapActTileToSurfaceType.c
/* gPalette_549 is the start of a 26-palette contiguous block in the GBA
 * `gfxAndPalettes` blob. Mt Crenel's weather-change manager reads
 * `gPalette_549 + 0xD0` (i.e., 13 palettes past the start) as `palette2`
 * for cross-fade — which only works because the GBA linker placed
 * gPalette_549..gPalette_574 sequentially. The PC port previously stubbed
 * this as a single 32-byte buffer, so the +0xD0 read walked off into garbage
 * and the Mt Crenel mountaintop terrain rendered with random colors (#34).
 * Allocate the full 416-color (832-byte) block; port_rom.c populates it
 * from gGlobalGfxAndPalettes after ROM load. */
u16 gPalette_549[0x1A0];
u32* gTranslations[16];
// gWallMasterScreenTransitions — now provided by src/data/screenTransitions.c
u16* gZeldaFollowerText[8];
Frame* gSpriteAnimations_322[128];
u32 gSpriteAnimations_GhostBrothers[64];
DiggingCaveEntranceTransition gDiggingCaveEntranceTransition;
u8 RupeeKeyDigits[16];

// Player macros — now provided by src/data/data_080046A4.c

// Entity data (ROM data — all zero-init stubs).
// On Linux the LTO type gate (pc_lto_type_check) compares these against the
// game's `extern EntityData name` declarations, so back a correctly-typed
// EntityData symbol with oversized storage via an ELF alias. Mach-O (macOS)
// has no `alias` attribute — and runs no gate — so define plain oversized
// storage there (the pre-gate form; the linker matches purely by name).
/* The Linux alias gives debuggers a typed EntityData view of the storage.
 * Under ASan the alias's 16-byte type poisons a redzone INSIDE the real
 * storage, so every sentinel-walk past the first entry (room.c
 * LoadRoomEntityList) and the Port_InitDataStubs memcpy report a bogus
 * global-buffer-overflow. Use the plain byte array there. */
#if defined(__SANITIZE_ADDRESS__)
#define TMC_ASAN_BUILD 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TMC_ASAN_BUILD 1
#endif
#endif
#if defined(__linux__) && !defined(TMC_ASAN_BUILD)
#define ENTITY_DATA_STUB(name, bytes)                     \
    u8 name##_storage[bytes] __attribute__((aligned(4))); \
    extern EntityData name __attribute__((alias(#name "_storage")))
#else
#define ENTITY_DATA_STUB(name, bytes) u8 name[bytes] __attribute__((aligned(4)))
#endif
/* Real ROM lists are 80 and 96 bytes (4 and 5 entities + 0xFF terminator);
 * 64 bytes cut the terminator off both. */
ENTITY_DATA_STUB(Entities_HouseInteriors1_Mayor_080D6210, 80);
ENTITY_DATA_STUB(Entities_MinishPaths_MayorsCabin_gUnk_080D6138, 96);
ENTITY_DATA_STUB(UpperInn_Din, 64);
ENTITY_DATA_STUB(UpperInn_Farore, 64);
ENTITY_DATA_STUB(UpperInn_Nayru, 64);
ENTITY_DATA_STUB(UpperInn_NoDin, 64);
ENTITY_DATA_STUB(UpperInn_NoFarore, 64);
ENTITY_DATA_STUB(UpperInn_NoNayru, 64);
ENTITY_DATA_STUB(UpperInn_Oracles, 64);
ENTITY_DATA_STUB(gUnk_additional_8_DeepwoodShrine_StairsToB1, 64);
ENTITY_DATA_STUB(gUnk_additional_8_HouseInteriors1_Library1F, 64);
ENTITY_DATA_STUB(gUnk_additional_8_HouseInteriors3_BorlovEntrance, 64);
ENTITY_DATA_STUB(gUnk_additional_8_HyruleCastle_3, 64);
/* gUnk_additional_8_MelarisMine_Main needs at least 96 bytes for the
 * post-state-change entity list (2 mountain minishes + Melari himself
 * + 2 cutscene-sword objects + terminator). The smaller default size
 * truncated past Melari, leaving the room with garbage entities the
 * runtime parsed as kind=GROUND_ITEM (visible as the "double heart
 * containers" in #42). */
ENTITY_DATA_STUB(gUnk_additional_8_MelarisMine_Main, 128);
ENTITY_DATA_STUB(gUnk_additional_8_PalaceOfWinds_GyorgTornado, 64);
/* ASan caught this at startup: Port_InitDataStubs (data_stubs_autogen.c:653)
 * copies 0x50 (80) bytes here from ROM, but the storage was sized 64 — the
 * memcpy overflowed 16 bytes into gUnk_additional_9_HouseInteriors2_Percy. */
ENTITY_DATA_STUB(gUnk_additional_9_HouseInteriors1_Library1F, 128);
ENTITY_DATA_STUB(gUnk_additional_9_HouseInteriors2_Percy, 64);
ENTITY_DATA_STUB(gUnk_additional_9_HouseInteriors3_BorlovEntrance, 64);
ENTITY_DATA_STUB(gUnk_additional_9_MelarisMine_Main, 64);
ENTITY_DATA_STUB(gUnk_additional_9_PalaceOfWinds_GyorgTornado, 64);
ENTITY_DATA_STUB(gUnk_additional_a_CaveOfFlamesBoss_Main, 64);
ENTITY_DATA_STUB(gUnk_additional_a_DeepwoodShrineBoss_Main, 64);
ENTITY_DATA_STUB(gUnk_additional_a_HouseInteriors2_Percy, 64);
ENTITY_DATA_STUB(gUnk_additional_a_HouseInteriors3_BorlovEntrance, 64);
ENTITY_DATA_STUB(gUnk_additional_a_TempleOfDroplets_BigOcto, 64);
ENTITY_DATA_STUB(gUnk_additional_c_HouseInteriors2_Romio, 64);
#undef ENTITY_DATA_STUB
u32 Enemies_LakeHylia_Main;
u32 Area_HyruleTown[16];

// Script data (ROM data — zero-init stubs so linker resolves them)
u16 script_08012C48;
u16 script_08015B14;
u16 script_BedAtSimons;
u16 script_BedInLinksRoom;
#define SCRIPT_STUB(name) Script name
SCRIPT_STUB(script_BombMinish);
SCRIPT_STUB(script_BombMinishKinstone);
u16 script_BusinessScrubIntro[16];
u16 script_CutsceneMiscObjectSwordInChest;
u16 script_CutsceneMiscObjectTheLittleHat;
u16 script_EzloTalkOcarina[16];
SCRIPT_STUB(script_ForestMinish1);
SCRIPT_STUB(script_ForestMinish2);
SCRIPT_STUB(script_ForestMinish3);
SCRIPT_STUB(script_ForestMinish4);
SCRIPT_STUB(script_ForestMinish5);
SCRIPT_STUB(script_ForestMinish6);
SCRIPT_STUB(script_ForestMinish7);
SCRIPT_STUB(script_ForestMinish8);
SCRIPT_STUB(script_ForestMinish9);
SCRIPT_STUB(script_ForestMinish10);
SCRIPT_STUB(script_ForestMinish11);
SCRIPT_STUB(script_ForestMinish12);
SCRIPT_STUB(script_ForestMinish13);
SCRIPT_STUB(script_ForestMinish14);
SCRIPT_STUB(script_ForestMinish15);
SCRIPT_STUB(script_ForestMinish16);
SCRIPT_STUB(script_ForestMinish17);
SCRIPT_STUB(script_ForestMinish18);
SCRIPT_STUB(script_ForestMinish19);
SCRIPT_STUB(script_ForestMinish20);
SCRIPT_STUB(script_ForestMinish21);
#undef SCRIPT_STUB
u16 script_MazaalBossObjectMazaal[16];
u16 script_MazaalMacroDefeated[16];
u8 script_MinishVillageObjectLeftStoneOpening[4];
u8 script_MinishVillageObjectRightStoneOpening[4];
u16 script_PlayerAtDarkNut1[16], script_PlayerAtDarkNut2[16], script_PlayerAtDarkNut3[16];
u16 script_PlayerAtMadderpillar[16];
u16 script_PlayerGetElement[16];
u32 script_PlayerIntro;
void* script_PlayerSleepingInn[8];
u32 script_PlayerWakeAfterRest;
u32 script_PlayerWakingUpAtSimons;
u32 script_PlayerWakingUpInHyruleCastle;
u8 script_Rem[4];
u16 script_Stockwell;
u16 script_StockwellBuy[16];
u16 script_StockwellDogFood[16];
u8 script_TalonGotKey;
u16 script_WindTribespeople6;
u16 script_ZeldaMagic;

// Unk_08133368 — already defined in data_stubs_autogen.c

/* ================================================================
 * Enemy update system — ported from ARM Thumb assembly (enemy.s,
 * code_08001A7C.s, code_080043E8.s)
 * ================================================================ */

extern void DeleteThisEntity(void);
extern u32 EntityDisabled(Entity*);
extern void DrawEntity(Entity*);
extern void Knockback1(Entity*);
extern void Knockback2(Entity*);
extern void EnemyDetachFX(Entity*);
extern void UpdateAnimationVariableFrames(Entity*, u32);
extern void CreatePitFallFX(Entity*);
extern void CreateDrownFX(Entity*);
extern void CreateLavaDrownFX(Entity*);
extern void CreateSwampDrownFX(Entity*);
extern u32 Random(void);
extern void (*gEnemyFunctions[])(Entity*);

/*
 * sub_080028E0 — Decrement iframes towards zero.
 * Port of 0x080028E0 from code_08001A7C.s.
 */
void sub_080028E0(Entity* entity) {
    if (entity->iframes > 0)
        entity->iframes--;
    else if (entity->iframes < 0)
        entity->iframes++;
}

/*
 * GetRandomByWeight — pick a random index from a probability table.
 * Port of 0x080028F4 from code_08001A7C.s.
 */
u32 GetRandomByWeight(const u8* weights) {
    u32 r = Random() & 0xFF;
    u32 i = 0;
    for (;;) {
        r -= weights[i];
        i++;
        if ((s32)r < 0)
            break;
    }
    return i - 1;
}

/*
 * CheckRectOnScreen — test if a rectangle (centred at x,y with
 * half-extents halfW, halfH) overlaps the visible screen.
 * Port of 0x0800290E from code_08001A7C.s.
 */
u32 CheckRectOnScreen(s32 x, s32 y, u32 halfW, u32 halfH) {
    s32 sx = gRoomControls.scroll_x - gRoomControls.origin_x;
    u32 dx = (u32)(x - sx + halfW);
    /* Runtime-gated widescreen: when the WIP option is off or this room
     * falls back to native, cull against 240 so engine behaviour matches the
     * 240x160 frame we present. */
    if (dx >= halfW * 2 + (u32)Port_Widescreen_EffectiveViewWidth())
        return 0;
    s32 sy = gRoomControls.scroll_y - gRoomControls.origin_y;
    u32 dy = (u32)(y - sy + halfH);
    if (dy >= halfH * 2 + 0xA0)
        return 0;
    return 1;
}
