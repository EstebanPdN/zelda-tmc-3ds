#ifndef MAIN_H
#define MAIN_H

#include "global.h"
#include "color.h"
#include "room.h"
#include "screen.h"
#include "script.h"
#include "structures.h"
#include "region.h"

/** File signature */
#define SIGNATURE 'MCZ3'
/** Maximum message speed. */
#define MAX_MSG_SPEED 3
/** Number of save slots */
#define NUM_SAVE_SLOTS 3
/** Maximum brightness. */
#define MAX_BRIGHTNESS 3

/** Supported game languages. */
typedef enum {
    LANGUAGE_JP,
    LANGUAGE_EN,
    LANGUAGE_FR,
    LANGUAGE_DE,
    LANGUAGE_ES,
    LANGUAGE_IT,
    NUM_LANGUAGES,
} Language;

/*
 * EU is the odd retail ROM: its language-gated graphics/text resources are
 * stored in save-header slots 2..6. Slot 2 is English, then FR/DE/ES/IT follow.
 * USA/JP use the public Language enum directly. Keep this conversion explicit
 * so a user-facing "English" preference (LANGUAGE_EN == 1) does not make the
 * EU file-select/title screens load slot 1 graphics.
 */
#define EU_LANGUAGE_EN_SLOT LANGUAGE_FR
#define EU_LANGUAGE_LAST_SLOT NUM_LANGUAGES
#define LANGUAGE_SLOT_COUNT (NUM_LANGUAGES + 1)

#ifdef MULTI_REGION
/* Fat binary is compiled USA/EU-baseline; GAME_LANGUAGE is the compile-time
 * default (English) used in the static sDefaultSettings initializer and as a
 * last-resort text fallback. Runtime region adaptation (JP->LANGUAGE_JP) is
 * layered on top in InitSaveHeader (src/main.c) and the text.c fallbacks. */
#define GAME_LANGUAGE LANGUAGE_EN
#else
#ifdef ENGLISH
#define GAME_LANGUAGE LANGUAGE_EN
#else
#define GAME_LANGUAGE LANGUAGE_JP
#endif
#endif

static inline u32 RegionLanguageSlotCount(void) {
    return REGION_IS_EU ? LANGUAGE_SLOT_COUNT : NUM_LANGUAGES;
}

static inline u8 RegionDefaultLanguage(void) {
    if (REGION_IS_JP) {
        return LANGUAGE_JP;
    }
    if (REGION_IS_EU) {
        return EU_LANGUAGE_EN_SLOT;
    }
    return GAME_LANGUAGE;
}

static inline s32 RegionPreferredLanguageToSaveSlot(s32 language) {
    if (language < 0) {
        return -1;
    }
    if (REGION_IS_JP) {
        return LANGUAGE_JP;
    }
    if (REGION_IS_EU) {
        if (language <= LANGUAGE_EN) {
            return EU_LANGUAGE_EN_SLOT;
        }
        if (language < NUM_LANGUAGES) {
            return language + 1;
        }
        return -1;
    }
    return language < NUM_LANGUAGES ? language : -1;
}

static inline bool32 RegionSaveLanguageValid(u32 language) {
    if (REGION_IS_JP) {
        return language == LANGUAGE_JP;
    }
    if (REGION_IS_EU) {
        return language >= EU_LANGUAGE_EN_SLOT && language <= EU_LANGUAGE_LAST_SLOT;
    }
    return language == GAME_LANGUAGE;
}

/** Program tasks. */
typedef enum {
    TASK_TITLE,       /**< Title task. This is the first task to be entered. */
    TASK_FILE_SELECT, /**< File selection task. */
    TASK_GAME,        /**< Gameplay task. Overworld, menus, cutscenes are all contained here. */
    TASK_GAMEOVER,    /**< Gameover task. */
    TASK_STAFFROLL,   /**< Staffroll task. Only accessible through the script played during the game ending. */
    TASK_DEBUG,       /**< Debug task. Inaccessible in normal gameplay. */
} Task;

/** System sleep status. */
typedef enum {
    DEFAULT,
    SLEEP,
} SleepStatus;

/**
 * Main system structure.
 */
typedef struct {
    vu8 interruptFlag;
    u8 sleepStatus;
    u8 task;     /**< Current #Task. */
    u8 state;    /**< State of the current #Task. */
    u8 substate; /**< Substate of the current #Task. */
    u8 field_0x5;
    u8 muteAudio; /**< Mute audio. */
    u8 field_0x7;
    u8 pauseFrames;   /**< Number of frames to pause. */
    u8 pauseCount;    /**< Number of pauses to make. */
    u8 pauseInterval; /**< Number of frames to play between each pause. */
    u8 pad;           // TODO actually used in CopyOAM()
    u16 ticks;        /**< Current time. */
} Main;

/**
 * HUD structure.
 */
// TODO Rather a structure more generally about gfx?
typedef struct {
    /*0x000*/ u8 nextToLoad;
    /*0x001*/ u8 _1;
    /*0x002*/ u8 lastState;
    /*0x003*/ u8 field_0x3;
    /*0x004*/ u8 state;
    /*0x005*/ u8 field_0x5;
    /*0x006*/ bool8 loadGfxOnRestore; // used in Subtask_FadeOut to determine the loadGfx parameter of RestoreGameTask.
    /*0x007*/ u8 pauseFadeIn;
    /*0x008*/ u16 fadeType;
    /*0x00A*/ u16 fadeInTime;
    /*0x00C*/ u8 controlMode;
    /*0x00D*/ u8 unk_d;
    /*0x00E*/ u8 unk_e;
    /*0x00F*/ u8 unk_f;
    /*0x010*/ void** currentRoomProperties;
    /*0x014*/ BgSettings* mapBottomBgSettings;
    /*0x018*/ BgSettings* mapTopBgSettings;
    /*0x01C*/ RoomControls roomControls;
    /*0x054*/ GfxSlotList gfxSlotList;
    /*0x268*/ Palette palettes[0x10];
    /*0x2A8*/ u8 unk_2a8[0x100];
    /*0x3A8*/ ActiveScriptInfo activeScriptInfo;
} UI;
PORT_STATIC_ASSERT_SIZE(UI, 0x3b4, 0x480, "UI size incorrect");

extern Main gMain; /**< Main instance. */
extern UI gUI;     /**< UI instance. */

/**
 * Program entry point.
 */
void AgbMain(void);

/**
 * Begin a new task.
 *
 * @param task #Task to begin.
 */
void SetTask(u32 task);

/**
 * Initialize the DMA system.
 */
void InitDMA(void);

/**
 * Soft reset the system.
 */
void DoSoftReset(void);

/**
 * Put the system into sleep mode.
 */
void SetSleepMode(void);

/**
 * Sets a DMA to be performed at next VBlank.
 */
extern void SetVBlankDMA(u16* src, u16* dest, u32 size);
extern void InitVBlankDMA(void);
extern void ResetPalettes(void);
extern void VBlankIntrWait();
extern void VBlankInterruptWait(void);
extern void DisableInterruptsAndDMA(void);
extern void EnableVBlankIntr(void);
extern void DisableVBlankDMA(void);

/** @name Task entrypoints */
///@{
/** Task entrypoint. */
extern void TitleTask(void);
extern void FileSelectTask(void);
extern void GameTask(void);
extern void GameOverTask(void);
extern void StaffrollTask(void);
extern void DebugTask(void);

#if defined(DEMO_USA) || defined(PC_PORT)
extern void DemoTask(void);
#endif
/// @}

extern u8 gUnk_03003DE4[0xC];
extern u16 gPaletteBuffer[];

extern u32 CheckRegionOnScreen(u32 x0, u32 y0, u32 x1, u32 y1);
extern u32 CheckRegionsOnScreen(const u16* arr);

#endif // MAIN_H
