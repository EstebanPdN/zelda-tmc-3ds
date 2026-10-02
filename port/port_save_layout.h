#ifndef PORT_SAVE_LAYOUT_H
#define PORT_SAVE_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "flags.h"
#include "item_ids.h"
#include "save.h"

/* v1.0 accidentally omitted the unused byte at EEPROM offset 0x25B. Saves
 * created from scratch by that build consequently stored flags and dungeon
 * arrays one byte early; compiler alignment put the u32 timers back at 0x48C.
 * Retail/emulator saves use the canonical layout now described by SaveFile. */

static inline unsigned Port_SaveLayoutFlag(const u8* bytes, size_t flagsOffset, unsigned flag) {
    return (bytes[flagsOffset + (flag >> 3)] >> (flag & 7)) & 1u;
}

static inline unsigned Port_SaveLayoutInventory(const u8* bytes, unsigned item) {
    return (bytes[offsetof(SaveFile, inventory) + (item >> 2)] >> ((item & 3) << 1)) & 3u;
}

static inline int Port_SaveLayoutStoryRank(const u8* bytes, size_t flagsOffset) {
    const unsigned start = Port_SaveLayoutFlag(bytes, flagsOffset, START);
    const unsigned ezlo = Port_SaveLayoutFlag(bytes, flagsOffset, EZERO_1ST);
    const unsigned departed = Port_SaveLayoutFlag(bytes, flagsOffset, TABIDACHI);

    if (departed && (!ezlo || !start)) {
        return -3;
    }
    if (ezlo && !start) {
        return -2;
    }
    return (int)start + (int)ezlo + (int)departed;
}

static inline int Port_SaveLayoutScore(const u8* bytes, size_t flagsOffset) {
    const unsigned progress = bytes[offsetof(SaveFile, global_progress)];
    const unsigned lv1 = Port_SaveLayoutFlag(bytes, flagsOffset, LV1_CLEAR);
    const unsigned lv3 = Port_SaveLayoutFlag(bytes, flagsOffset, LV3_CLEAR);
    const unsigned lv4 = Port_SaveLayoutFlag(bytes, flagsOffset, LV4_CLEAR);
    const unsigned lv5 = Port_SaveLayoutFlag(bytes, flagsOffset, LV5_CLEAR);
    const int storyRank = Port_SaveLayoutStoryRank(bytes, flagsOffset);
    const bool advancedInventory =
        Port_SaveLayoutInventory(bytes, ITEM_PEGASUS_BOOTS) != 0 ||
        Port_SaveLayoutInventory(bytes, ITEM_SKILL_DASH_ATTACK) != 0 ||
        Port_SaveLayoutInventory(bytes, ITEM_FOURSWORD) != 0;
    int score = storyRank * 3;

    if (progress >= 2) score += lv1 ? 4 : -4;
    if (progress >= 5) score += lv3 ? 3 : -3;
    if (progress >= 6) score += lv4 ? 3 : -3;
    if (progress >= 8) score += lv5 ? 3 : -3;
    if (lv3 && !lv1) score -= 2;
    if (lv4 && !lv3) score -= 2;
    if (lv5 && !lv4) score -= 2;
    if (advancedInventory) score += storyRank >= 2 ? 3 : -3;
    return score;
}

static inline bool Port_SaveLayoutIsCoherent(const u8* bytes, size_t flagsOffset) {
    const unsigned progress = bytes[offsetof(SaveFile, global_progress)];
    const unsigned lv1 = Port_SaveLayoutFlag(bytes, flagsOffset, LV1_CLEAR);
    const unsigned lv3 = Port_SaveLayoutFlag(bytes, flagsOffset, LV3_CLEAR);
    const unsigned lv4 = Port_SaveLayoutFlag(bytes, flagsOffset, LV4_CLEAR);
    const unsigned lv5 = Port_SaveLayoutFlag(bytes, flagsOffset, LV5_CLEAR);
    const int storyRank = Port_SaveLayoutStoryRank(bytes, flagsOffset);
    const bool advancedInventory =
        Port_SaveLayoutInventory(bytes, ITEM_PEGASUS_BOOTS) != 0 ||
        Port_SaveLayoutInventory(bytes, ITEM_SKILL_DASH_ATTACK) != 0 ||
        Port_SaveLayoutInventory(bytes, ITEM_FOURSWORD) != 0;

    if (storyRank < 0) return false;
    if (progress >= 2 && !lv1) return false;
    if (progress >= 5 && !lv3) return false;
    if (progress >= 6 && !lv4) return false;
    if (progress >= 8 && !lv5) return false;
    if ((lv3 && !lv1) || (lv4 && !lv3) || (lv5 && !lv4)) return false;
    if (advancedInventory && storyRank < 2) return false;
    return true;
}

static inline bool Port_SaveLayoutLooksLegacy(const SaveFile* save) {
    const u8* bytes = (const u8*)save;
    const size_t canonicalFlags = offsetof(SaveFile, flags);
    const size_t legacyFlags = offsetof(SaveFile, filler25B);
    const int canonicalScore = Port_SaveLayoutScore(bytes, canonicalFlags);
    const int legacyScore = Port_SaveLayoutScore(bytes, legacyFlags);

    /* Fail closed on the inherently ambiguous early-v1.0 case: a zero byte at
     * 0x25B is indistinguishable from canonical padding without version
     * metadata. Likewise, never reinterpret a coherent canonical record. */
    if (bytes[legacyFlags] == 0 || Port_SaveLayoutIsCoherent(bytes, canonicalFlags) ||
        !Port_SaveLayoutIsCoherent(bytes, legacyFlags)) {
        return false;
    }

    /* Require the legacy interpretation to win by at least one ordered story
     * transition even after the structural checks above. */
    return legacyScore >= canonicalScore + 3;
}

static inline bool Port_SaveNormalizeLegacyLayout(SaveFile* save) {
    u8* bytes;
    const size_t canonicalFlags = offsetof(SaveFile, flags);
    const size_t legacyFlags = offsetof(SaveFile, filler25B);

    if (save == NULL || save->initialized != 1 || !Port_SaveLayoutLooksLegacy(save)) {
        return false;
    }

    bytes = (u8*)save;
    /* The v1.0 compiler inserted its own alignment byte at 0x48B before the
     * u32 timers. Only flags/dungeon arrays were shifted; 0x48C..0x4FF were
     * already canonical and must remain untouched. */
    memmove(bytes + canonicalFlags, bytes + legacyFlags, offsetof(SaveFile, darknut_timer) - canonicalFlags);
    bytes[legacyFlags] = 0;
    return true;
}

#endif /* PORT_SAVE_LAYOUT_H */
