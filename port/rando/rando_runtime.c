/*
 * port/rando/rando_runtime.c — native randomizer runtime integrations.
 */

#include "common.h"
#include "flags.h"
#include "item.h"
#include "save.h"
#include "sound.h"
#include "transitions.h"
#include "pauseMenu.h"
#include "windcrest.h"
#include "main.h"
#include "rando/rando.h"
#include "rando/rando_runtime.h"
#include "rando/rando_newfile.h"
#include "port_softslots.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define RANDO_WINDCREST_SHIFT 24
static_assert(WINDCREST_MT_CRENEL == 24 && WINDCREST_MINISH_WOODS == 31, "windcrest bit layout changed");

/* First-pickup "seen" latches */
static const u8 kSeenItems[] = {
    ITEM_NONE,         ITEM_MAP,       ITEM_KINSTONE_BAG,    ITEM_SHELLS,      ITEM_DUNGEON_MAP, ITEM_COMPASS,
    ITEM_BIG_KEY,      ITEM_SMALL_KEY, ITEM_RUPEE1,          ITEM_RUPEE5,      ITEM_RUPEE20,     ITEM_RUPEE50,
    ITEM_RUPEE100,     ITEM_RUPEE200,  ITEM_KINSTONE,        ITEM_BOMBS5,      ITEM_ARROWS5,     ITEM_HEART,
    ITEM_FAIRY,        ITEM_SHELLS30,  ITEM_HEART_CONTAINER, ITEM_HEART_PIECE, ITEM_WALLET,      ITEM_BOMBBAG,
    ITEM_LARGE_QUIVER, ITEM_BOMBS10,   ITEM_BOMBS30,         ITEM_ARROWS10,    ITEM_ARROWS30,
};

static const u16 kDigFlags[] = {
    FLAG_BANK_1 + KUMOUE_01_T4,      FLAG_BANK_1 + KUMOUE_01_T5,      FLAG_BANK_1 + KUMOUE_01_T6,
    FLAG_BANK_1 + KUMOUR_01_K0,      FLAG_BANK_1 + KUMOUR_01_K1,      FLAG_BANK_1 + KUMOUR_01_K2,
    FLAG_BANK_1 + KUMOUR_01_K3,      FLAG_BANK_2 + KOBITOYAMA_00_R00, FLAG_BANK_2 + KOBITOYAMA_00_R02,
    FLAG_BANK_2 + KOBITOYAMA_00_R05, FLAG_BANK_2 + KOBITOYAMA_00_R07,
};

/* Randomizer tables are compiled from the USA-baseline named flags. Convert
 * their composed bank+ordinal values to the active ROM's flag numbering before
 * touching the universal build's save buffer. Global flags are region-stable. */
static void WriteBaselineFlag(u32 flag) {
    u32 bank;
    u32 ordinal;
    int i;

    if (flag < FLAG_BANK_1) {
        WriteBit(gSave.flags, flag);
        return;
    }

    bank = FLAG_BANK_1;
    for (i = LOCAL_BANK_12; i >= LOCAL_BANK_1; i--) {
        if (flag >= gLocalFlagBanks[i]) {
            bank = gLocalFlagBanks[i];
            break;
        }
    }
    ordinal = flag - bank;
#if defined(PC_PORT) && defined(MULTI_REGION)
    ordinal = Port_RemapBaselineLocalFlag(bank, ordinal);
    if (ordinal == PORT_FLAG_REMAP_INVALID) {
        return;
    }
#endif
    WriteBit(gSave.flags, bank + ordinal);
}

static void ApplyStartInventory(u64 seed) {
    RandomizerSettings settings = Rando_GetSettings();
    u32 granted = 0;
    if (settings.start_sword) {
        SetInventoryValue(ITEM_SMITH_SWORD, 1);
        granted++;
    }
    if (granted != 0) {
        fprintf(stderr, "[RANDO] start inventory: %u item(s) applied\n", granted);
    }
}

static void ApplyCrests(u64 seed) {
    RandomizerSettings settings = Rando_GetSettings();
    if (settings.early_crests) {
        gSave.windcrests |= 0x18u << RANDO_WINDCREST_SHIFT;
        fprintf(stderr, "[RANDO] wind crests pre-opened: 0x18\n");
    }
}

static void ApplyInstantText(void) {
    RandomizerSettings settings = Rando_GetSettings();
    if (settings.instant_text) {
        gSave.msg_speed = 2;
        gSaveHeader->msg_speed = 2;
        fprintf(stderr, "[RANDO] instant text enabled\n");
    }
}

static void ApplyStorySkip(void) {
    SetGlobalFlag(START);
    SetGlobalFlag(EZERO_1ST);
    SetGlobalFlag(TABIDACHI);
    SetGlobalFlag(OUTDOOR);
    SetGlobalFlag(ENTRANCE_0);
    SetLocalFlagByBankB(FLAG_BANK_1, MORI_00_KOBITO);
    SetLocalFlagByBankB(FLAG_BANK_1, MORI_ENTRANCE_1ST);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_01_ZELDA);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_1);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_2);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_WAKAGI_3);
    SetLocalFlagByBankB(FLAG_BANK_1, SOUGEN_06_AKINDO);
    SetLocalFlagByBankB(FLAG_BANK_1, CASTLE_04_MEZAME);
    SetLocalFlagByBankB(FLAG_BANK_1, MACHI_01_DEMO);
    SetLocalFlagByBankB(FLAG_BANK_2, MHOUSE15_OP1ST);
    SetLocalFlagByBankB(FLAG_BANK_2, M_PRIEST_TALK);
    SetLocalFlagByBankB(FLAG_BANK_2, M_ELDER_TALK1ST);
    SetLocalFlagByBankB(FLAG_BANK_2, M_PRIEST_MOVE);
    SetLocalFlagByBankB(FLAG_BANK_2, KOBITO_MORI_1ST);
    SetLocalFlagByBankB(FLAG_BANK_5, LV1_0B_WALK);
    fprintf(stderr, "[RANDO] story skip: intro flags set (post-Ezlo start)\n");
}

static void ApplyWorldOpen(void) {
    RandomizerSettings settings = Rando_GetSettings();
    if (settings.open_world) {
        SetGlobalFlag(KUMOTATSUMAKI);
        SetGlobalFlag(WARP_EVENT_END);
        SetGlobalFlag(TINGLE_TALK1ST);
        SetGlobalFlag(MIZUKAKI_START);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_00);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_01);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_02);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_03);
        SetLocalFlagByBankB(FLAG_BANK_1, BEANDEMO_04);
        SetLocalFlagByBankB(FLAG_BANK_1, YAMA_04_BOMBWALL0);
        fprintf(stderr, "[RANDO] world open: speed-up flags applied\n");
    }
}

static void ApplyBaselineNewFile(u64 seed) {
    size_t count = 0;
    const u16* flags = Rando_NewFile_BaselineFlags(&count);
    size_t i;

    for (i = 0; i < count; i++) {
        WriteBaselineFlag(flags[i]);
    }

    SetLocalFlagByBankB(FLAG_BANK_10, LV6_SOTO_01_00);
    SetLocalFlagByBankB(FLAG_BANK_10, LV6_SOTO_01_01);
    SetLocalFlagByBankB(FLAG_BANK_10, LV6_SOTO_01_02);
    SetLocalFlagByBankB(FLAG_BANK_10, LV6_35_00);

    // Skip cucco rounds, leaving 1 round
    SetGlobalFlag(ANJU_LV_BIT0);
    SetGlobalFlag(ANJU_LV_BIT3);

    for (i = 0; i < (u32)(sizeof(kSeenItems) / sizeof(kSeenItems[0])); i++) {
        if (GetInventoryValue(kSeenItems[i]) == 0) {
            SetInventoryValue(kSeenItems[i], 1);
        }
    }

    // Unconditional QoL figurines
    for (i = 0; i < RANDO_NEWFILE_FIGURINE_BYTES; i++) {
        gSave.figurines[i] |= kRandoNewFileFigurines[i];
    }

    gSave.windcrests |= RANDO_NEWFILE_MAP_REVEAL_MASK;
    gSave.map_hints |= RANDO_NEWFILE_MAP_HINTS_MASK;
    gSave.saved_status.overworld_map_x = RANDO_NEWFILE_WORLDMAP_X;
    gSave.saved_status.overworld_map_y = RANDO_NEWFILE_WORLDMAP_Y;

    fprintf(stderr, "[RANDO] new-file baseline: %u flag(s) + QoL state applied\n", (unsigned)count);
}

static void ApplyLocationDisableFlags(void) {
    WriteBaselineFlag(FLAG_BANK_2 + BILL09_YADO2F_POEMN);
    WriteBaselineFlag(FLAG_BANK_8 + LV4_0a_TSUBO);
    WriteBaselineFlag(FLAG_BANK_2 + MHOUSE2_02_KEY);
    WriteBaselineFlag(FLAG_BANK_3 + MOGURA_51_00);
    WriteBaselineFlag(FLAG_BANK_3 + MOGURA_51_01);
    WriteBaselineFlag(FLAG_BANK_1 + HIKYOU_00_M2);
    WriteBaselineFlag(FLAG_BANK_1 + HIKYOU_00_T1);
    WriteBaselineFlag(FLAG_BANK_1 + LOST_00_ENTER);
    WriteBaselineFlag(FLAG_BANK_1 + MIZUUMI_00_H01);
    WriteBaselineFlag(FLAG_BANK_8 + LV4_34_01);
    for (size_t i = 0; i < (sizeof(kDigFlags) / sizeof(kDigFlags[0])); i++) {
        WriteBaselineFlag(kDigFlags[i]);
    }
}

static void ApplyOpenWorld(void) {
    size_t count = 0;
    const u16* flags;
    size_t i;

    if (!Rando_GetSettings().open_world) {
        return;
    }

    flags = Rando_NewFile_WorldOpenFlags(&count);
    for (i = 0; i < count; i++) {
        WriteBaselineFlag(flags[i]);
    }

    gSave.areaVisitFlags[0] |= RANDO_NEWFILE_VISIT_MASK;

    SetInventoryValue(ITEM_QST_GRAVEYARD_KEY, GetInventoryValue(ITEM_QST_GRAVEYARD_KEY) | 2u);

    fprintf(stderr, "[RANDO] open world: %u obstacle flag(s) cleared\n", (unsigned)count);
}

static struct {
    bool active;
    u64 seed;
    int damage_multiplier;
    bool mute_low_health_beep;
    bool mute_music;
    bool allow_homewarp;
    bool open_tingle;
} sRuntime = { false, 0, 1, false, false, false, false };

void Rando_Runtime_Refresh(void) {
    sRuntime.active = Rando_IsActive();
    sRuntime.seed = Rando_GetSeed64();
    sRuntime.damage_multiplier = 1;
    sRuntime.mute_low_health_beep = false;
    sRuntime.mute_music = false;
    sRuntime.allow_homewarp = false;
    sRuntime.open_tingle = false;

    if (!sRuntime.active) {
        return;
    }

    RandomizerSettings settings = Rando_GetSettings();
    sRuntime.allow_homewarp = settings.homewarp;
    sRuntime.open_tingle = settings.open_world;
}

static void EnsureFresh(void) {
    if (sRuntime.active != Rando_IsActive() || sRuntime.seed != Rando_GetSeed64()) {
        Rando_Runtime_Refresh();
    }
}

int Rando_Runtime_DamageMultiplier(void) {
    EnsureFresh();
    return sRuntime.damage_multiplier;
}

bool Rando_Runtime_MuteLowHealthBeep(void) {
    EnsureFresh();
    return sRuntime.mute_low_health_beep;
}

bool Rando_Runtime_MuteMusic(void) {
    EnsureFresh();
    return sRuntime.mute_music;
}

bool Rando_Runtime_AllowHomewarp(void) {
    EnsureFresh();
    return sRuntime.active && sRuntime.allow_homewarp;
}

bool Rando_Runtime_OpenTingleBrothers(void) {
    EnsureFresh();
    return sRuntime.active && sRuntime.open_tingle;
}

void Rando_Runtime_OnNewFile(void) {
    u64 seed;
    if (!Rando_IsActive()) {
        return;
    }
    seed = Rando_GetSeed64();
    fprintf(stderr, "[RANDO] applying new-file grants (seed %llu)\n", (unsigned long long)seed);
    ApplyBaselineNewFile(seed);
    ApplyLocationDisableFlags();
    ApplyStorySkip();
    ApplyWorldOpen();
    ApplyStartInventory(seed);
    ApplyOpenWorld();
    ApplyCrests(seed);
    ApplyInstantText();
    Rando_Runtime_Refresh();
}

unsigned Rando_GetChestLocalFlag(unsigned area, unsigned room, unsigned chestIndex) {
    TileEntity* te = (TileEntity*)GetRoomProperty(area, room, 3);
    int index = 0;
    if (te == NULL)
        return 0xFF;
    for (int i = 0; i < 256 && te[i].type != 0; ++i) {
        if (te[i].type != SMALL_CHEST && te[i].type != BIG_CHEST)
            continue;
        if (index == (int)chestIndex)
            return te[i].localFlag;
        index++;
    }
    return 0xFF;
}

unsigned Rando_GetDungeonKeyCount(unsigned dungeon_idx) {
    if (dungeon_idx >= 16)
        return 0;
    return gSave.dungeonKeys[dungeon_idx];
}

bool Rando_GetDungeonHasBigKey(unsigned dungeon_idx) {
    if (dungeon_idx >= 16)
        return false;
    /* Big key is bit 2 (0x4) of dungeonItems; bit1 (0x2) is the compass,
     * bit0 (0x1) the map — see gameUtils.c HasDungeonBigKey/Compass/Map and
     * itemMetaData.c. (The save.h field comment mislabels these.) */
    return (gSave.dungeonItems[dungeon_idx] & 4) != 0;
}

/* GiveItem (itemUtils.c cases 5/6) origin routing: a rando-shuffled dungeon
 * item carries 0x80|origin_dungeon in its subtype (RANDO_DUNGEON_ORIGIN_SUBTYPE)
 * and must credit THAT dungeon's save slot — the vanilla give path credits
 * gArea.dungeon_idx, which is wrong (or out of range) anywhere outside the
 * item's home dungeon. Returns true when the credit was applied here. */
bool Rando_RouteDungeonItem(u32 item, u32 subtype) {
    if (!Rando_IsActive() || !RANDO_SUBTYPE_HAS_ORIGIN(subtype))
        return false;
    unsigned origin = RANDO_SUBTYPE_ORIGIN(subtype);
    if (origin == 0 || origin >= 16)
        return false;
    switch (item) {
        case ITEM_SMALL_KEY:
            if (gSave.dungeonKeys[origin] < 99)
                gSave.dungeonKeys[origin]++;
            return true;
        case ITEM_DUNGEON_MAP:
            gSave.dungeonItems[origin] |= 0x1;
            return true;
        case ITEM_COMPASS:
            gSave.dungeonItems[origin] |= 0x2;
            return true;
        case ITEM_BIG_KEY:
            gSave.dungeonItems[origin] |= 0x4;
            return true;
        default:
            return false;
    }
}

bool Rando_IsInGameplay(void) {
    return gMain.task == TASK_GAME;
}

bool Rando_IsInFileSelect(void) {
    return gMain.task == TASK_FILE_SELECT;
}

void Rando_PlayCancelSfx(void) {
    SoundReq(SFX_MENU_CANCEL);
}

static u8 sHomewarpPending = 0;

void DoExitTransition(const Transition* data);

bool Rando_Homewarp_Request(void) {
    if (!Rando_Runtime_AllowHomewarp() || sHomewarpPending != 0) {
        return false;
    }
    if (gPlayerState.flags & PL_MINISH) {
        return false;
    }
    sHomewarpPending = 1;
    fprintf(stderr, "[RANDO] homewarp armed (sleeping)\n");
    return true;
}

bool Rando_Homewarp_HintVisible(void) {
    return Rando_Runtime_AllowHomewarp() && gPauseMenuOptions.screen == PauseMenuScreen_2 &&
           !(gPlayerState.flags & PL_MINISH);
}

void Rando_Homewarp_Tick(void) {
    Transition t;

    if (sHomewarpPending == 0) {
        return;
    }
    if (gMain.task != TASK_GAME || gSave.stats.health == 0) {
        sHomewarpPending = 0;
        return;
    }
    if (Port_SoftSlots_IsPauseActive()) {
        return;
    }
    sHomewarpPending = 0;

    t.warp_type = WARP_TYPE_AREA;
    t.startX = 0;
    t.startY = 0;
    t.endX = 0x90;
    t.endY = 0x38;
    t.shape = 0;
    t.area = 0x22;
    t.room = 0x15;
    t.layer = 1;
    t.transition_type = 0;
    t.facing_direction = 0;
    t.transitionSFX = 0;
    t.unk2 = 0;
    t.unk3 = 0;
    gRoomTransition.stairs_idx = 0;
    DoExitTransition(&t);
    fprintf(stderr, "[RANDO] homewarp: warped to Link's bed\n");
}
