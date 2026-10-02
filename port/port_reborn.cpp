/*
 * port/port_reborn.cpp — implementation of the Reborn-parity feature
 * toggle store. Process-local; resets to defaults on each launch.
 *
 * Reborn-parity QoL features are ported from / modeled on The Minish Cap
 * Reborn by Admentus64 (GPL-3.0); distributed under the GPL-3.0 with
 * attribution. See THIRD-PARTY-LICENSES.md.
 */

#include "port_reborn.h"
#include "port_runtime_config.h"  /* persist feature toggles (issue #146) */

namespace {

struct {
    bool enabled;
    const char* label;
    const char* description;
} sFeatures[REBORN_FEAT_COUNT] = {
    /* REBORN_FEAT_SHELLS_9999 */
    {
        true,
        "Shells cap 9999",
        "Raise the Mysterious Shells cap from 999 to 9999.",
    },
    /* REBORN_FEAT_EQUIP_LR_BOOTS */
    {
        true,
        "L + R → Pegasus Boots",
        "Press R while holding L to instantly equip Pegasus Boots to slot A.",
    },
    /* REBORN_FEAT_EQUIP_LSELECT_OCARINA */
    {
        true,
        "L + SELECT → Ocarina",
        "Press SELECT while holding L to instantly equip the Ocarina to slot A.",
    },
    /* REBORN_FEAT_NO_EZLO_ON_RESUME */
    {
        true,
        "Skip Ezlo hint on resume",
        "Suppress the area Ezlo hint that fires the first time you re-enter "
        "the room after loading a save.",
    },
    /* REBORN_FEAT_LIBRARY_REOPEN */
    {
        true,
        "Library stays open after Four Sword",
        "Keep the Royal Hyrule Library accessible after obtaining the Four "
        "Sword. Vanilla closes it permanently.",
    },
    /* REBORN_FEAT_FIGURINE_MIN_RAISED */
    {
        true,
        "Raise minimum figurine chance",
        "Increase the floor on figurine-machine drop probability so "
        "duplicates take fewer attempts to clear.",
    },
    /* REBORN_FEAT_SKIP_EZLO_TUTORIALS */
    {
        false,
        "Skip Ezlo tutorial pop-ups",
        "Suppress the early-game Ezlo tutorial hints (movement, signs, "
        "talking, etc.). Doesn't affect plot-critical dialogue.",
    },
    /* REBORN_FEAT_HERO_MODE */
    {
        false,
        "Hero Mode (2× damage)",
        "Enemies and hazards deal double damage. Toggleable mid-run; takes "
        "effect from the next hit.",
    },
    /* REBORN_FEAT_RUPEE_LIKE_OVERHAUL — removed feature. Slot kept so the
     * persisted feature bitmask (issue #146) keeps its bit positions; the
     * entry is hidden from the Reborn tab and read by nothing. */
    {
        false,
        "Rupee Like overhaul (removed)",
        "",
    },
    /* REBORN_FEAT_SECONDARY_LAB */
    {
        true,
        "L + A/B → soft-slot 0/1",
        "Hold L while pressing A or B to fire the item assigned to "
        "the port's soft-slot 0 / soft-slot 1 (settable via SELECT-"
        "hold in the pause menu, or via the existing X/Y/L2/R2 "
        "soft-slot binding UI).",
    },
    /* REBORN_FEAT_SELECT_HOLD_EQUIP */
    {
        true,
        "SELECT-hold writes to soft slots",
        "Hold SELECT while pressing A in the pause menu to assign the "
        "cursor's item to soft-slot 0 (or B → soft-slot 1) instead of "
        "the primary A/B slot. Soft slots persist to the port config, "
        "not the save file.",
    },
    /* REBORN_FEAT_ANALOG_360_MOVEMENT */
    {
        true,
        "360° analog stick movement",
        "Move Link with the left analog stick at any of 32 angles "
        "(11.25° each) instead of the D-pad's 8. Walk speed is "
        "unchanged — only the direction Link faces and walks toward.",
    },
};

static bool sJustResumed = false;

} /* namespace */

extern "C" void Port_Reborn_NotifyJustResumed(void) {
    sJustResumed = true;
}

extern "C" bool Port_Reborn_ConsumeJustResumed(void) {
    bool v = sJustResumed;
    sJustResumed = false;
    return v;
}


extern "C" bool Port_Reborn_IsEnabled(RebornFeature f) {
    if ((unsigned)f >= REBORN_FEAT_COUNT) return false;
    return sFeatures[f].enabled;
}

/* Bitmask helpers for persistence (issue #146): bit i == feature i enabled. */
extern "C" unsigned Port_Reborn_GetMask(void) {
    unsigned m = 0;
    for (unsigned i = 0; i < REBORN_FEAT_COUNT; i++)
        if (sFeatures[i].enabled) m |= (1u << i);
    return m;
}

extern "C" void Port_Reborn_ApplyMask(unsigned mask) {
    for (unsigned i = 0; i < REBORN_FEAT_COUNT; i++)
        sFeatures[i].enabled = ((mask >> i) & 1u) != 0;
}

extern "C" void Port_Reborn_SetEnabled(RebornFeature f, bool on) {
    if ((unsigned)f >= REBORN_FEAT_COUNT) return;
    sFeatures[f].enabled = on;
    /* Persist the full feature set so the toggle survives restart. */
    Port_Config_SetRebornMask(Port_Reborn_GetMask());
}

extern "C" const char* Port_Reborn_FeatureLabel(RebornFeature f) {
    if ((unsigned)f >= REBORN_FEAT_COUNT) return "";
    return sFeatures[f].label;
}

extern "C" const char* Port_Reborn_FeatureDescription(RebornFeature f) {
    if ((unsigned)f >= REBORN_FEAT_COUNT) return "";
    return sFeatures[f].description;
}
