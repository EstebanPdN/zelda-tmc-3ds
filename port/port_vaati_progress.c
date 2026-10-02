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

static bool32 VaatiProgressGlobalFlagIsSet(const SaveFile* save, u32 flag) {
    return ReadBit((void*)save->flags, FLAG_BANK_0 + flag);
}

static bool32 VaatiProgressIsRepairable(const SaveFile* save) {
    if (save == NULL || save->invalid || !save->initialized || save->saw_staffroll) {
        return FALSE;
    }

    /* This repair is proven only for the supported USA and European retail
     * profiles.  Their Dark Hyrule Castle ordinals are byte-identical. */
    if (!REGION_IS_USA && !REGION_IS_EU) {
        return FALSE;
    }

    if (VaatiProgressGlobalFlagIsSet(save, ENDING) ||
        VaatiProgressGlobalFlagIsSet(save, GAMECLEAR) ||
        VaatiProgressFlagIsSet(save, LV6_ZELDA_DISCURSE) ||
        VaatiProgressFlagIsSet(save, LV6_00_ESCAPE) ||
        VaatiProgressFlagIsSet(save, LV6_SOTO_ENDING)) {
        return FALSE;
    }

    return save->global_progress == 9 && VaatiProgressFlagIsSet(save, LV6_GUFUU1_GISHIKI);
}

static bool32 VaatiProgressNeedsForwardRepair(const SaveFile* save) {
    return VaatiProgressIsRepairable(save) && !VaatiProgressFlagIsSet(save, LV6_GUFUU1_DEMO) &&
           VaatiProgressFlagIsSet(save, LV6_GUFUU2_DEAD);
}

static bool32 VaatiProgressNeedsReplayRepair(const SaveFile* save) {
    return VaatiProgressIsRepairable(save) && !VaatiProgressFlagIsSet(save, LV6_GUFUU1_DEMO) &&
           !VaatiProgressFlagIsSet(save, LV6_GUFUU2_DEAD) &&
           VaatiProgressFlagIsSet(save, LV6_GUFUU1_FIGURE) &&
           VaatiProgressFlagIsSet(save, LV6_GUFUU2_FIGURE);
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
    if (!VaatiProgressIsRepairable(save) || !VaatiProgressFlagIsSet(save, LV6_GUFUU1_DEMO) ||
        !VaatiProgressFlagIsSet(save, LV6_GUFUU2_DEAD)) {
        return FALSE;
    }
    return VaatiProgressNeedsForwardRepair(backup) && VaatiProgressMatchesLegacyBackup(save, backup);
}

bool32 Port_VaatiProgressNeedsRepair(const SaveFile* save, bool32 legacyRepairEvidence) {
    (void)legacyRepairEvidence;
    return VaatiProgressNeedsForwardRepair(save) || VaatiProgressNeedsReplayRepair(save);
}

bool32 Port_RepairVaatiProgress(SaveFile* save, bool32 legacyRepairEvidence) {
    if (!Port_VaatiProgressNeedsRepair(save, legacyRepairEvidence)) {
        return FALSE;
    }

    if (VaatiProgressNeedsForwardRepair(save)) {
        /* The second phase was defeated, but its prerequisite was lost. Commit
         * the prerequisite so the retail room loader advances to phase 3. */
        WriteBit(save->flags, FLAG_BANK_10 + LV6_GUFUU1_DEMO);
    } else {
        /* E5 could clear both phase bits after Vaati 2 had already been
         * entered. The loader-only phase-2 marker proves that history. Replay
         * Vaati 1 from a clean room load; its intro will commit DEMO normally. */
        ClearBit(save->flags, FLAG_BANK_10 + LV6_GUFUU1_FIGURE);
        ClearBit(save->flags, FLAG_BANK_10 + LV6_GUFUU2_FIGURE);
        ClearBit(save->flags, FLAG_BANK_10 + LV6_GUFUU1_DEMO);
        ClearBit(save->flags, FLAG_BANK_10 + LV6_GUFUU2_DEAD);
    }

    save->saved_status.area_next = AREA_DARK_HYRULE_CASTLE;
    save->saved_status.room_next = ROOM_DARK_HYRULE_CASTLE_3F_TRIPLE_DARKNUT;
    save->saved_status.start_anim = 0;
    save->saved_status.spawn_type = PL_SPAWN_DEFAULT;
    save->saved_status.start_pos_x = 0xA8;
    save->saved_status.start_pos_y = 0x78;
    save->saved_status.layer = 1;
    return TRUE;
}

bool32 Port_VaatiRebornNeedsResumeDefeat(u32 action, u32 phase) {
    return action != 0 && action != 7 && phase > 2;
}
