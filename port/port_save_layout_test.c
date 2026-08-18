#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "flags.h"
#include "item_ids.h"
#include "port_save_layout.h"
#include "save.h"

static int sFailures;

/* Minimal model of the affected v1.0 tail. Its u32 member verifies the real C
 * ABI boundary that stopped the one-byte displacement before the timers. */
typedef struct {
    u8 prefix[0x25B];
    u8 flags[0x200];
    u8 dungeonKeys[0x10];
    u8 dungeonItems[0x10];
    u8 dungeonWarps[0x10];
    u32 timers[8];
    u8 filler4ac[0x54];
} LegacySaveFileTailModel;

#define CHECK(condition, message)                                  \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr, "FAIL: %s\n", message);              \
            sFailures++;                                           \
        }                                                          \
    } while (0)

static unsigned ImportedGlobalFlag(const SaveFile* save, unsigned flag) {
    return (save->flags[flag >> 3] >> (flag & 7)) & 1u;
}

static unsigned ImportedInventoryValue(const SaveFile* save, unsigned item) {
    return (save->inventory[item >> 2] >> ((item & 3) << 1)) & 3u;
}

static void SetFlagAtOffset(unsigned char* bytes, size_t flagsOffset, unsigned flag) {
    bytes[flagsOffset + (flag >> 3)] |= 1u << (flag & 7);
}

static void BuildCanonicalProgressState(SaveFile* save, unsigned progress, unsigned storyStage,
                                        unsigned advancedInventory, unsigned filler) {
    unsigned char* bytes = (unsigned char*)save;

    memset(save, 0, sizeof(*save));
    save->initialized = 1;
    save->global_progress = progress;
    save->filler25B = filler;
    if (progress >= 2) SetFlagAtOffset(bytes, offsetof(SaveFile, flags), LV1_CLEAR);
    if (progress >= 5) SetFlagAtOffset(bytes, offsetof(SaveFile, flags), LV3_CLEAR);
    if (progress >= 6) SetFlagAtOffset(bytes, offsetof(SaveFile, flags), LV4_CLEAR);
    if (progress >= 8) SetFlagAtOffset(bytes, offsetof(SaveFile, flags), LV5_CLEAR);
    if (storyStage >= 1) SetFlagAtOffset(bytes, offsetof(SaveFile, flags), START);
    if (storyStage >= 2) SetFlagAtOffset(bytes, offsetof(SaveFile, flags), EZERO_1ST);
    if (storyStage >= 3) SetFlagAtOffset(bytes, offsetof(SaveFile, flags), TABIDACHI);
    if (advancedInventory) {
        save->inventory[ITEM_PEGASUS_BOOTS >> 2] |= 1u << ((ITEM_PEGASUS_BOOTS & 3) << 1);
    }
}

static void EncodeV1LegacyAffectedRange(SaveFile* save) {
    unsigned char* bytes = (unsigned char*)save;

    memmove(bytes + offsetof(SaveFile, filler25B), bytes + offsetof(SaveFile, flags),
            offsetof(SaveFile, darknut_timer) - offsetof(SaveFile, flags));
    bytes[offsetof(SaveFile, darknut_timer) - 1] = 0; /* compiler alignment */
}

int main(void) {
    unsigned char canonical[0x500] = { 0 };
    SaveFile imported;
    SaveFile legacy;
    unsigned canonicalCases = 0;
    unsigned falsePositives = 0;
    unsigned vanillaCanonicalCases = 0;
    unsigned vanillaFalsePositives = 0;
    unsigned progressedLegacyCases = 0;
    unsigned missedProgressedLegacy = 0;
    unsigned vanillaLegacyCases = 0;
    unsigned missedVanillaLegacy = 0;
    unsigned progress;
    unsigned storyStage;
    unsigned advancedInventory;
    unsigned filler;
    unsigned neighboringFlags;
    size_t i;

    /* These are EEPROM record offsets, independent of the host compiler. */
    canonical[0x001] = 1; /* initialized */
    canonical[0x008] = 9; /* global_progress */
    canonical[0x25C + (LV1_CLEAR >> 3)] |= 1u << (LV1_CLEAR & 7);
    canonical[0x25C + (LV3_CLEAR >> 3)] |= 1u << (LV3_CLEAR & 7);
    canonical[0x25C + (LV4_CLEAR >> 3)] |= 1u << (LV4_CLEAR & 7);
    canonical[0x25C + (LV5_CLEAR >> 3)] |= 1u << (LV5_CLEAR & 7);
    canonical[0x25C + (START >> 3)] |= 1u << (START & 7);
    canonical[0x25C + (EZERO_1ST >> 3)] |= 1u << (EZERO_1ST & 7);
    canonical[0x25C + (TABIDACHI >> 3)] |= 1u << (TABIDACHI & 7);
    canonical[0x25C + (TATEKAKE_HOUSE >> 3)] |= 1u << (TATEKAKE_HOUSE & 7);
    canonical[0x25C + (DASHBOOTS >> 3)] |= 1u << (DASHBOOTS & 7);
    canonical[0x0F2 + (ITEM_PEGASUS_BOOTS >> 2)] |= 1u << ((ITEM_PEGASUS_BOOTS & 3) << 1);
    canonical[0x0F2 + (ITEM_SKILL_DASH_ATTACK >> 2)] |= 1u << ((ITEM_SKILL_DASH_ATTACK & 3) << 1);
    canonical[0x0A8 + 0x0D] = ITEM_FOURSWORD; /* Stats.equipped[SLOT_B] */
    canonical[0x45C] = 7;
    canonical[0x46C] = 4;
    canonical[0x47C] = 2;
    canonical[0x48B] = 0xA7; /* final dungeonWarps byte */
    for (i = 0x48C; i < sizeof(canonical); i++) {
        canonical[i] = (unsigned char)(i * 37u + 11u);
    }

    memcpy(&imported, canonical, sizeof(imported));

    CHECK(sizeof(KinstoneSave) == 0x147, "KinstoneSave remains the canonical 0x147 bytes");
    CHECK(sizeof(imported) == sizeof(canonical), "SaveFile remains one 0x500-byte EEPROM record");
    CHECK(offsetof(LegacySaveFileTailModel, flags) == 0x25B, "v1.0 flags actually began at 0x25B");
    CHECK(offsetof(LegacySaveFileTailModel, dungeonWarps) == 0x47B,
          "v1.0 dungeon warps actually ended at 0x48A");
    CHECK(offsetof(LegacySaveFileTailModel, timers) == 0x48C,
          "v1.0 ABI padding realigned timers to canonical offset 0x48C");
    CHECK(sizeof(LegacySaveFileTailModel) == 0x500, "v1.0 record still occupied 0x500 bytes");
    CHECK(offsetof(SaveFile, filler25B) == 0x25B, "the EEPROM padding byte remains at 0x25B");
    CHECK(offsetof(SaveFile, flags) == 0x25C, "global/local flags begin at canonical offset 0x25C");
    CHECK(offsetof(SaveFile, darknut_timer) == 0x48C, "aligned timers begin at canonical offset 0x48C");
    CHECK(imported.filler25B == 0, "canonical padding byte stays clear");
    CHECK(ImportedGlobalFlag(&imported, EZERO_1ST), "an imported save keeps the Ezlo flag");
    CHECK(ImportedGlobalFlag(&imported, TATEKAKE_HOUSE), "an imported save keeps the completed-house flag");
    CHECK(ImportedGlobalFlag(&imported, DASHBOOTS), "an imported save keeps the Castor Wilds dash-hint flag");
    CHECK(ImportedInventoryValue(&imported, ITEM_PEGASUS_BOOTS) == 1,
          "an imported save keeps the Pegasus Boots inventory item");
    CHECK(ImportedInventoryValue(&imported, ITEM_SKILL_DASH_ATTACK) == 1,
          "an imported save keeps the Dash Attack skill");
    CHECK(imported.stats.equipped[SLOT_B] == ITEM_FOURSWORD,
          "an imported save keeps the equipped sword used by the dash animation");
    CHECK(imported.dungeonKeys[0] == 7, "dungeon key counts begin at canonical offset 0x45C");
    CHECK(imported.dungeonItems[0] == 4, "dungeon item flags begin at canonical offset 0x46C");
    CHECK(imported.dungeonWarps[0] == 2, "dungeon warp flags begin at canonical offset 0x47C");
    CHECK(!Port_SaveNormalizeLegacyLayout(&imported), "a canonical emulator save is never shifted again");

    /* Encode the same record exactly as v1.0 did. Flags and the three dungeon
     * arrays were early; compiler alignment at 0x48B put timers/tail back at
     * their canonical offsets. */
    memcpy(&legacy, canonical, sizeof(legacy));
    EncodeV1LegacyAffectedRange(&legacy);
    CHECK(Port_SaveNormalizeLegacyLayout(&legacy), "a progressed v1.0 save is detected and migrated");
    CHECK(memcmp(&legacy, canonical, sizeof(legacy)) == 0,
          "v1.0 migration restores shifted fields without altering canonical timers or tail");

    /* Before the first dungeon, legacy global-flags byte zero is ambiguous
     * with canonical padding. Without version metadata migration must fail
     * closed rather than risk rewriting a canonical save. */
    memset(&legacy, 0, sizeof(legacy));
    legacy.initialized = 1;
    ((unsigned char*)&legacy)[0x25B + (START >> 3)] |= 1u << (START & 7);
    ((unsigned char*)&legacy)[0x25B + (EZERO_1ST >> 3)] |= 1u << (EZERO_1ST & 7);
    ((unsigned char*)&legacy)[0x25B + (TABIDACHI >> 3)] |= 1u << (TABIDACHI & 7);
    memcpy(&imported, &legacy, sizeof(imported));
    CHECK(!Port_SaveNormalizeLegacyLayout(&legacy), "an ambiguous early-story v1.0 save fails closed");
    CHECK(memcmp(&legacy, &imported, sizeof(legacy)) == 0,
          "a rejected ambiguous v1.0 save remains byte-for-byte untouched");

    memset(&imported, 0, sizeof(imported));
    imported.initialized = 1;
    imported.flags[START >> 3] |= 1u << (START & 7);
    imported.flags[EZERO_1ST >> 3] |= 1u << (EZERO_1ST & 7);
    imported.flags[TABIDACHI >> 3] |= 1u << (TABIDACHI & 7);
    CHECK(!Port_SaveNormalizeLegacyLayout(&imported), "an early-story canonical save remains canonical");

    /* A sidecar-active/torn rando save can intentionally violate vanilla
     * story ordering. MACHI_SET_2/3/4 occupy precisely the neighboring bits
     * that the one-byte-early view would mistake for START/EZERO/TABIDACHI.
     * Canonical padding zero is the decisive fail-closed evidence here. */
    BuildCanonicalProgressState(&imported, 1, 0, 0, 0);
    SetFlagAtOffset((unsigned char*)&imported, offsetof(SaveFile, flags), MACHI_SET_2);
    SetFlagAtOffset((unsigned char*)&imported, offsetof(SaveFile, flags), MACHI_SET_3);
    SetFlagAtOffset((unsigned char*)&imported, offsetof(SaveFile, flags), MACHI_SET_4);
    memcpy(&legacy, &imported, sizeof(legacy));
    CHECK(!Port_SaveNormalizeLegacyLayout(&imported),
          "a canonical rando/story-skip exception with neighboring MACHI bits fails closed");
    CHECK(memcmp(&imported, &legacy, sizeof(imported)) == 0,
          "the canonical rando/story-skip exception remains byte-for-byte untouched");

    /* v1.0 and current rando sidecars share the same format and contain no
     * producer-layout marker. An early E1 rando record (global byte zero) is
     * therefore ambiguous and must also fail closed. Once LV1_CLEAR supplies
     * nonzero legacy-global evidence, the same rando layout is recoverable. */
    BuildCanonicalProgressState(&legacy, 1, 3, 0, 0);
    EncodeV1LegacyAffectedRange(&legacy);
    memcpy(&imported, &legacy, sizeof(imported));
    CHECK(!Port_SaveNormalizeLegacyLayout(&legacy), "an ambiguous early E1 rando save fails closed");
    CHECK(memcmp(&legacy, &imported, sizeof(legacy)) == 0,
          "an ambiguous early E1 rando save remains byte-for-byte untouched");

    BuildCanonicalProgressState(&imported, 2, 3, 0, 0);
    memcpy(&legacy, &imported, sizeof(legacy));
    EncodeV1LegacyAffectedRange(&legacy);
    CHECK(Port_SaveNormalizeLegacyLayout(&legacy), "a progressed E1 rando save has enough evidence to migrate");
    CHECK(memcmp(&legacy, &imported, sizeof(legacy)) == 0,
          "a progressed E1 rando migration restores its canonical bytes exactly");

    /* Real vanilla LV1 completion also sets MACHI_SET_1. After the E1 shift,
     * that bit masquerades as canonical LV1_CLEAR while the three prologue
     * bits masquerade as MACHI_SET_2/3/4. The strict policy must leave the
     * mathematically ambiguous bytes alone; the vanilla policy has enough
     * game-order evidence to recover report 2 exactly. */
    BuildCanonicalProgressState(&imported, 2, 3, 0, 0);
    SetFlagAtOffset((unsigned char*)&imported, offsetof(SaveFile, flags), MACHI_SET_1);
    memcpy(&legacy, &imported, sizeof(legacy));
    EncodeV1LegacyAffectedRange(&legacy);
    CHECK(!Port_SaveNormalizeLegacyLayout(&legacy),
          "the strict policy fails closed on the LV1/MACHI_SET_1 byte collision");
    CHECK(Port_SaveNormalizeLegacyLayoutWithPolicy(&legacy, true),
          "the vanilla policy detects the real LV1/MACHI_SET_1 E1 fixture");
    CHECK(memcmp(&legacy, &imported, sizeof(legacy)) == 0,
          "the vanilla LV1 fixture restores every shifted flag and dungeon byte");

    /* The indistinguishable canonical story-skip pattern is possible only for
     * randomizer/corrupt state. Runtime randomizer loads use the strict policy,
     * so even a nonzero padding byte cannot trigger a false migration. */
    BuildCanonicalProgressState(&imported, 2, 0, 0, 1u << (LV1_CLEAR & 7));
    SetFlagAtOffset((unsigned char*)&imported, offsetof(SaveFile, flags), MACHI_SET_2);
    SetFlagAtOffset((unsigned char*)&imported, offsetof(SaveFile, flags), MACHI_SET_3);
    SetFlagAtOffset((unsigned char*)&imported, offsetof(SaveFile, flags), MACHI_SET_4);
    memcpy(&legacy, &imported, sizeof(legacy));
    CHECK(!Port_SaveNormalizeLegacyLayoutWithPolicy(&imported, false),
          "strict randomizer policy rejects the canonical LV1 collision twin");
    CHECK(memcmp(&imported, &legacy, sizeof(imported)) == 0,
          "the canonical LV1 collision twin remains byte-for-byte untouched");

    /* Every coherent post-LV1 v1.0 state has a nonzero legacy-global byte.
     * Cover all progress values, ordered story stages, and allowed inventory
     * combinations to ensure fail-closed handling does not strand progressed
     * E1 vanilla or rando saves. */
    for (progress = 2; progress <= 10; progress++) {
        for (storyStage = 0; storyStage <= 3; storyStage++) {
            for (advancedInventory = 0; advancedInventory <= 1; advancedInventory++) {
                if (advancedInventory && storyStage < 2) {
                    continue;
                }
                BuildCanonicalProgressState(&imported, progress, storyStage, advancedInventory, 0);
                memcpy(&legacy, &imported, sizeof(legacy));
                EncodeV1LegacyAffectedRange(&legacy);
                progressedLegacyCases++;
                if (!Port_SaveNormalizeLegacyLayout(&legacy) || memcmp(&legacy, &imported, sizeof(legacy)) != 0) {
                    missedProgressedLegacy++;
                }
            }
        }
    }
    CHECK(progressedLegacyCases == 54, "the progressed E1 audit covers all planned states");
    CHECK(missedProgressedLegacy == 0, "the progressed E1 audit detects and restores every state");

    /* Vanilla post-LV1 saves always completed the prologue and defeated the
     * Big Green Chuchu. Exercise the actual collision at every later progress
     * value and with/without representative advanced inventory. */
    for (progress = 2; progress <= 10; progress++) {
        for (advancedInventory = 0; advancedInventory <= 1; advancedInventory++) {
            BuildCanonicalProgressState(&imported, progress, 3, advancedInventory, 0);
            SetFlagAtOffset((unsigned char*)&imported, offsetof(SaveFile, flags), MACHI_SET_1);
            memcpy(&legacy, &imported, sizeof(legacy));
            EncodeV1LegacyAffectedRange(&legacy);
            vanillaLegacyCases++;
            if (!Port_SaveNormalizeLegacyLayoutWithPolicy(&legacy, true) ||
                memcmp(&legacy, &imported, sizeof(legacy)) != 0) {
                missedVanillaLegacy++;
            }
        }
    }
    CHECK(vanillaLegacyCases == 18, "the vanilla E1 audit covers every post-LV1 progress state");
    CHECK(missedVanillaLegacy == 0, "the vanilla E1 audit restores every real MACHI_SET_1 state");

    /* False-positive audit: exercise every progress value, every coherent
     * START/EZERO/TABIDACHI/inventory combination, all 256 possible values of
     * nominal padding, and all 256 neighboring global-flag bytes that a
     * shifted interpretation could mistake for story flags. */
    for (progress = 0; progress <= 10; progress++) {
        for (storyStage = 0; storyStage <= 3; storyStage++) {
            for (advancedInventory = 0; advancedInventory <= 1; advancedInventory++) {
                if (advancedInventory && storyStage < 2) {
                    continue;
                }
                for (filler = 0; filler <= 0xFF; filler++) {
                    for (neighboringFlags = 0; neighboringFlags <= 0xFF; neighboringFlags++) {
                        BuildCanonicalProgressState(&imported, progress, storyStage, advancedInventory, filler);
                        ((unsigned char*)&imported)[offsetof(SaveFile, flags) + 1] = neighboringFlags;
                        canonicalCases++;
                        if (Port_SaveNormalizeLegacyLayout(&imported)) {
                            falsePositives++;
                        }
                        /* A valid vanilla save cannot reach progress 2 before
                         * completing START/EZERO/TABIDACHI. Audit the relaxed
                         * collision policy across all otherwise-valid vanilla
                         * states, including arbitrary padding and neighboring
                         * flag bytes. */
                        if (progress < 2 || storyStage == 3) {
                            BuildCanonicalProgressState(&imported, progress, storyStage, advancedInventory, filler);
                            ((unsigned char*)&imported)[offsetof(SaveFile, flags) + 1] = neighboringFlags;
                            vanillaCanonicalCases++;
                            if (Port_SaveNormalizeLegacyLayoutWithPolicy(&imported, true)) {
                                vanillaFalsePositives++;
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(canonicalCases == 4325376, "the canonical discriminator audit covers all planned states");
    CHECK(falsePositives == 0, "the canonical discriminator audit has zero false migrations");
    CHECK(vanillaCanonicalCases == 1966080,
          "the vanilla collision-policy audit covers every planned canonical state");
    CHECK(vanillaFalsePositives == 0, "the vanilla collision policy has zero valid-save false migrations");

    if (sFailures != 0) {
        return 1;
    }
    puts("port_save_layout_test: ALL PASS");
    return 0;
}
