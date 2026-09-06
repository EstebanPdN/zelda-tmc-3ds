/* Production-path Kinstone save repair and retail-fickleness integration.
 *
 * This links src/common.c and calls GetFusionToOffer itself.  A synthetic ROM
 * models the exact E1 EU table displacement without embedding retail assets;
 * the real semantic validators, real gSave mutation order and real durable
 * profile preservation are exercised together. */

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "asm.h"
#include "entity.h"
#include "item_ids.h"
#include "kinstone.h"
#include "port_rom.h"
#include "port_save.h"
#include "port_types.h"
#include "region.h"
#include "save.h"

#define EEPROM_SIZE 0x2000u
#define TEST_ROM_SIZE 0x8000u

extern KinstoneId GetFusionToOffer(Entity* entity);
extern u16 EEPROMConfigure(u16 type);

typedef struct FuserFixture {
    u8 id;
    u8 e1RecordOffer;
    u8 e1SavedOffer;
    u8 retailOffer;
    bool oddRetailPointer;
} FuserFixture;

static const FuserFixture kFixtures[] = {
    /* Wind visitor, light-blue Tingle, magic-boomerang Tingle. */
    { 0x30, 0x0C, 0x0C, 0x0F, false },
    { 0x3C, 0x34, 0x34, 0x40, false },
    /* E1's cursor is RANDOM; 0x3C is the exact shared offer persisted by the
     * report and must remain semantically valid against that stale record. */
    { 0x3D, KINSTONE_RANDOM, 0x3C, 0x4D, true },
    /* Three special/intermittent reports. */
    { 0x5B, 0x13, 0x13, 0x4C, false },
    { 0x5D, 0x4A, 0x4A, 0x64, false },
    { 0x66, 0x40, 0x40, 0x29, false },
    /* Remaining strange-wall records; 0x69 is the intentional equal-offer
     * control and therefore needs no mutation. */
    { 0x67, 0x4D, 0x4D, 0x29, false },
    { 0x68, 0x5A, 0x5A, 0x29, false },
    { 0x69, 0x29, 0x29, 0x29, false },
    { 0x6A, 0x62, 0x62, 0x29, false },
    /* Cave Gorons omitted by the earlier helper-only fixtures. */
    { 0x6B, 0x21, 0x21, 0x33, true },
    { 0x6C, 0x1B, 0x1B, 0x0E, false },
};

SaveFile gSave;
int gActiveRegion = TMC_REGION_EU;
u8* gRomData;
u32 gRomSize;

static _Alignas(4) u8 sRom[TEST_ROM_SIZE];
static u32 sFuserId;
static u32 sRandomValue;
static bool sRandomizerEnabled;
static int sFailures;

#define CHECK(condition, message)                                                                    \
    do {                                                                                             \
        if (!(condition)) {                                                                          \
            fprintf(stderr, "port_kinstone_integration_test: FAIL: %s\n", message);                \
            ++sFailures;                                                                             \
        }                                                                                            \
    } while (0)

u32 GetFuserId(Entity* entity) {
    (void)entity;
    return sFuserId;
}

u32 GetInventoryValue(u32 item) {
    return item == ITEM_KINSTONE_BAG ? 1u : 0u;
}

u32 Random(void) {
    return sRandomValue;
}

bool Port_Config_GetRandoEnabled(void) {
    return sRandomizerEnabled;
}

int Port_QuickSave_ClearAll(void) {
    return 1;
}

void* Port_GetFuserFusionData(u32 fuserId) {
    const u32 table = REGION_IS_EU ? PORT_FUSER_FUSION_PTRS_EU : PORT_FUSER_FUSION_PTRS_USA;
    return (void*)Port_ResolveFuserDataFromRom(gRomData, gRomSize, table, fuserId,
                                               PORT_FUSER_FUSION_RECORD_BYTES);
}

static void WriteU32(u8* dst, u32 value) {
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8);
    dst[2] = (u8)(value >> 16);
    dst[3] = (u8)(value >> 24);
}

static void InstallRecord(u32 table, u32 id, u32 recordOffset, u8 stability, u8 offer) {
    u8* record = sRom + recordOffset;
    WriteU32(sRom + table + id * sizeof(u32), 0x08000000u + recordOffset);
    memset(record, 0, PORT_FUSER_FUSION_RECORD_BYTES);
    record[0] = 0;         /* available from global progress zero */
    record[1] = stability; /* percent chance that the bubble is visible */
    record[2] = 0x0A;
    record[3] = 0x1E;
    record[4] = 0x3C;
    record[5] = offer;
    record[6] = 0;
}

static void BuildRomFixtures(void) {
    size_t i;
    memset(sRom, 0, sizeof(sRom));
    for (i = 0; i < sizeof(kFixtures) / sizeof(kFixtures[0]); ++i) {
        const u32 staleOffset = 0x4000u + (u32)i * 0x20u;
        u32 retailOffset = 0x6000u + (u32)i * 0x20u;
        if (kFixtures[i].oddRetailPointer) ++retailOffset;
        InstallRecord(PORT_FUSER_FUSION_PTRS_USA, kFixtures[i].id, staleOffset, 100,
                      kFixtures[i].e1RecordOffer);
        InstallRecord(PORT_FUSER_FUSION_PTRS_EU, kFixtures[i].id, retailOffset, 100,
                      kFixtures[i].retailOffer);
    }
    gRomData = sRom;
    gRomSize = sizeof(sRom);
}

static int WriteBlankProfile(const char* path) {
    u8 bytes[EEPROM_SIZE];
    FILE* file;
    int ok;
    memset(bytes, 0xFF, sizeof(bytes));
    file = fopen(path, "wb");
    if (file == NULL) return 0;
    ok = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static size_t ReadBytes(const char* path, void* bytes, size_t size) {
    FILE* file = fopen(path, "rb");
    size_t got = 0;
    if (file != NULL) {
        got = fread(bytes, 1, size, file);
        if (got == size && fgetc(file) != EOF) got = 0;
        fclose(file);
    }
    return got;
}

static int FileExists(const char* path) {
    struct stat info;
    return stat(path, &info) == 0;
}

static int FileIsBlankProfile(const char* path) {
    u8 bytes[EEPROM_SIZE];
    size_t i;
    if (ReadBytes(path, bytes, sizeof(bytes)) != sizeof(bytes)) return 0;
    for (i = 0; i < sizeof(bytes); ++i) {
        if (bytes[i] != 0xFF) return 0;
    }
    return 1;
}

static void ActivateBlankProfile(const char* path) {
    CHECK(WriteBlankProfile(path), "blank raw profile is written");
    CHECK(Port_Save_SetActivePath(path), "Kinstone test profile is accepted");
    CHECK(EEPROMConfigure(0x40) == 0, "Kinstone test profile initializes EEPROM emulation");
}

static void SeedSave(u32 fuserId, u8 offer) {
    memset(&gSave, 0, sizeof(gSave));
    gSave.initialized = 1;
    gSave.global_progress = 10;
    gSave.kinstones.fuserProgress[fuserId] = 0;
    gSave.kinstones.fuserOffers[fuserId] = offer;
}

static void TestEenieCancellationRecovery(void) {
    Entity entity = {0};
    sFuserId = 0x3f;
    gActiveRegion = TMC_REGION_EU;
    sRandomizerEnabled = false;
    sRandomValue = 0;
    InstallRecord(PORT_FUSER_FUSION_PTRS_EU, sFuserId, 0x6800, 100, 0x29);
    ActivateBlankProfile("tmc_kinstone_eenie.sav");
    SeedSave(sFuserId, KINSTONE_FUSER_DONE);
    CHECK(GetFusionToOffer(&entity) == 0x29, "cancelled EU Eenie fusion becomes available");
    CHECK(FileIsBlankProfile("tmc_kinstone_eenie.sav.pre-fuser-repair.bak"), "Eenie repair preserves raw profile");
    SeedSave(sFuserId, KINSTONE_FUSER_DONE);
    gSave.kinstones.fusedKinstones[0x29 / 8] |= 1u << (0x29 % 8);
    SaveFile before = gSave;
    CHECK(GetFusionToOffer(&entity) == KINSTONE_NONE, "completed Eenie stays done");
    CHECK(memcmp(&before, &gSave, sizeof(before)) == 0, "completed Eenie is unchanged");
    SeedSave(sFuserId, KINSTONE_FUSER_DONE);
    before = gSave;
    sRandomizerEnabled = true;
    CHECK(GetFusionToOffer(&entity) == KINSTONE_NONE, "randomizer Eenie is not migrated");
    CHECK(memcmp(&before, &gSave, sizeof(before)) == 0, "randomizer sentinel preserved");
    sRandomizerEnabled = false;
    ActivateBlankProfile("tmc_kinstone_eenie-unavailable.sav");
    remove("tmc_kinstone_eenie-unavailable.sav");
    SeedSave(sFuserId, KINSTONE_FUSER_DONE);
    before = gSave;
    CHECK(GetFusionToOffer(&entity) == KINSTONE_NONE, "unbacked Eenie repair rejected");
    CHECK(memcmp(&before, &gSave, sizeof(before)) == 0, "failed backup leaves Eenie unchanged");
}

static void TestAllEuE1Repairs(void) {
    Entity entity;
    size_t i;
    memset(&entity, 0, sizeof(entity));
    gActiveRegion = TMC_REGION_EU;
    sRandomizerEnabled = false;
    sRandomValue = 0;

    for (i = 0; i < sizeof(kFixtures) / sizeof(kFixtures[0]); ++i) {
        char path[64];
        char backup[96];
        SaveFile expected;
        KinstoneId result;
        const FuserFixture* fixture = &kFixtures[i];

        snprintf(path, sizeof(path), "tmc_kinstone_%02x.sav", fixture->id);
        snprintf(backup, sizeof(backup), "%s.pre-fuser-repair.bak", path);
        ActivateBlankProfile(path);
        sFuserId = fixture->id;
        SeedSave(fixture->id, fixture->e1SavedOffer);
        expected = gSave;
        if (fixture->e1SavedOffer != fixture->retailOffer) {
            expected.kinstones.fuserProgress[fixture->id] = 0;
            expected.kinstones.fuserOffers[fixture->id] = fixture->retailOffer;
        }

        result = GetFusionToOffer(&entity);
        CHECK(result == fixture->retailOffer, "EU E1 fixture resolves the retail offer through production");
        CHECK(memcmp(&gSave, &expected, sizeof(gSave)) == 0,
              "EU E1 repair mutates only the exact fuser offer/cursor bytes");
        if (fixture->e1SavedOffer != fixture->retailOffer) {
            CHECK(FileExists(backup), "EU E1 repair creates its durable profile backup");
            CHECK(FileIsBlankProfile(backup), "EU E1 repair backup preserves the complete raw 8 KiB profile");
        } else {
            CHECK(!FileExists(backup), "already-correct strange-wall fixture requires no backup or mutation");
        }
    }
}

static void TestNoMutationControls(void) {
    Entity entity;
    SaveFile before;
    KinstoneId result;
    const FuserFixture* fixture = &kFixtures[0];
    memset(&entity, 0, sizeof(entity));
    sFuserId = fixture->id;
    sRandomValue = 0;

    ActivateBlankProfile("tmc_kinstone_correct_eu.sav");
    gActiveRegion = TMC_REGION_EU;
    sRandomizerEnabled = false;
    SeedSave(fixture->id, fixture->retailOffer);
    before = gSave;
    result = GetFusionToOffer(&entity);
    CHECK(result == fixture->retailOffer && memcmp(&gSave, &before, sizeof(gSave)) == 0,
          "correct EU save remains byte-for-byte unchanged");
    CHECK(!FileExists("tmc_kinstone_correct_eu.sav.pre-fuser-repair.bak"),
          "correct EU save is never backed up as a false repair");

    ActivateBlankProfile("tmc_kinstone_usa_mismatch.sav");
    gActiveRegion = TMC_REGION_USA;
    sRandomizerEnabled = false;
    /* The synthetic USA table contains the state E1 EU accidentally read.
     * Feed it the EU-retail offer: it is a semantic mismatch, but region
     * provenance must forbid automatic mutation. */
    SeedSave(fixture->id, fixture->retailOffer);
    before = gSave;
    result = GetFusionToOffer(&entity);
    CHECK(result == KINSTONE_NONE && memcmp(&gSave, &before, sizeof(gSave)) == 0,
          "USA semantic mismatch fails closed without changing any save byte");
    CHECK(!FileExists("tmc_kinstone_usa_mismatch.sav.pre-fuser-repair.bak"),
          "USA state is never treated as an E1 EU repair");

    ActivateBlankProfile("tmc_kinstone_rando_mismatch.sav");
    gActiveRegion = TMC_REGION_EU;
    sRandomizerEnabled = true;
    SeedSave(fixture->id, fixture->e1SavedOffer);
    before = gSave;
    result = GetFusionToOffer(&entity);
    CHECK(result == KINSTONE_NONE && memcmp(&gSave, &before, sizeof(gSave)) == 0,
          "randomizer semantic mismatch fails closed without changing any save byte");
    CHECK(!FileExists("tmc_kinstone_rando_mismatch.sav.pre-fuser-repair.bak"),
          "randomizer state is never treated as an E1 EU repair");
    sRandomizerEnabled = false;
}

static void TestBackupFailureIsTransactional(void) {
    Entity entity;
    SaveFile before;
    KinstoneId result;
    const FuserFixture* fixture = &kFixtures[sizeof(kFixtures) / sizeof(kFixtures[0]) - 1];
    memset(&entity, 0, sizeof(entity));
    ActivateBlankProfile("tmc_kinstone_backup_fail.sav");
    gActiveRegion = TMC_REGION_EU;
    sRandomizerEnabled = false;
    sRandomValue = 0;
    sFuserId = fixture->id;
    SeedSave(fixture->id, fixture->e1SavedOffer);
    before = gSave;
    Port_Save_TestFailNextPreserve();
    result = GetFusionToOffer(&entity);
    CHECK(result == KINSTONE_NONE, "failed durable backup refuses the fuser repair");
    CHECK(memcmp(&gSave, &before, sizeof(gSave)) == 0,
          "failed durable backup leaves the entire live save byte-for-byte unchanged");
    CHECK(!FileExists("tmc_kinstone_backup_fail.sav.pre-fuser-repair.bak"),
          "failed durable backup leaves no partial preservation file");
}

static void TestRetailFicklenessKeepsOffer(void) {
    Entity entity;
    SaveFile before;
    KinstoneId hidden;
    KinstoneId visible;
    const FuserFixture* fixture = &kFixtures[0];
    const u32 recordOffset = 0x7000u;
    memset(&entity, 0, sizeof(entity));

    /* Give this correct EU fuser 50% stability. GetFusionToOffer stores or
     * reuses the offer before the room-initialization mood roll. */
    InstallRecord(PORT_FUSER_FUSION_PTRS_EU, fixture->id, recordOffset, 50, fixture->retailOffer);
    ActivateBlankProfile("tmc_kinstone_fickleness.sav");
    gActiveRegion = TMC_REGION_EU;
    sRandomizerEnabled = false;
    sFuserId = fixture->id;
    SeedSave(fixture->id, fixture->retailOffer);
    before = gSave;

    sRandomValue = 75;
    hidden = GetFusionToOffer(&entity);
    CHECK(hidden == KINSTONE_NONE, "Random()%100 above stability hides the retail fusion bubble");
    CHECK(memcmp(&gSave, &before, sizeof(gSave)) == 0,
          "hidden fickle bubble keeps the same persisted offer and cursor");

    sRandomValue = 25;
    visible = GetFusionToOffer(&entity);
    CHECK(visible == fixture->retailOffer, "a later room roll below stability reveals the same fusion bubble");
    CHECK(memcmp(&gSave, &before, sizeof(gSave)) == 0,
          "visible fickle bubble still keeps the same persisted offer and cursor");
    CHECK(!FileExists("tmc_kinstone_fickleness.sav.pre-fuser-repair.bak"),
          "retail fickleness never invokes migration or backup");
}

static void RemoveTestDirectory(const char* directory, const char* oldDirectory) {
    DIR* dir;
    struct dirent* entry;
    if (chdir(oldDirectory) != 0) return;
    dir = opendir(directory);
    if (dir == NULL) return;
    while ((entry = readdir(dir)) != NULL) {
        char path[1024];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        remove(path);
    }
    closedir(dir);
    rmdir(directory);
}

int main(void) {
    char oldDirectory[1024];
    char tempTemplate[] = "/tmp/tmc-kinstone-integration-XXXXXX";
    char* tempDirectory;

    CHECK(getcwd(oldDirectory, sizeof(oldDirectory)) != NULL, "current directory is available");
    tempDirectory = mkdtemp(tempTemplate);
    CHECK(tempDirectory != NULL, "private Kinstone-test directory is created");
    if (tempDirectory == NULL || chdir(tempDirectory) != 0) return 1;

    BuildRomFixtures();
    TestAllEuE1Repairs();
    TestNoMutationControls();
    TestBackupFailureIsTransactional();
    TestRetailFicklenessKeepsOffer();
    TestEenieCancellationRecovery();

    RemoveTestDirectory(tempDirectory, oldDirectory);
    if (sFailures != 0) return 1;
    puts("port_kinstone_integration_test: ALL PASS");
    return 0;
}
