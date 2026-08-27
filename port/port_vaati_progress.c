#include "port_vaati_progress.h"

#include "area.h"
#include "flags.h"
#include "player.h"
#include "region.h"
#include "roomid.h"

#include <string.h>

static bool32 VaatiProgressFlagIsSet(const SaveFile* save, u32 flag) {
    return ReadBit((void*)save->flags, FLAG_BANK_10 + flag);
}

static bool32 VaatiProgressBaseIsRepairable(const SaveFile* save) {
    if (save == NULL || save->invalid || !save->initialized || save->saw_staffroll) {
        return FALSE;
    }

    /* This repair is proven only for the supported USA and European retail
     * profiles.  Their Dark Hyrule Castle ordinals are byte-identical. */
    if (!REGION_IS_USA && !REGION_IS_EU) {
        return FALSE;
    }

    if (ReadBit((void*)save->flags, FLAG_BANK_1 + ENDING) ||
        VaatiProgressFlagIsSet(save, LV6_ZELDA_DISCURSE) ||
        VaatiProgressFlagIsSet(save, LV6_00_ESCAPE) ||
        VaatiProgressFlagIsSet(save, LV6_SOTO_ENDING)) {
        return FALSE;
    }

    return VaatiProgressFlagIsSet(save, LV6_GUFUU1_GISHIKI) &&
           VaatiProgressFlagIsSet(save, LV6_GUFUU2_DEAD);
}

static bool32 VaatiProgressHasOriginalCorruption(const SaveFile* save) {
    return VaatiProgressBaseIsRepairable(save) && !VaatiProgressFlagIsSet(save, LV6_GUFUU1_DEMO);
}

static bool32 VaatiProgressMatchesLegacyBackup(const SaveFile* save, const SaveFile* backup) {
    const u32 bankStart = FLAG_BANK_10 >> 3;
    const u32 demoByte = (FLAG_BANK_10 + LV6_GUFUU1_DEMO) >> 3;
    const u8 demoMask = 1u << ((FLAG_BANK_10 + LV6_GUFUU1_DEMO) & 7);
    u32 i;

    if (save->global_progress != backup->global_progress ||
        memcmp(save->name, backup->name, sizeof(save->name)) != 0) {
        return FALSE;
    }
    for (i = bankStart; i < bankStart + 0x20; ++i) {
        const u8 mask = i == demoByte ? (u8)~demoMask : 0xFF;
        if ((save->flags[i] & mask) != (backup->flags[i] & mask)) return FALSE;
    }
    return TRUE;
}

bool32 Port_VaatiProgressBackupProvesLegacyRepair(const SaveFile* save, const SaveFile* backup) {
    if (!VaatiProgressBaseIsRepairable(save) || !VaatiProgressFlagIsSet(save, LV6_GUFUU1_DEMO)) {
        return FALSE;
    }
    return VaatiProgressHasOriginalCorruption(backup) && VaatiProgressMatchesLegacyBackup(save, backup);
}

bool32 Port_VaatiProgressNeedsRepair(const SaveFile* save, bool32 legacyRepairEvidence) {
    if (VaatiProgressHasOriginalCorruption(save)) return TRUE;
    return legacyRepairEvidence && VaatiProgressBaseIsRepairable(save) &&
           VaatiProgressFlagIsSet(save, LV6_GUFUU1_DEMO);
}

bool32 Port_RepairVaatiProgress(SaveFile* save, bool32 legacyRepairEvidence) {
    if (!Port_VaatiProgressNeedsRepair(save, legacyRepairEvidence)) {
        return FALSE;
    }

    /* Replay Vaati 1 from its native pre-intro checkpoint. Clearing both bits
     * also reverses E10's added intro bit when its exact backup proves that
     * legacy repair ran. */
    ClearBit(save->flags, FLAG_BANK_10 + LV6_GUFUU1_DEMO);
    ClearBit(save->flags, FLAG_BANK_10 + LV6_GUFUU2_DEAD);

    /* Saves made inside Vaati 2 need a safe room from which the first phase's
     * retail room loader can start. Other save locations remain untouched. */
    if (save->saved_status.area_next == AREA_VAATI_2) {
        save->saved_status.area_next = AREA_DARK_HYRULE_CASTLE;
        save->saved_status.room_next = ROOM_DARK_HYRULE_CASTLE_3F_TRIPLE_DARKNUT;
        save->saved_status.start_anim = 0;
        save->saved_status.spawn_type = PL_SPAWN_DEFAULT;
        save->saved_status.start_pos_x = 0xA8;
        save->saved_status.start_pos_y = 0x78;
        save->saved_status.layer = 1;
    }
    return TRUE;
}
