#include "port_audio_mute.h"
#include "platform_3ds.h"
#include "port_reborn.h"
#include "port_runtime_config.h"
#include "port_tts.h"
#include "main.h"
#include "save.h"

#include <stdbool.h>
#include <stdint.h>

bool Port_Reborn_IsEnabled(RebornFeature feature) { (void)feature; return false; }
void Port_Reborn_SetEnabled(RebornFeature feature, bool enabled) { (void)feature; (void)enabled; }
unsigned Port_Reborn_GetMask(void) { return 0; }
void Port_Reborn_ApplyMask(unsigned mask) { (void)mask; }
void Port_Reborn_NotifyJustResumed(void) {}
bool Port_Reborn_ConsumeJustResumed(void) { return false; }

void Port_TTS_Speak(const char* text, const PortTtsOptions* options) { (void)text; (void)options; }
void Port_TTS_Stop(void) {}
bool Port_TTS_GetEnabled(void) { return false; }

bool Port_AudioMute_ShouldSuppress(unsigned int soundRequest) { (void)soundRequest; return false; }
int Port_Debug_NoclipEnabled(void) { return 0; }

static uint64_t sNextAutosaveMs;

extern u32 WriteSaveFile(u32 index, SaveFile* saveFile);
extern bool Port_Config_AutosaveEnabled(void);
extern u32 Port_Config_AutosaveIntervalMs(void);

int Port_QuickSave_AutoEnabled(void) { return Port_Config_AutosaveEnabled() ? 1 : 0; }
void Port_QuickSave_SetAutoEnabled(int enabled) {
    sNextAutosaveMs = enabled ? Platform3DS_Milliseconds() + Port_Config_AutosaveIntervalMs() : 0;
}

/* The desktop port's autosave is a full machine-state snapshot, which is
 * intentionally not used on 3DS because its native pointers are not safely
 * relocatable. The device implementation writes the game's ordinary active
 * save slot instead, through the same checksum and atomic EEPROM path as the
 * pause menu. It runs on the game thread and only at a normal gameplay frame
 * boundary. */
void Port_QuickSave_AutoTick(void) {
    if (!Port_Config_AutosaveEnabled()) {
        sNextAutosaveMs = 0;
        return;
    }
    if (gMain.task != TASK_GAME || gMain.pauseFrames != 0 || gSaveHeader->signature != SIGNATURE ||
        gSaveHeader->saveFileId >= NUM_SAVE_SLOTS) {
        return;
    }

    uint64_t now = Platform3DS_Milliseconds();
    if (sNextAutosaveMs == 0) {
        sNextAutosaveMs = now + Port_Config_AutosaveIntervalMs();
        return;
    }
    if ((int64_t)(now - sNextAutosaveMs) < 0) return;

    UpdateGlobalProgress();
    if (WriteSaveFile(gSaveHeader->saveFileId, &gSave) != 0) {
        Platform3DS_Debug("[tmc3ds] autosave completed\n");
    } else {
        Platform3DS_Debug("[tmc3ds] autosave failed\n");
    }
    sNextAutosaveMs = now + Port_Config_AutosaveIntervalMs();
}
