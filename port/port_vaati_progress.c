#include "port_vaati_progress.h"

#include "flags.h"
#include "region.h"

static bool32 VaatiProgressFlagIsSet(const SaveFile* save, u32 flag) {
    return ReadBit((void*)save->flags, FLAG_BANK_10 + flag);
}

bool32 Port_VaatiProgressNeedsRepair(const SaveFile* save) {
    if (save == NULL || save->invalid || !save->initialized || save->saw_staffroll) {
        return FALSE;
    }

    /* This repair is proven only for the supported USA and European retail
     * profiles.  Their Dark Hyrule Castle ordinals are byte-identical. */
    if (!REGION_IS_USA && !REGION_IS_EU) {
        return FALSE;
    }

    return VaatiProgressFlagIsSet(save, LV6_GUFUU1_GISHIKI) &&
           !VaatiProgressFlagIsSet(save, LV6_GUFUU1_DEMO) &&
           VaatiProgressFlagIsSet(save, LV6_GUFUU2_DEAD);
}

bool32 Port_RepairVaatiProgress(SaveFile* save) {
    if (!Port_VaatiProgressNeedsRepair(save)) {
        return FALSE;
    }

    /* LV6_GUFUU2_DEAD semantically implies this prerequisite.  Restore only
     * the missing prerequisite and preserve every other story flag. */
    WriteBit(save->flags, FLAG_BANK_10 + LV6_GUFUU1_DEMO);
    return TRUE;
}
