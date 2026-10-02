#include <stdio.h>

#include "main.h"

int gActiveRegion = TMC_REGION_USA;

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        int got__ = (int)(actual);                                                                               \
        int want__ = (int)(expected);                                                                            \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got %d expected %d\n", message, got__, want__);                           \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

int main(void) {
    gActiveRegion = TMC_REGION_USA;
    CHECK_EQ(RegionLanguageSlotCount(), NUM_LANGUAGES, "USA uses the public language slot count");
    CHECK_EQ(RegionDefaultLanguage(), LANGUAGE_EN, "USA defaults to English");
    CHECK_EQ(RegionPreferredLanguageToSaveSlot(LANGUAGE_EN), LANGUAGE_EN, "USA English preference is slot 1");
    CHECK_EQ(RegionSaveLanguageValid(LANGUAGE_EN), TRUE, "USA accepts English save slot");

    gActiveRegion = TMC_REGION_EU;
    CHECK_EQ(RegionLanguageSlotCount(), LANGUAGE_SLOT_COUNT, "EU exposes slot 6 for Italian resources");
    CHECK_EQ(RegionDefaultLanguage(), EU_LANGUAGE_EN_SLOT, "EU defaults to internal English slot 2");
    CHECK_EQ(RegionPreferredLanguageToSaveSlot(LANGUAGE_JP), EU_LANGUAGE_EN_SLOT,
             "EU maps unsupported Japanese preference to English");
    CHECK_EQ(RegionPreferredLanguageToSaveSlot(LANGUAGE_EN), EU_LANGUAGE_EN_SLOT,
             "EU maps user-facing English to internal slot 2");
    CHECK_EQ(RegionPreferredLanguageToSaveSlot(LANGUAGE_FR), 3, "EU maps French to internal slot 3");
    CHECK_EQ(RegionPreferredLanguageToSaveSlot(LANGUAGE_IT), 6, "EU maps Italian to internal slot 6");
    CHECK_EQ(RegionPreferredLanguageToSaveSlot(NUM_LANGUAGES), -1, "EU rejects out-of-range preference");
    CHECK_EQ(RegionSaveLanguageValid(LANGUAGE_EN), FALSE, "EU rejects USA English slot 1");
    CHECK_EQ(RegionSaveLanguageValid(EU_LANGUAGE_EN_SLOT), TRUE, "EU accepts English slot 2");
    CHECK_EQ(RegionSaveLanguageValid(EU_LANGUAGE_LAST_SLOT), TRUE, "EU accepts Italian slot 6");
    CHECK_EQ(RegionSaveLanguageValid(LANGUAGE_SLOT_COUNT), FALSE, "EU rejects slot 7");

    gActiveRegion = TMC_REGION_JP;
    CHECK_EQ(RegionLanguageSlotCount(), NUM_LANGUAGES, "JP uses public language slot count");
    CHECK_EQ(RegionDefaultLanguage(), LANGUAGE_JP, "JP defaults to Japanese");
    CHECK_EQ(RegionPreferredLanguageToSaveSlot(LANGUAGE_EN), LANGUAGE_JP,
             "JP maps any preference to Japanese");
    CHECK_EQ(RegionSaveLanguageValid(LANGUAGE_JP), TRUE, "JP accepts Japanese save slot");
    CHECK_EQ(RegionSaveLanguageValid(LANGUAGE_EN), FALSE, "JP rejects English save slot");

    if (sFailures != 0) {
        return 1;
    }
    printf("port_region_language_test: ALL PASS\n");
    return 0;
}
