/**
 * @file game.c
 * @ingroup Tasks
 *
 * @brief Game task
 */
#include "game.h"

#include "area.h"
#include "asm.h"
#include "backgroundAnimations.h"
#include "beanstalkSubtask.h"
#include "common.h"
#include "droptables.h"
#include "enemy.h"
#include "entity.h"
#include "fade.h"
#include "fileselect.h"
#include "flags.h"
#include "functions.h"
#include "item.h"
#include "itemMetaData.h"
#include "kinstone.h"
#include "main.h"
#include "manager/diggingCaveEntranceManager.h"
#include "menu.h"
#include "message.h"
#include "npc.h"
#include "object.h"
#include "player.h"
#include "room.h"
#include "roomid.h"
#include "save.h"
#include "screen.h"
#include "sound.h"
#include "subtask.h"
#include "transitions.h"
#include "ui.h"

// Game task

typedef void(GameState)(void);
typedef void(GameMainState)(void);

static GameState GameTask_Transition;
static GameState GameTask_Init;
static GameState GameTask_Exit;
static GameState GameTask_Main;

static GameMainState GameMain_InitRoom;
static GameMainState GameMain_ChangeRoom;
static GameMainState GameMain_Update;
static GameMainState GameMain_ChangeArea;
GameMainState GameMain_MinishPortal;
static GameMainState GameMain_BarrelUpdate;
/*static GameMainState 00000000;*/
GameMainState GameMain_Subtask;

extern u8 gUpdateVisibleTiles;

extern void FinalizeSave(void);
extern void ClearArmosData(void);
extern void ClearRoomMemory(void);
extern void ClearMenuSavestate(void);
extern void ResetUI(void);
extern void sub_0806FD8C(void);
extern void sub_080300C4(void);
extern u32 sub_0805BC04(void);
extern void DeleteSleepingEntities(void);
extern bool32 UpdateLightLevel(void);
extern void sub_080185F8(void);
extern void UpdateDoorTransition(void);
extern void FlushSprites(void);
extern void DrawEntities(void);
extern void CopyOAM(void);
extern bool32 CheckInitPortal(void);
extern void UpdateCarriedObject(void);
extern void CollisionMain(void);
extern void sub_0805BB74(s32);
extern void CreateZeldaFollower(void);
extern void LoadRoomGfx(void);
extern void RecycleEntities(void);
extern void sub_0804AF90(void);
extern void CallRoomProp6(void);

static void UpdateWindcrests(void);
static void InitializeEntities(void);
static void sub_08051D98(void);
static void sub_08051DCC(void);

void sub_08054974(u32 worldEventId, bool32 param_2);

void GameTask(void) {
    static GameState* const sStates[] = {
        GameTask_Transition,
        GameTask_Init,
        GameTask_Main,
        GameTask_Exit,
    };

    gRoomTransition.frameCount++;
    sStates[gMain.state]();
}

static void GameTask_Transition(void) {
    // wait for file select to fade out
    if (gFadeControl.active)
        return;

    DispReset(1);
    InitSoundPlayingInfo();
    zMallocInit();
    ResetUI();
    ClearMenuSavestate();
    MemClear(&gRoomTransition, sizeof(gRoomTransition));
    ClearRoomMemory();
    ClearArmosData();

    FinalizeSave();
    // spawn in with saved status
    MemCopy(&gSave.saved_status, &gRoomTransition.player_status, sizeof(gRoomTransition.player_status));
    gRoomTransition.type = TRANSITION_FADE_BLACK_SLOW;
    ResetTmpFlags();

    gMain.state = GAMETASK_INIT;
    gMain.substate = GAMEMAIN_INITROOM;
}

static void GameTask_Init(void) {
    DispReset(1);
    gFadeControl.mask = 0xffffffff;
    MemClear(&gOAMControls, sizeof(gOAMControls));
    MemClear(&gUI, sizeof(gUI));
    EraseAllEntities();
    SetBGDefaults();
    ClearTileMaps();
    ResetPalettes();
    ResetPaletteTable(1);
    sub_0806FD8C();
    gRoomControls.area = gRoomTransition.player_status.area_next;
    gRoomControls.room = gRoomTransition.player_status.room_next;
    LoadGfxGroups();
    gGFXSlots.unk0 = 1;
    gMain.state = GAMETASK_MAIN;
}

static void GameTask_Main(void) {
    static GameMainState* const sStates[] = {
        GameMain_InitRoom,
        GameMain_ChangeRoom,
        GameMain_Update,
        GameMain_ChangeArea,
        GameMain_MinishPortal,
        GameMain_BarrelUpdate,
        0,
        GameMain_Subtask,
    };
    sStates[gMain.substate]();
}

static void GameMain_InitRoom(void) {
    SetInitializationPriority();
    gScreen.lcd.displayControl = DISPCNT_BG0_ON | DISPCNT_BG1_ON | DISPCNT_BG2_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
    gMain.substate = GAMEMAIN_CHANGEROOM;
    gRoomTransition.transitioningOut = 0;
    gRoomTransition.field_0x4[0] = 0;
    gRoomTransition.field_0x4[1] = 0;
    MessageInitialize();
    InitRoom();
    InitUI(FALSE);
    InitializeEntities();
    if (!REGION_IS_EU) {
        sub_0801855C();
    }
}

static void GameMain_ChangeRoom(void) {
    UpdateEntities();
    if (!UpdateLightLevel())
        UpdateScroll();
    UpdateBgAnimations();
    UpdateScrollVram();
    DrawUI();
    UpdateManagers();
    FlushSprites();
#ifdef PC_PORT
    DrawUIElementsGameplay();
#else
    DrawUIElements();
#endif
    UpdateCarriedObject();
    DrawEntities();
    CopyOAM();

    if (gFadeControl.active || gRoomControls.reload_flags != 0)
        return;

    UpdateFakeScroll();
    if (gArea.bgm != gArea.queued_bgm) {
        gArea.bgm = gArea.queued_bgm;
        SoundReq(gArea.queued_bgm | SONG_PLAY_VOL_RESET);
    }

    DeleteSleepingEntities();

    if (sub_0805BC04()) {
        return;
    }

    UpdatePlayerMapCoords();
    ClearEventPriority();
    UpdateWindcrests();
    sub_080300C4();
    gMain.substate = GAMEMAIN_UPDATE;
    SetPlayerControl(0);
    gPauseMenuOptions.disabled = 0;
#if defined(PC_PORT)
    if (REGION_IS_USA) {
        bool32 fire_hint = (gArea.unk28.textBaseIndex != 0xff);
        extern bool Port_Reborn_IsEnabled(int feat);
        extern bool Port_Reborn_ConsumeJustResumed(void);
        if (fire_hint && Port_Reborn_IsEnabled(3 /*REBORN_FEAT_NO_EZLO_ON_RESUME*/)
            && Port_Reborn_ConsumeJustResumed()) {
            gArea.unk28.textBaseIndex = 0xff;
            fire_hint = false;
        }
        if (fire_hint) {
            sub_0801855C();
        }
        CreateMiscManager();
        CheckAreaDiscovery();
    } else if (REGION_IS_EU) {
        CheckAreaDiscovery();
        sub_0801855C();
    } else if (REGION_IS_JP) {
        CheckAreaDiscovery();
        if (gArea.unk28.textBaseIndex != 0xff) {
            sub_0801855C();
        }
    }
#else
#if defined(USA) || defined(DEMO_USA)
    {
        bool32 fire_hint = (gArea.unk28.textBaseIndex != 0xff);
        if (fire_hint) {
            sub_0801855C();
        }
    }
    CreateMiscManager();
    CheckAreaDiscovery();
#elif defined(EU)
    CheckAreaDiscovery();
    sub_0801855C();
#elif defined(JP)
    CheckAreaDiscovery();
    if (gArea.unk28.textBaseIndex != 0xff) {
        sub_0801855C();
    }
#elif defined(DEMO_JP)
    if (gRoomTransition.field31)
        CheckAreaDiscovery();
    if (gArea.unk28.textBaseIndex != 0xff) {
        sub_0801855C();
    }
    CreateMiscManager();
#endif
#endif

    if (!gRoomVars.didEnterScrolling) {
        RequestPriorityDuration(NULL, 1);
    }
}

#ifdef PC_PORT
extern bool Port_Reborn_IsEnabled(int feat);
#define _REBORN_LR_BOOTS         1
#define _REBORN_LSELECT_OCARINA  2

/* Reborn-parity (ported from Minish Cap Reborn, GPL-3.0):
 * in-game equip shortcuts. Press R while holding L → equip Pegasus
 * Boots in slot A. Press SELECT while holding L → equip Ocarina in
 * slot A. Both gated on the item being in inventory + a state where
 * the pause menu itself would normally be openable, so we don't
 * trigger mid-cutscene or while dying. Each shortcut runtime-togglable
 * via F8 → Reborn tab. */
static void Port_ApplyEquipShortcut(u32 trigger_key, u32 itemId) {
    if (!(gInput.heldKeys & L_BUTTON)) return;
    if (!(gInput.newKeys & trigger_key)) return;
    if (gFadeControl.active) return;
    if (gMessage.state & MESSAGE_ACTIVE) return;
    if (gSave.stats.health == 0) return;
    if (gPlayerState.controlMode != 0) return;
    if (GetInventoryValue(itemId) == 0) return;
    ForceEquipItem(itemId, EQUIP_SLOT_A);
}
#endif

static void GameMain_Update(void) {
#ifdef PC_PORT
    /* Reborn-style item-equip shortcuts. Process BEFORE the pause-
     * menu check so L+START still opens the pause menu normally. */
    if (Port_Reborn_IsEnabled(_REBORN_LR_BOOTS)) {
        Port_ApplyEquipShortcut(R_BUTTON, ITEM_PEGASUS_BOOTS);
    }
    if (Port_Reborn_IsEnabled(_REBORN_LSELECT_OCARINA)) {
        Port_ApplyEquipShortcut(SELECT_BUTTON, ITEM_OCARINA);
    }
#endif
    if (CheckInitPauseMenu() || CheckInitPortal()) {
        return;
    }
    UpdateTimerCallbacks();

    // leave early if player is now entering a portal
    if (gMain.substate != GAMEMAIN_UPDATE) {
        return;
    }

    if ((gMessage.state & MESSAGE_ACTIVE) || gPriorityHandler.priority_timer != 0)
        PausePlayer();

    FlushSprites();
    UpdateEntities();
    UpdateDoorTransition();
    CollisionMain();
    UpdateScroll();
    UpdateBgAnimations();
    UpdateScrollVram();
    DecreasePortalTimer();
    DrawUI();
    UpdateManagers();
#ifdef PC_PORT
    DrawUIElementsGameplay();
#else
    DrawUIElements();
#endif
    UpdateCarriedObject();
    DrawEntities();
    CheckRoomExit();
    UpdatePlayerMapCoords();
    CheckGameOver();
    sub_080185F8();
    CopyOAM();
    switch (gRoomControls.reload_flags) {
        case RELOAD_ALL:

            gPlayerState.queued_action = PLAYER_ROOMTRANSITION;
            gMain.substate = GAMEMAIN_CHANGEROOM;
            SetRoomReloadPriority();
            sub_08051D98();
            break;
        case RELOAD_ENTITIES:

            gPlayerState.queued_action = PLAYER_ROOMTRANSITION;
            gMain.substate = GAMEMAIN_CHANGEROOM;
            SetRoomReloadPriority();
            sub_08051DCC();
            break;
    }
}

static void GameMain_BarrelUpdate(void) {
    if (CheckInitPauseMenu())
        return;

    UpdateEntities();
    CollisionMain();
    DrawUI();
    UpdateManagers();
    FlushSprites();
#ifdef PC_PORT
    DrawUIElementsGameplay();
#else
    DrawUIElements();
#endif
    UpdateCarriedObject();
    DrawEntities();
    CheckRoomExit();
    CheckGameOver();
    CopyOAM();
    if (!gFadeControl.active)
        ClearEventPriority();
}

static void GameMain_ChangeArea(void) {
    FlushSprites();
#ifdef PC_PORT
    DrawUIElementsGameplay();
#else
    DrawUIElements();
#endif
    DrawEntities();
    gMain.pad = 1;
    CopyOAM();
    if (!gFadeControl.active) {
        DispReset(1);
        gMain.state = GAMETASK_INIT;
        gMain.substate = GAMEMAIN_INITROOM;
        gRoomTransition.transitioningOut = 1;
    }
}

static void GameTask_Exit(void) {
    SetFade(FADE_IN_OUT | FADE_BLACK_WHITE | FADE_INSTANT, 8);
    SetTask(TASK_GAMEOVER);
}

// TODO End of GameTask?

static void InitializeEntities(void) {
    sub_08052EA0();
    sub_0804AF90();
    CallRoomProp6();
    InitializePlayer();
    gDiggingCaveEntranceTransition.entrance = NULL;
    InitializeCamera();
    gUpdateVisibleTiles = 1;
    LoadRoomBgm();
    SetColor(0, 0);
    LoadRoom();
    CreateZeldaFollower();
    CallRoomProp5And7();
    sub_0805329C();
    UpdateScrollVram();
    sub_0805BB74(-1);
    UpdatePlayerRoomStatus();
}

static void sub_08051D98(void) {
#ifdef PC_PORT
    /* Issue #139 (dark-room dim overlay): intra-area RELOAD_ALL doesn't call
     * InitRoom, so gArea.lightType persists from the previous room. The
     * LIGHT_MANAGER (ENT_PERSIST) carries over via RecycleEntities and keeps
     * the dim BG3/WIN0 overlay active in the bright destination room. Clear
     * lightType + the DISPCNT enable bits so the overlay drops; a genuinely
     * dark next room re-energises it via its tile entity (room.c:613 ->
     * sub_0805BB00). The separate symptom — the ToGrimblade flame braziers
     * vanishing behind the bowl on this transition — was a PPU BG-priority
     * sort bug, fixed in libs/ViruaPPU/src/mode1.c (the dark room leaves
     * BG3CNT at priority 0, which an unstable sort let reorder BG1 past BG2). */
    if (gArea.lightType != 0) {
        gArea.lightType = 0;
        gScreen.lcd.displayControl &= ~(DISPCNT_BG3_ON | DISPCNT_WIN0_ON);
    }
#endif
    sub_08052EA0();
    gRoomVars.didEnterScrolling = TRUE;

    // remove old entities, unless persistent
    RecycleEntities();

    sub_0804AF90();
    CallRoomProp6();
    LoadRoomGfx();
    LoadRoomBgm();
    LoadRoom();
    CallRoomProp5And7();
    SetPlayerControl(1);
}

static void sub_08051DCC(void) {
    gRoomControls.area = gRoomTransition.player_status.area_next;
    gRoomControls.room = gRoomTransition.player_status.room_next;
    RoomExitCallback();
    gRoomTransition.type = TRANSITION_3;
    InitRoom();
    sub_08052EA0();
    RecycleEntities();
    sub_0804AF90();
    CallRoomProp6();
    LoadRoomBgm();
}

static void UpdateWindcrests(void) {
    if (AreaIsOverworld()) {
        const OverworldLocation* location;
        u32 hi_x, hi_y;
        s32 x, y;

        x = gPlayerEntity.base.x.HALF.HI;
        if (x < 0)
            x += 0xf;
        hi_x = x >> 4;

        y = gPlayerEntity.base.y.HALF.HI;
        if (y < 0)
            y += 0xf;
        hi_y = y >> 4;

        for (location = gOverworldLocations; location->minX != 0xFF; location++) {
            if (location->minX <= hi_x && location->maxX >= hi_x && location->minY <= hi_y && location->maxY >= hi_y) {
                gSave.windcrests |= 1 << location->windcrestId;
                break;
            }
        }
    }
}
