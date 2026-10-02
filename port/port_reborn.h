/*
 * port/port_reborn.h — runtime toggles for Minish Cap Reborn parity
 * features.
 *
 * These quality-of-life features are ported from / modeled on
 * The Minish Cap Reborn by Admentus64 (GPL-3.0):
 *   https://github.com/Admentus64/The-Minish-Cap-Reborn
 * Distributed under the GPL-3.0 with attribution; see THIRD-PARTY-LICENSES.md.
 * Each feature checks its toggle at the call site, so users can flip
 * individual features on or off at runtime.
 *
 * Defaults: all ON (these are quality-of-life wins).
 * Surface in F8 ribbon → "Reborn" tab.
 */

#ifndef PORT_REBORN_H
#define PORT_REBORN_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REBORN_FEAT_SHELLS_9999,           /* shells cap raised 999 → 9999 */
    REBORN_FEAT_EQUIP_LR_BOOTS,        /* L + R → Pegasus Boots */
    REBORN_FEAT_EQUIP_LSELECT_OCARINA, /* L + SELECT → Ocarina */
    REBORN_FEAT_NO_EZLO_ON_RESUME,     /* skip Ezlo hint after file resume */
    REBORN_FEAT_LIBRARY_REOPEN,        /* Library accessible after Four Sword */
    REBORN_FEAT_FIGURINE_MIN_RAISED,   /* raise minimum figurine drop chance */
    REBORN_FEAT_SKIP_EZLO_TUTORIALS,   /* skip most early-game Ezlo tutorials */
    REBORN_FEAT_HERO_MODE,             /* damage doubled — toggleable Hero Mode */
    REBORN_FEAT_RUPEE_LIKE_OVERHAUL,   /* removed feature; slot kept for config-bitmask stability */
    REBORN_FEAT_SECONDARY_LAB,         /* L+A / L+B → use SLOT_LA / SLOT_LB items */
    REBORN_FEAT_SELECT_HOLD_EQUIP,     /* SELECT-hold in pause menu → equip secondary slot */
    REBORN_FEAT_ANALOG_360_MOVEMENT,   /* 360° left-stick movement (32-step snap) */
    REBORN_FEAT_COUNT,
} RebornFeature;

bool Port_Reborn_IsEnabled(RebornFeature f);
void Port_Reborn_SetEnabled(RebornFeature f, bool on);
/* Bitmask accessors for config persistence (issue #146): bit i == feature i. */
unsigned Port_Reborn_GetMask(void);
void Port_Reborn_ApplyMask(unsigned mask);

/* Human-readable label for a feature. Used by the F8 ribbon UI. */
const char* Port_Reborn_FeatureLabel(RebornFeature f);

/* Short description / what it does. Tooltip-style. */
const char* Port_Reborn_FeatureDescription(RebornFeature f);

/* Marks that a save-resume just happened. Set by Port_QuickLoad and
 * by the save-load entry path in src/save.c (PC_PORT only). The next
 * area-init that would have fired an Ezlo hint consumes this flag
 * and suppresses the hint when REBORN_FEAT_NO_EZLO_ON_RESUME is on. */
void Port_Reborn_NotifyJustResumed(void);
bool Port_Reborn_ConsumeJustResumed(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_REBORN_H */
