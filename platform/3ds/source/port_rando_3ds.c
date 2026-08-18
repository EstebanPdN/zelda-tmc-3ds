#include "platform_3ds.h"
#include "port_runtime_config.h"
#include "port_save.h"
#include "rando/rando.h"
#include "rando/rando_file_menu.h"
#include "rando/rando_save.h"
#include "main.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern void Port_FileSelectRando_StartSlot(int slot);
extern void Port_FileSelectRando_CancelSlot(int slot);

static bool sGenerating;

static RandomizerSettings BuildSettings(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.glitchless_logic = Port_Config_GetRandoGlitchless();
    settings.obscure_locations = Port_Config_GetRandoObscure();
    settings.shuffle_kinstones = Port_Config_GetRandoKinstones();
    settings.shuffle_entrances = Port_Config_GetRandoEntrances();
    settings.shuffle_dojos = Port_Config_GetRandoDojos();
    settings.shuffle_dungeon_items = Port_Config_GetRandoDungeonItems();
    settings.open_world = Port_Config_GetRandoOpenWorld();
    settings.item_difficulty = (RandoItemPoolDifficulty)Port_Config_GetRandoItemPool();
    settings.homewarp = Port_Config_GetRandoHomewarp();
    settings.start_sword = Port_Config_GetRandoStartSword();
    settings.early_crests = Port_Config_GetRandoEarlyCrests();
    settings.instant_text = Port_Config_GetRandoInstantText();
    settings.tunic_color = Port_Config_GetRandoTunicColor();
    settings.heart_color = Port_Config_GetRandoHeartColor();
    settings.tricks = (uint32_t)Port_Config_GetRandoTricks();
    settings.accessibility = (RandoAccessibility)Port_Config_GetRandoAccessibility();
    return settings;
}

bool Port_RandoFileMenu_ShouldOpenForNewFile(void) {
    return Port_Config_GetRandoEnabled();
}

void Port_RandoFileMenu_Open(int saveSlot) {
    if (sGenerating) return;
    if (!Port_Config_GetRandoEnabled()) {
        Port_FileSelectRando_CancelSlot(saveSlot);
        return;
    }

    sGenerating = true;
    RandomizerSettings settings = BuildSettings();
    uint64_t seed = Platform3DS_SystemTick() ^ (Platform3DS_Milliseconds() << 17) ^
                    ((uint64_t)(unsigned)saveSlot << 57);
    if (seed == 0) seed = 0x544d43334453524eull;

    uint64_t generatedSeed = 0;
    const RandoStatus status = Rando_GenerateSeed(seed, &settings, &generatedSeed);
    if (status == RANDO_OK && Port_RandoSave_SaveActiveSlot(saveSlot)) {
        printf("Randomizer seed %llu ready for slot %d.\n", (unsigned long long)generatedSeed, saveSlot + 1);
        sGenerating = false;
        Port_FileSelectRando_StartSlot(saveSlot);
        return;
    }

    printf("Randomizer generation failed (%d). Returning to file select.\n", (int)status);
    Port_RandoSave_ClearSlot(saveSlot);
    Rando_Reset();
    sGenerating = false;
    Port_FileSelectRando_CancelSlot(saveSlot);
}

void Port_RandoFileMenu_Close(void) {}
bool Port_RandoFileMenu_IsOpen(void) { return sGenerating; }
bool Port_RandoFileMenu_IsModalOpen(void) { return sGenerating; }
bool Port_RandoFileMenu_GetRandoOptionEnabled(void) { return Port_Config_GetRandoEnabled(); }
void Port_RandoFileMenu_SetRandoOptionEnabled(bool enabled) { Port_Config_SetRandoEnabled(enabled); }
bool Port_RandoFileMenu_IsSidebarOpen(void) { return false; }
void Port_RandoFileMenu_SetSidebarOpen(bool open) { (void)open; }
void Port_RandoFileMenu_ToggleSidebar(void) {}
void Port_RandoFileMenu_RestorePersistedSettings(void) {}
void Port_RandoFileMenu_PersistLogicOverrides(void) {}

void Port_Rando3DS_ConfirmModeChange(bool enabled) {
    if (enabled == Port_Config_GetRandoEnabled()) return;

    if (!Port_Save_ClearActiveProfileData()) {
        printf("Randomizer mode unchanged: active profile cleanup failed.\n");
        return;
    }
    Rando_Reset();
    Port_Config_SetRandoEnabled(enabled);
    printf("Randomizer %s; active profile cleared. Restarting.\n", enabled ? "enabled" : "disabled");
    DoSoftReset();
}
