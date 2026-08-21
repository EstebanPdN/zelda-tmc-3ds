/* End-to-end legacy-save migration test.
 *
 * Unlike port_save_layout_test, this links the real src/save.c and the real
 * file-backed EEPROM implementation.  Fixtures enter through an exact raw
 * 8 KiB profile and leave through ReadSaveFile, including preservation,
 * normalization, duplicate-record writes and durability failures. */

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "flags.h"
#include "gba/macro.h"
#include "item_ids.h"
#include "port_save.h"
#include "port_types.h"
#include "region.h"
#include "save.h"

#define EEPROM_SIZE 0x2000u
#define EEPROM_BLOCK_SIZE 8u
#define SLOT_SIZE 0x500u
#define SLOT0_DATA1 0x0080u
#define SLOT0_DATA2 0x1080u
#define SLOT0_STATUS1 0x0030u
#define SLOT0_STATUS2 0x1030u
#define SLOT0_BLOCK1 (SLOT0_DATA1 / EEPROM_BLOCK_SIZE)
#define SLOT0_BLOCK2 (SLOT0_DATA2 / EEPROM_BLOCK_SIZE)

extern s32 ReadSaveFile(u32 index, SaveFile* saveFile);
extern bool32 Port_Save_TestLastDoubleWriteFullyRedundant(void);
extern u16 EEPROMConfigure(u16 type);

int gActiveRegion = TMC_REGION_USA;

static bool sRandomizerEnabled;
static int sFailures;

bool Port_Config_GetRandoEnabled(void) {
    return sRandomizerEnabled;
}

int Port_QuickSave_ClearAll(void) {
    return 1;
}

/* src/save.c uses CpuSet to clear a failed read.  Keep the host shim local so
 * this harness does not pull the full game/BIOS executable into the test. */
void CpuSet(const void* source, void* destination, u32 control) {
    const size_t units = control & 0x1FFFFFu;
    const bool word = (control & CPU_SET_32BIT) != 0;
    const bool fixed = (control & CPU_SET_SRC_FIXED) != 0;
    size_t i;

    if (word) {
        const u32* src = source;
        u32* dst = destination;
        for (i = 0; i < units; ++i) dst[i] = src[fixed ? 0 : i];
    } else {
        const u16* src = source;
        u16* dst = destination;
        for (i = 0; i < units; ++i) dst[i] = src[fixed ? 0 : i];
    }
}

#define CHECK(condition, message)                                                                        \
    do {                                                                                                 \
        if (!(condition)) {                                                                              \
            fprintf(stderr, "port_save_migration_integration_test: FAIL: %s\n", message);             \
            ++sFailures;                                                                                 \
        }                                                                                                \
    } while (0)

static void WriteU16(u8* dst, u16 value) {
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8);
}

static void WriteU32(u8* dst, u32 value) {
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8);
    dst[2] = (u8)(value >> 16);
    dst[3] = (u8)(value >> 24);
}

static u16 ReadU16(const u8* src) {
    return (u16)((u16)src[0] | ((u16)src[1] << 8));
}

static u32 ReadU32(const u8* src) {
    return (u32)src[0] | ((u32)src[1] << 8) | ((u32)src[2] << 16) | ((u32)src[3] << 24);
}

static u16 Checksum(const u8* data, u32 size) {
    u32 checksum = 0;
    while (size != 0) {
        checksum += ReadU16(data) ^ size;
        data += 2;
        size -= 2;
    }
    return (u16)checksum;
}

static void ReverseBlocks(u8* bytes) {
    size_t block;
    for (block = 0; block < EEPROM_SIZE; block += EEPROM_BLOCK_SIZE) {
        size_t i;
        for (i = 0; i < EEPROM_BLOCK_SIZE / 2; ++i) {
            const u8 value = bytes[block + i];
            bytes[block + i] = bytes[block + EEPROM_BLOCK_SIZE - 1 - i];
            bytes[block + EEPROM_BLOCK_SIZE - 1 - i] = value;
        }
    }
}

static int WriteBytes(const char* path, const void* bytes, size_t size) {
    FILE* file = fopen(path, "wb");
    int ok = file != NULL;
    if (ok) ok = fwrite(bytes, 1, size, file) == size;
    if (file != NULL && fclose(file) != 0) ok = 0;
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

static int FilesEqual(const char* left, const char* right) {
    u8 a[EEPROM_SIZE];
    u8 b[EEPROM_SIZE];
    return ReadBytes(left, a, sizeof(a)) == sizeof(a) && ReadBytes(right, b, sizeof(b)) == sizeof(b) &&
           memcmp(a, b, sizeof(a)) == 0;
}

static void SetSaveFlag(SaveFile* save, unsigned flag) {
    save->flags[flag >> 3] |= (u8)(1u << (flag & 7));
}

static void BuildCanonicalVanilla(SaveFile* save) {
    size_t i;
    memset(save, 0, sizeof(*save));
    save->initialized = 1;
    save->global_progress = 2;
    save->stats.health = 24;
    save->stats.maxHealth = 32;
    SetSaveFlag(save, START);
    SetSaveFlag(save, EZERO_1ST);
    SetSaveFlag(save, TABIDACHI);
    SetSaveFlag(save, LV1_CLEAR);
    SetSaveFlag(save, MACHI_SET_1);
    save->dungeonKeys[0] = 7;
    save->dungeonItems[0] = 4;
    save->dungeonWarps[0] = 2;
    for (i = offsetof(SaveFile, darknut_timer); i < sizeof(*save); ++i) {
        ((u8*)save)[i] = (u8)(i * 37u + 11u);
    }
}

static void BuildCanonicalRandoCollisionTwin(SaveFile* save) {
    memset(save, 0, sizeof(*save));
    save->initialized = 1;
    save->global_progress = 2;
    save->filler25B = (u8)(1u << (LV1_CLEAR & 7));
    SetSaveFlag(save, LV1_CLEAR);
    SetSaveFlag(save, MACHI_SET_2);
    SetSaveFlag(save, MACHI_SET_3);
    SetSaveFlag(save, MACHI_SET_4);
}

static void BuildAmbiguousEarlyLegacy(SaveFile* save) {
    memset(save, 0, sizeof(*save));
    save->initialized = 1;
    save->global_progress = 1;
    SetSaveFlag(save, START);
    SetSaveFlag(save, EZERO_1ST);
    SetSaveFlag(save, TABIDACHI);
}

static void EncodeLegacyLayout(SaveFile* save) {
    u8* bytes = (u8*)save;
    memmove(bytes + offsetof(SaveFile, filler25B), bytes + offsetof(SaveFile, flags),
            offsetof(SaveFile, darknut_timer) - offsetof(SaveFile, flags));
    bytes[offsetof(SaveFile, darknut_timer) - 1] = 0;
}

static void WriteStatus(u8* ram, u32 statusOffset, u32 dataOffset) {
    const u32 status = (u32)'MCZ3';
    u16 checksum;
    memset(ram + statusOffset, 0xFF, 16);
    WriteU32(ram + statusOffset + 4, status);
    checksum = (u16)(Checksum(ram + statusOffset + 4, 4) + Checksum(ram + dataOffset, SLOT_SIZE));
    WriteU16(ram + statusOffset, checksum);
    WriteU16(ram + statusOffset + 2, (u16)-checksum);
}

static void BuildRawProfile(u8 disk[EEPROM_SIZE], const SaveFile* first, const SaveFile* second) {
    static const char signature[0x21] = "AGBZELDA:THE MINISH CAP:ZELDA 5";
    memset(disk, 0xFF, EEPROM_SIZE);
    memcpy(disk, signature, 0x20);
    memcpy(disk + 0x1000, signature, 0x20);
    memcpy(disk + SLOT0_DATA1, first, sizeof(*first));
    memcpy(disk + SLOT0_DATA2, second, sizeof(*second));
    WriteStatus(disk, SLOT0_STATUS1, SLOT0_DATA1);
    WriteStatus(disk, SLOT0_STATUS2, SLOT0_DATA2);
    ReverseBlocks(disk);
}

static int StatusMatches(const u8* ram, u32 statusOffset, u32 dataOffset, const SaveFile* expected) {
    const u16 checksum = ReadU16(ram + statusOffset);
    const u16 calculated =
        (u16)(Checksum(ram + statusOffset + 4, 4) + Checksum(ram + dataOffset, SLOT_SIZE));
    return ReadU32(ram + statusOffset + 4) == (u32)'MCZ3' &&
           ReadU16(ram + statusOffset + 2) == (u16)-checksum && checksum == calculated &&
           memcmp(ram + dataOffset, expected, sizeof(*expected)) == 0;
}

static int ReadProfileRam(const char* path, u8 ram[EEPROM_SIZE]) {
    if (ReadBytes(path, ram, EEPROM_SIZE) != EEPROM_SIZE) return 0;
    ReverseBlocks(ram);
    return 1;
}

static void ActivateProfile(const char* path) {
    CHECK(Port_Save_SetActivePath(path), "test profile is accepted");
    CHECK(EEPROMConfigure(0x40) == 0, "raw profile initializes EEPROM emulation");
}

static void TestCanonicalCompatibility(void) {
    const char* path = "tmc_migration_canonical.sav";
    SaveFile canonical;
    SaveFile loaded;
    u8 original[EEPROM_SIZE];
    u8 after[EEPROM_SIZE];

    BuildCanonicalVanilla(&canonical);
    BuildRawProfile(original, &canonical, &canonical);
    CHECK(WriteBytes(path, original, sizeof(original)), "canonical raw profile is written");
    ActivateProfile(path);
    memset(&loaded, 0xA5, sizeof(loaded));
    CHECK(ReadSaveFile(0, &loaded) == 1, "canonical raw profile loads through ReadSaveFile");
    CHECK(memcmp(&loaded, &canonical, sizeof(loaded)) == 0, "canonical prior save stays byte-identical");
    CHECK(ReadBytes(path, after, sizeof(after)) == sizeof(after) && memcmp(after, original, sizeof(after)) == 0,
          "canonical prior raw profile is never rewritten");
    CHECK(!FileExists("tmc_migration_canonical.sav.pre-migration.bak"),
          "canonical prior save creates no migration backup");
}

static void TestAmbiguousAndRandoFailClosed(void) {
    SaveFile ambiguous;
    SaveFile randoTwin;
    SaveFile loaded;
    u8 original[EEPROM_SIZE];
    u8 after[EEPROM_SIZE];

    BuildAmbiguousEarlyLegacy(&ambiguous);
    EncodeLegacyLayout(&ambiguous);
    BuildRawProfile(original, &ambiguous, &ambiguous);
    CHECK(WriteBytes("tmc_migration_ambiguous.sav", original, sizeof(original)),
          "ambiguous E1 profile is written");
    ActivateProfile("tmc_migration_ambiguous.sav");
    CHECK(ReadSaveFile(0, &loaded) == 1, "ambiguous E1 profile remains readable");
    CHECK(memcmp(&loaded, &ambiguous, sizeof(loaded)) == 0, "ambiguous E1 bytes fail closed in memory");
    CHECK(ReadBytes("tmc_migration_ambiguous.sav", after, sizeof(after)) == sizeof(after) &&
              memcmp(after, original, sizeof(after)) == 0,
          "ambiguous E1 raw profile fails closed on disk");
    CHECK(!FileExists("tmc_migration_ambiguous.sav.pre-migration.bak"),
          "ambiguous E1 profile is not mislabeled as a migration");

    BuildCanonicalRandoCollisionTwin(&randoTwin);
    BuildRawProfile(original, &randoTwin, &randoTwin);
    CHECK(WriteBytes("tmc_migration_rando.sav", original, sizeof(original)),
          "randomizer collision-twin profile is written");
    sRandomizerEnabled = true;
    ActivateProfile("tmc_migration_rando.sav");
    CHECK(ReadSaveFile(0, &loaded) == 1, "randomizer collision twin remains readable");
    CHECK(memcmp(&loaded, &randoTwin, sizeof(loaded)) == 0,
          "randomizer policy never rewrites a canonical collision twin");
    CHECK(ReadBytes("tmc_migration_rando.sav", after, sizeof(after)) == sizeof(after) &&
              memcmp(after, original, sizeof(after)) == 0,
          "randomizer collision twin stays byte-identical on disk");
    CHECK(!FileExists("tmc_migration_rando.sav.pre-migration.bak"),
          "randomizer collision twin creates no migration backup");
    sRandomizerEnabled = false;
}

static void TestSuccessfulMigration(void) {
    const char* path = "tmc_migration_success.sav";
    const char* backup = "tmc_migration_success.sav.pre-migration.bak";
    SaveFile canonical;
    SaveFile legacy;
    SaveFile loaded;
    SaveFile loadedAgain;
    u8 original[EEPROM_SIZE];
    u8 ram[EEPROM_SIZE];
    u8 statusBytes[4];
    u16 fixtureChecksum;

    BuildCanonicalVanilla(&canonical);
    legacy = canonical;
    EncodeLegacyLayout(&legacy);
    WriteU32(statusBytes, (u32)'MCZ3');
    fixtureChecksum = (u16)(Checksum(statusBytes, sizeof(statusBytes)) + Checksum((u8*)&legacy, SLOT_SIZE));
    CHECK(fixtureChecksum == 0xAACEu,
          "raw E1 fixture keeps the high-bit checksum that exercises unsigned checksum reconstruction");
    BuildRawProfile(original, &legacy, &legacy);
    CHECK(WriteBytes(path, original, sizeof(original)), "unambiguous E1 raw profile is written");
    ActivateProfile(path);
    CHECK(ReadSaveFile(0, &loaded) == 1, "unambiguous E1 profile loads through production flow");
    CHECK(Port_Save_TestLastDoubleWriteFullyRedundant(),
          "a successful migration records both durable canonical copies and statuses");
    CHECK(memcmp(&loaded, &canonical, sizeof(loaded)) == 0, "production flow normalizes exact E1 bytes");
    CHECK(FileExists(backup) && FilesEqual(path, backup) == 0,
          "migration preserves a distinct pre-migration raw profile");
    CHECK(ReadBytes(backup, ram, sizeof(ram)) == sizeof(ram) && memcmp(ram, original, sizeof(ram)) == 0,
          "pre-migration backup is the original raw 8 KiB byte-for-byte");
    CHECK(ReadProfileRam(path, ram), "migrated raw profile can be decoded");
    CHECK(StatusMatches(ram, SLOT0_STATUS1, SLOT0_DATA1, &canonical),
          "normal migration persists a valid canonical first copy");
    CHECK(StatusMatches(ram, SLOT0_STATUS2, SLOT0_DATA2, &canonical),
          "normal migration persists a valid canonical second copy");
    CHECK(ReadSaveFile(0, &loadedAgain) == 1 && memcmp(&loadedAgain, &canonical, sizeof(loadedAgain)) == 0,
          "migrated profile is immediately compatible with the normal reader");
    CHECK(!FileExists("tmc_migration_success.sav.pre-migration.001.bak"),
          "a canonical reread does not create a second migration backup");
}

static void TestBackupFailure(void) {
    const char* path = "tmc_migration_backup_fail.sav";
    SaveFile canonical;
    SaveFile legacy;
    SaveFile loaded;
    u8 original[EEPROM_SIZE];
    u8 after[EEPROM_SIZE];

    BuildCanonicalVanilla(&canonical);
    legacy = canonical;
    EncodeLegacyLayout(&legacy);
    BuildRawProfile(original, &legacy, &legacy);
    CHECK(WriteBytes(path, original, sizeof(original)), "backup-failure E1 profile is written");
    ActivateProfile(path);
    Port_Save_TestFailNextPreserve();
    CHECK(ReadSaveFile(0, &loaded) == 1, "backup failure does not make the source unreadable");
    CHECK(memcmp(&loaded, &legacy, sizeof(loaded)) == 0,
          "backup failure rolls normalization back byte-for-byte in memory");
    CHECK(ReadBytes(path, after, sizeof(after)) == sizeof(after) && memcmp(after, original, sizeof(after)) == 0,
          "backup failure leaves the raw source byte-for-byte intact");
    CHECK(!FileExists("tmc_migration_backup_fail.sav.pre-migration.bak"),
          "injected backup failure leaves no partial backup");
}

static void TestDurableWriteFailure(void) {
    const char* path = "tmc_migration_write_fail.sav";
    const char* backup = "tmc_migration_write_fail.sav.pre-migration.bak";
    SaveFile canonical;
    SaveFile legacy;
    SaveFile loaded;
    PortSaveStats stats;
    u8 original[EEPROM_SIZE];
    u8 after[EEPROM_SIZE];

    BuildCanonicalVanilla(&canonical);
    legacy = canonical;
    EncodeLegacyLayout(&legacy);
    BuildRawProfile(original, &legacy, &legacy);
    CHECK(WriteBytes(path, original, sizeof(original)), "write-failure E1 profile is written");
    ActivateProfile(path);
    Port_Save_TestFailNextAtomicWrite();
    CHECK(ReadSaveFile(0, &loaded) == 1, "durability failure keeps the readable source status");
    CHECK(!Port_Save_TestLastDoubleWriteFullyRedundant(),
          "failed durable flush is never classified as a fully redundant write");
    CHECK(memcmp(&loaded, &canonical, sizeof(loaded)) == 0,
          "durability failure keeps normalized state in memory for this session");
    CHECK(ReadBytes(path, after, sizeof(after)) == sizeof(after) && memcmp(after, original, sizeof(after)) == 0,
          "durability failure leaves the original raw file byte-for-byte intact");
    CHECK(FileExists(backup) && ReadBytes(backup, after, sizeof(after)) == sizeof(after) &&
              memcmp(after, original, sizeof(after)) == 0,
          "durability failure retains an exact permanent pre-migration backup");
    Port_Save_GetStats(&stats);
    CHECK(stats.dirty && stats.flushFailures != 0,
          "durability failure remains dirty and observable for a later retry");

    /* Do not let the test's later profile switch retry and overwrite the raw
     * failure fixture. Clearing occurs only inside this private temp dir. */
    CHECK(Port_Save_ClearActiveProfileData(), "failed-write test state resets without a retry flush");
}

static void TestOneGoodDuplicateIsReadable(const char* path, u16 failingBlock, bool firstShouldSurvive) {
    char backup[128];
    SaveFile canonical;
    SaveFile legacy;
    SaveFile loaded;
    SaveFile reread;
    u8 original[EEPROM_SIZE];
    u8 ram[EEPROM_SIZE];
    int firstValid;
    int secondValid;

    snprintf(backup, sizeof(backup), "%s.pre-migration.bak", path);
    BuildCanonicalVanilla(&canonical);
    legacy = canonical;
    EncodeLegacyLayout(&legacy);
    BuildRawProfile(original, &legacy, &legacy);
    CHECK(WriteBytes(path, original, sizeof(original)), "partial-copy E1 profile is written");
    ActivateProfile(path);
    Port_Save_TestFailNextEepromWriteAtBlock(failingBlock);
    CHECK(ReadSaveFile(0, &loaded) == 1, "one successful EEPROM duplicate is accepted by retail semantics");
    CHECK(!Port_Save_TestLastDoubleWriteFullyRedundant(),
          "a partial duplicate write preserves retail success without claiming full redundancy");
    CHECK(memcmp(&loaded, &canonical, sizeof(loaded)) == 0,
          "partial duplicate migration keeps canonical state in memory");
    CHECK(FileExists(backup), "partial duplicate migration still has a permanent backup");
    CHECK(ReadProfileRam(path, ram), "partial duplicate raw profile can be decoded");
    firstValid = StatusMatches(ram, SLOT0_STATUS1, SLOT0_DATA1, &canonical);
    secondValid = StatusMatches(ram, SLOT0_STATUS2, SLOT0_DATA2, &canonical);
    CHECK(firstValid == (firstShouldSurvive ? 1 : 0), "first duplicate has the expected validity");
    CHECK(secondValid == (firstShouldSurvive ? 0 : 1), "second duplicate has the expected validity");
    CHECK(firstValid + secondValid == 1,
          "retail OR-success semantics leave exactly one durable canonical duplicate after one block fault");
    CHECK(ReadSaveFile(0, &reread) == 1 && memcmp(&reread, &canonical, sizeof(reread)) == 0,
          "normal reader recovers the canonical slot from either surviving duplicate");
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
    char tempTemplate[] = "/tmp/tmc-save-migration-integration-XXXXXX";
    char* tempDirectory;

    CHECK(sizeof(SaveFile) == SLOT_SIZE, "SaveFile remains the compatible 0x500-byte record");
    CHECK(getcwd(oldDirectory, sizeof(oldDirectory)) != NULL, "current directory is available");
    tempDirectory = mkdtemp(tempTemplate);
    CHECK(tempDirectory != NULL, "private migration-test directory is created");
    if (tempDirectory == NULL || chdir(tempDirectory) != 0) return 1;

    TestCanonicalCompatibility();
    TestAmbiguousAndRandoFailClosed();
    TestSuccessfulMigration();
    TestBackupFailure();
    TestOneGoodDuplicateIsReadable("tmc_migration_copy1_fail.sav", SLOT0_BLOCK1, false);
    TestOneGoodDuplicateIsReadable("tmc_migration_copy2_fail.sav", SLOT0_BLOCK2, true);
    TestDurableWriteFailure();

    RemoveTestDirectory(tempDirectory, oldDirectory);
    if (sFailures != 0) return 1;
    puts("port_save_migration_integration_test: ALL PASS");
    return 0;
}
