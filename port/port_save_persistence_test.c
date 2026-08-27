#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "port_save.h"
#include "port_types.h"
#include "region.h"

#define EEPROM_SIZE 8192
#define EEPROM_BLOCK 8

int gActiveRegion = TMC_REGION_USA;

bool Port_Config_GetRandoEnabled(void) {
    return false;
}

int Port_QuickSave_ClearAll(void) {
    return 1;
}

u16 EEPROMConfigure(u16 type);
u16 EEPROMRead(u16 block, u16* dest);
u16 EEPROMWrite0_8k_Check(u16 block, const u16* src);
void Port_Save_TestFailNextRecoveryInstall(void);
void Port_Save_TestFailNextProfileCommit(void);

static int sFailures;

#define CHECK(condition, message)                                  \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr, "FAIL: %s\n", message);              \
            sFailures++;                                           \
        }                                                          \
    } while (0)

static void ReverseBlocks(u8* image) {
    size_t block;
    for (block = 0; block < EEPROM_SIZE; block += EEPROM_BLOCK) {
        size_t i;
        for (i = 0; i < EEPROM_BLOCK / 2; ++i) {
            u8 value = image[block + i];
            image[block + i] = image[block + EEPROM_BLOCK - 1 - i];
            image[block + EEPROM_BLOCK - 1 - i] = value;
        }
    }
}

static u16 ReadU16ForTest(const u8* data) {
    return (u16)((u16)data[0] | ((u16)data[1] << 8));
}

static u16 CalculateChecksumForTest(const u8* data, u32 size) {
    u32 checksum = 0;
    while (size != 0) {
        checksum += ReadU16ForTest(data) ^ size;
        data += 2;
        size -= 2;
    }
    return (u16)checksum;
}

static void WriteInitStatus(u8* image, u32 offset) {
    const u32 status = (u32)'TINI';
    memset(image + offset, 0xFF, 4);
    memcpy(image + offset + 4, &status, sizeof(status));
}

static void WriteSavedStatus(u8* image, u32 statusOffset, u32 dataOffset, u32 dataSize) {
    const u32 status = (u32)'MCZ3';
    u16 checksum;
    u16 complement;
    memset(image + statusOffset, 0xFF, 16);
    memcpy(image + statusOffset + 4, &status, sizeof(status));
    checksum = CalculateChecksumForTest(image + statusOffset + 4, 4);
    checksum = (u16)(checksum + CalculateChecksumForTest(image + dataOffset, dataSize));
    complement = (u16)(-checksum);
    memcpy(image + statusOffset, &checksum, sizeof(checksum));
    memcpy(image + statusOffset + 2, &complement, sizeof(complement));
}

static void BuildDiskImage(u8* image, const char* signature, u8 marker) {
    static const u16 initStatusOffsets[] = { 0x20, 0x1020, 0x30, 0x1030, 0x40, 0x1040,
                                             0x50, 0x1050, 0x60, 0x1060 };
    size_t i;
    memset(image, 0xFF, EEPROM_SIZE);
    memcpy(image, signature, 0x20);
    memcpy(image + 0x1000, signature, 0x20);
    image[0x80] = marker;
    for (i = 0; i < sizeof(initStatusOffsets) / sizeof(initStatusOffsets[0]); ++i) {
        WriteInitStatus(image, initStatusOffsets[i]);
    }
    /* Slot 0 is valid only through a real MCZ3 data checksum. This makes every
     * recovery fixture exercise semantic validation, not signature presence. */
    memset(image + 0x1030, 0xFF, 16);
    WriteSavedStatus(image, 0x30, 0x80, 0x500);
    ReverseBlocks(image);
}

static int WriteBytes(const char* path, const void* data, size_t size) {
    FILE* file = fopen(path, "wb");
    int ok = file != NULL;
    if (ok) ok = fwrite(data, 1, size, file) == size;
    if (file != NULL && fclose(file) != 0) ok = 0;
    return ok;
}

static int AppendByte(const char* path, u8 value) {
    FILE* file = fopen(path, "ab");
    int ok = file != NULL;
    if (ok) ok = fwrite(&value, 1, 1, file) == 1;
    if (file != NULL && fclose(file) != 0) ok = 0;
    return ok;
}

static void CorruptEveryStatusRecord(u8* diskImage) {
    ReverseBlocks(diskImage);
    memset(diskImage + 0x20, 0, 0x50);
    memset(diskImage + 0x1020, 0, 0x50);
    ReverseBlocks(diskImage);
}

static void LeaveOnlyHeaderStatusValid(u8* diskImage) {
    ReverseBlocks(diskImage);
    memset(diskImage + 0x30, 0, 0x40);
    memset(diskImage + 0x1030, 0, 0x40);
    ReverseBlocks(diskImage);
}

static size_t ReadBytes(const char* path, void* data, size_t capacity) {
    FILE* file = fopen(path, "rb");
    size_t size = 0;
    if (file != NULL) {
        size = fread(data, 1, capacity, file);
        fclose(file);
    }
    return size;
}

static int FileExistsForTest(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) return 0;
    fclose(file);
    return 1;
}

static int FilesEqualForTest(const char* leftPath, const char* rightPath) {
    u8 left[EEPROM_SIZE];
    u8 right[EEPROM_SIZE];
    return ReadBytes(leftPath, left, sizeof(left)) == sizeof(left) &&
           ReadBytes(rightPath, right, sizeof(right)) == sizeof(right) && memcmp(left, right, sizeof(left)) == 0;
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
    static const char usaSignature[0x21] = "AGBZELDA:THE MINISH CAP:ZELDA 5";
    static const char euSignature[0x21] = "AGBZELDA:THE MINISH CAP:ZELDA 3";
    const int testEu = getenv("TMC_SAVE_TEST_EU") != NULL;
    const char* activeSignature = testEu ? euSignature : usaSignature;
    const char* otherRegionSignature = testEu ? usaSignature : euSignature;
    char oldDirectory[1024];
    char tempTemplate[] = "/tmp/tmc-save-persistence-XXXXXX";
    char* tempDirectory;
    u8 image[EEPROM_SIZE];
    u8 compare[EEPROM_SIZE];
    u8 vaatiSlot[0x500];
    u16 block[4] = { 0x1234, 0x5678, 0x9ABC, 0xDEF0 };
    const u8 shortFile[] = { 0x12, 0x34, 0x56 };

    gActiveRegion = testEu ? TMC_REGION_EU : TMC_REGION_USA;
    CHECK(getcwd(oldDirectory, sizeof(oldDirectory)) != NULL, "current directory is available");
    tempDirectory = mkdtemp(tempTemplate);
    CHECK(tempDirectory != NULL, "private temporary test directory is created");
    if (tempDirectory == NULL || chdir(tempDirectory) != 0) return 1;

    CHECK(WriteBytes("tmc_short.sav", shortFile, sizeof(shortFile)), "short-file fixture is written");
    Port_Save_SetActivePath("tmc_short.sav");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0, "short existing save blocks every EEPROM write");
    CHECK(ReadBytes("tmc_short.sav", compare, sizeof(compare)) == sizeof(shortFile),
          "short existing save keeps its original length");
    CHECK(memcmp(compare, shortFile, sizeof(shortFile)) == 0, "short existing save remains byte-for-byte intact");
    Port_Save_BeginTransaction();
    CHECK(!Port_Save_EndTransaction(), "blocked short save cannot report a successful transaction");

    BuildDiskImage(image, activeSignature, 0x91);
    CHECK(WriteBytes("tmc_long.sav", image, sizeof(image)) && AppendByte("tmc_long.sav", 0x5A),
          "long-file fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_long.sav"), "long-file profile path is selected");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0, "long existing save blocks every EEPROM write");
    CHECK(ReadBytes("tmc_long.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) == 0,
          "long existing save keeps its original first 8 KiB");
    {
        struct stat status;
        CHECK(stat("tmc_long.sav", &status) == 0 && status.st_size == EEPROM_SIZE + 1,
              "long existing save keeps its original length");
    }

    BuildDiskImage(image, otherRegionSignature, 0xE1);
    CHECK(WriteBytes("tmc_wrong_region.sav", image, sizeof(image)), "wrong-region fixture is written");
    Port_Save_SetActivePath("tmc_wrong_region.sav");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0, "wrong-region profile blocks writes instead of reinitializing");
    CHECK(ReadBytes("tmc_wrong_region.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) == 0,
          "wrong-region profile remains byte-for-byte intact");

    BuildDiskImage(image, activeSignature, 0xA0);
    ReverseBlocks(image);
    memcpy(image + 0x1000, otherRegionSignature, 0x20);
    ReverseBlocks(image);
    CHECK(WriteBytes("tmc_mixed_region.sav", image, sizeof(image)), "mixed-signature fixture is written");
    Port_Save_SetActivePath("tmc_mixed_region.sav");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0, "mixed USA/EU signatures fail closed");
    CHECK(ReadBytes("tmc_mixed_region.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) == 0,
          "mixed-signature profile remains byte-for-byte intact");

    BuildDiskImage(image, activeSignature, 0xA2);
    ReverseBlocks(image);
    image[0x80] ^= 0x01; /* invalidate the only usable slot-0 MCZ3 copy */
    ReverseBlocks(image);
    CHECK(WriteBytes("tmc_partial_corruption.sav", image, sizeof(image)), "partial-corruption fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_partial_corruption.sav"), "partial-corruption profile path is selected");
    EEPROMConfigure(0x40);
    CHECK(EEPROMRead(0, block) == 0, "one corrupt slot does not hide the other recoverable records");
    CHECK(ReadBytes("tmc_partial_corruption.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) == 0,
          "loading a partially corrupt profile does not rewrite it");

    BuildDiskImage(image, activeSignature, 0xA4);
    CorruptEveryStatusRecord(image);
    CHECK(WriteBytes("tmc_no_semantics.sav", image, sizeof(image)), "signature-only fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_no_semantics.sav"), "signature-only profile path is selected");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0, "signature without any valid record fails closed");
    CHECK(ReadBytes("tmc_no_semantics.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) == 0,
          "signature-only profile remains byte-for-byte intact");

    BuildDiskImage(image, activeSignature, 0xA1);
    CHECK(WriteBytes("tmc_backup.sav", image, sizeof(image)), "migration-backup fixture is written");
    Port_Save_SetActivePath("tmc_backup.sav");
    EEPROMConfigure(0x40);
    CHECK(Port_Save_PreserveBeforeMigration(), "first permanent pre-migration backup succeeds");
    CHECK(FileExistsForTest("tmc_backup.sav.pre-migration.bak"), "first backup has the stable permanent name");
    CHECK(FilesEqualForTest("tmc_backup.sav", "tmc_backup.sav.pre-migration.bak"),
          "first backup is byte-for-byte exact");
    CHECK(Port_Save_PreserveBeforeMigration(), "second permanent pre-migration backup succeeds");
    CHECK(FileExistsForTest("tmc_backup.sav.pre-migration.001.bak"),
          "second backup is unique and does not overwrite the first");

    BuildDiskImage(image, activeSignature, 0xA5);
    CHECK(WriteBytes("tmc_fuser_backup.sav", image, sizeof(image)), "fuser-repair backup fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_fuser_backup.sav"), "fuser-repair backup profile is selected");
    EEPROMConfigure(0x40);
    Port_Save_BeginTransaction();
    CHECK(EEPROMWrite0_8k_Check(900, block) == 0, "transactional fuser fixture has a pending EEPROM block");
    CHECK(!Port_Save_PreserveBeforeFuserRepair(), "fuser repair refuses a mid-transaction snapshot");
    CHECK(!FileExistsForTest("tmc_fuser_backup.sav.pre-fuser-repair.bak"),
          "refused mid-transaction repair creates no stale backup");
    CHECK(Port_Save_EndTransaction(), "fuser fixture transaction becomes durable");

    CHECK(chmod(tempDirectory, 0500) == 0, "fuser fixture directory becomes temporarily unwritable");
    CHECK(EEPROMWrite0_8k_Check(901, block) == 0, "latest fuser fixture write remains pending in memory");
    CHECK(!Port_Save_PreserveBeforeFuserRepair(), "fuser repair aborts when latest raw image cannot flush");
    CHECK(!FileExistsForTest("tmc_fuser_backup.sav.pre-fuser-repair.bak"),
          "failed latest-image flush creates no stale backup");
    CHECK(chmod(tempDirectory, 0700) == 0, "fuser fixture write permission is restored");
    CHECK(Port_Save_PreserveBeforeFuserRepair(), "first permanent pre-fuser-repair backup succeeds");
    CHECK(FileExistsForTest("tmc_fuser_backup.sav.pre-fuser-repair.bak"),
          "fuser repair has a stable permanent backup");
    CHECK(FilesEqualForTest("tmc_fuser_backup.sav", "tmc_fuser_backup.sav.pre-fuser-repair.bak"),
          "fuser-repair backup contains the latest raw 8 KiB byte-for-byte");
    CHECK(ReadBytes("tmc_fuser_backup.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) != 0,
          "latest dirty EEPROM blocks reached disk before the backup was accepted");
    CHECK(Port_Save_PreserveBeforeFuserRepair(), "later repairs reuse the verified profile backup");
    CHECK(!FileExistsForTest("tmc_fuser_backup.sav.pre-fuser-repair.001.bak"),
          "one contaminated profile does not create a backup per fuser");

    BuildDiskImage(compare, activeSignature, 0xB5);
    CHECK(WriteBytes("tmc_fuser_other.sav", compare, sizeof(compare)),
          "alternate fuser profile fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_fuser_other.sav"),
          "switching away from a preserved fuser profile succeeds");
    EEPROMConfigure(0x40);
    BuildDiskImage(image, activeSignature, 0xA6);
    CHECK(WriteBytes("tmc_fuser_backup.sav", image, sizeof(image)),
          "inactive preserved profile can be replaced with a new raw image");
    CHECK(Port_Save_SetActivePath("tmc_fuser_backup.sav"),
          "switching back to the replaced fuser profile succeeds");
    EEPROMConfigure(0x40);
    CHECK(Port_Save_PreserveBeforeFuserRepair(),
          "replaced profile receives a fresh pre-fuser-repair backup");
    CHECK(FileExistsForTest("tmc_fuser_backup.sav.pre-fuser-repair.001.bak"),
          "A-to-B-to-A replacement creates the next unique backup");
    CHECK(FilesEqualForTest("tmc_fuser_backup.sav", "tmc_fuser_backup.sav.pre-fuser-repair.001.bak"),
          "replacement backup preserves the new raw 8 KiB byte-for-byte");
    CHECK(!FilesEqualForTest("tmc_fuser_backup.sav.pre-fuser-repair.bak",
                             "tmc_fuser_backup.sav.pre-fuser-repair.001.bak"),
          "replacement backup is not the stale raw image from the first activation");
    CHECK(Port_Save_PreserveBeforeFuserRepair(),
          "later repairs still reuse the backup within the replacement activation");
    CHECK(!FileExistsForTest("tmc_fuser_backup.sav.pre-fuser-repair.002.bak"),
          "replacement activation does not create a backup per fuser");

    BuildDiskImage(image, activeSignature, 0xA8);
    CHECK(WriteBytes("tmc_smith_bottle_backup.sav", image, sizeof(image)),
          "Smith bottle flag repair backup fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_smith_bottle_backup.sav"),
          "Smith bottle flag repair backup profile is selected");
    EEPROMConfigure(0x40);
    CHECK(Port_Save_PreserveBeforeSmithBottleFlagRepair(),
          "first permanent pre-Smith-bottle-flag-repair backup succeeds");
    CHECK(FileExistsForTest("tmc_smith_bottle_backup.sav.pre-smith-bottle-flag-repair.bak"),
          "Smith bottle flag repair has a stable permanent backup");
    CHECK(FilesEqualForTest("tmc_smith_bottle_backup.sav",
                            "tmc_smith_bottle_backup.sav.pre-smith-bottle-flag-repair.bak"),
          "Smith bottle flag repair backup preserves the complete raw profile byte-for-byte");
    CHECK(Port_Save_PreserveBeforeSmithBottleFlagRepair(),
          "later Smith bottle flag repair checks reuse the verified profile backup");
    CHECK(!FileExistsForTest("tmc_smith_bottle_backup.sav.pre-smith-bottle-flag-repair.001.bak"),
          "one profile does not create repeated Smith bottle flag repair backups");

    BuildDiskImage(image, activeSignature, 0xA7);
    CHECK(WriteBytes("tmc_cloud_tops_backup.sav", image, sizeof(image)),
          "Cloud Tops repair backup fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_cloud_tops_backup.sav"),
          "Cloud Tops repair backup profile is selected");
    EEPROMConfigure(0x40);
    CHECK(Port_Save_PreserveBeforeCloudTopsRepair(),
          "first permanent pre-Cloud-Tops-repair backup succeeds");
    CHECK(FileExistsForTest("tmc_cloud_tops_backup.sav.pre-cloud-tops-repair.bak"),
          "Cloud Tops repair has a stable permanent backup");
    CHECK(FilesEqualForTest("tmc_cloud_tops_backup.sav",
                            "tmc_cloud_tops_backup.sav.pre-cloud-tops-repair.bak"),
          "Cloud Tops repair backup preserves the complete raw profile byte-for-byte");
    CHECK(Port_Save_PreserveBeforeCloudTopsRepair(),
          "later Cloud Tops repair checks reuse the verified profile backup");
    CHECK(!FileExistsForTest("tmc_cloud_tops_backup.sav.pre-cloud-tops-repair.001.bak"),
          "one profile does not create repeated Cloud Tops repair backups");

    BuildDiskImage(image, activeSignature, 0xA9);
    CHECK(WriteBytes("tmc_vaati_backup.sav", image, sizeof(image)),
          "Vaati repair backup fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_vaati_backup.sav"), "Vaati repair backup profile is selected");
    EEPROMConfigure(0x40);
    CHECK(Port_Save_PreserveBeforeVaatiProgressRepair(),
          "first permanent pre-Vaati-repair backup succeeds");
    CHECK(FileExistsForTest("tmc_vaati_backup.sav.pre-vaati-progress-repair.bak"),
          "Vaati repair has a stable permanent backup");
    memset(vaatiSlot, 0, sizeof(vaatiSlot));
    CHECK(Port_Save_ReadVaatiProgressBackupSlot(0, vaatiSlot, sizeof(vaatiSlot)) && vaatiSlot[0] == 0xA9,
          "validated Vaati backup exposes the original slot data in RAM order");
    CHECK(!Port_Save_ReadVaatiProgressBackupSlot(3, vaatiSlot, sizeof(vaatiSlot)),
          "out-of-range Vaati backup slots fail closed");
    CHECK(!Port_Save_ReadVaatiProgressBackupSlot(0, vaatiSlot, sizeof(vaatiSlot) - 1),
          "wrong-sized Vaati backup reads fail closed");
    CHECK(Port_Save_PreserveBeforeVaatiProgressRepair(),
          "later Vaati repair checks reuse the verified profile backup");
    CHECK(!FileExistsForTest("tmc_vaati_backup.sav.pre-vaati-progress-repair.001.bak"),
          "one profile does not create repeated Vaati repair backups");

    BuildDiskImage(image, activeSignature, 0xA3);
    CHECK(WriteBytes("tmc_switch.sav", image, sizeof(image)), "pending-profile-switch fixture is written");
    Port_Save_SetActivePath("tmc_switch.sav");
    EEPROMConfigure(0x40);
    Port_Save_BeginTransaction();
    CHECK(!Port_Save_SetActivePath("tmc_switch_destination.sav"),
          "an open save transaction refuses a profile switch before any writes");
    CHECK(strcmp(Port_Save_GetActivePath(), "tmc_switch.sav") == 0,
          "transaction-scoped profile switch retains the source path");
    CHECK(Port_Save_EndTransaction(), "empty transaction closes on its original profile");

    CHECK(chmod(tempDirectory, 0500) == 0, "test directory becomes temporarily unwritable");
    CHECK(EEPROMWrite0_8k_Check(10, block) == 0,
          "emulated block write succeeds while host durability remains pending");
    CHECK(!Port_Save_SetActivePath("tmc_switch_destination.sav"),
          "profile switch reports failure while the source cannot flush");
    CHECK(strcmp(Port_Save_GetActivePath(), "tmc_switch.sav") == 0,
          "failed flush refuses profile switch and retains pending state");
    CHECK(chmod(tempDirectory, 0700) == 0, "test directory write permission is restored");
    Port_Save_BeginTransaction();
    CHECK(Port_Save_EndTransaction(), "pending source profile flush retries successfully after permission recovery");

    {
        char longName[96];
        memset(longName, 'a', sizeof(longName));
        memcpy(longName, "tmc_", 4);
        memcpy(longName + sizeof(longName) - 5, ".sav", 5);
        CHECK(!Port_Save_SetActivePath(longName), "overlong managed-looking path is rejected");
        CHECK(strcmp(Port_Save_GetActivePath(), "tmc_switch.sav") == 0,
              "overlong path cannot alias the active profile through truncation");
    }

    CHECK(WriteBytes("tmc_recover.sav", shortFile, sizeof(shortFile)), "interrupted current fixture is written");
    BuildDiskImage(image, activeSignature, 0xB1);
    CHECK(WriteBytes("tmc_recover.sav.rollback", image, sizeof(image)), "valid rollback fixture is written");
    Port_Save_SetActivePath("tmc_recover.sav");
    EEPROMConfigure(0x40);
    CHECK(ReadBytes("tmc_recover.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) == 0,
          "one unambiguous valid rollback is recovered");
    CHECK(FileExistsForTest("tmc_recover.sav.interrupted.bak"), "malformed current file is preserved during recovery");
    CHECK(!FileExistsForTest("tmc_recover.sav.rollback"), "installed rollback is consumed only after recovery");

    memset(compare, 0xFF, sizeof(compare));
    CHECK(WriteBytes("tmc_blank_current.sav", compare, sizeof(compare)), "blank current fixture is written");
    BuildDiskImage(image, activeSignature, 0xB2);
    CHECK(WriteBytes("tmc_blank_current.sav.rollback", image, sizeof(image)),
          "active rollback beside blank current is written");
    CHECK(Port_Save_SetActivePath("tmc_blank_current.sav"), "blank-current recovery profile path is selected");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0,
          "blank current plus active rollback blocks instead of guessing recency");
    memset(compare, 0, sizeof(compare));
    CHECK(ReadBytes("tmc_blank_current.sav", compare, sizeof(compare)) == sizeof(compare) &&
              compare[0] == 0xFF && compare[sizeof(compare) - 1] == 0xFF,
          "ambiguous blank current remains untouched");
    CHECK(FileExistsForTest("tmc_blank_current.sav.rollback"),
          "active rollback beside blank current is preserved");

    CHECK(WriteBytes("tmc_active_plus_blank.sav", shortFile, sizeof(shortFile)),
          "active-plus-blank malformed current is written");
    BuildDiskImage(image, activeSignature, 0xB3);
    CHECK(WriteBytes("tmc_active_plus_blank.sav.rollback", image, sizeof(image)),
          "active candidate beside blank candidate is written");
    memset(compare, 0xFF, sizeof(compare));
    CHECK(WriteBytes("tmc_active_plus_blank.sav.tmp", compare, sizeof(compare)), "blank candidate is written");
    CHECK(Port_Save_SetActivePath("tmc_active_plus_blank.sav"),
          "active-plus-blank recovery profile path is selected");
    EEPROMConfigure(0x40);
    CHECK(ReadBytes("tmc_active_plus_blank.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) == 0,
          "one active candidate wins over a blank candidate");
    CHECK(FileExistsForTest("tmc_active_plus_blank.sav.tmp"), "blank candidate is retained rather than consumed");

    BuildDiskImage(image, activeSignature, 0xB5);
    CHECK(WriteBytes("tmc_absent_dual.sav.rollback", image, sizeof(image)),
          "old rollback for absent-current crash fixture is written");
    BuildDiskImage(compare, activeSignature, 0xB6);
    CHECK(WriteBytes("tmc_absent_dual.sav.tmp", compare, sizeof(compare)),
          "new temp for absent-current crash fixture is written");
    CHECK(Port_Save_SetActivePath("tmc_absent_dual.sav"), "absent-current crash profile path is selected");
    EEPROMConfigure(0x40);
    CHECK(ReadBytes("tmc_absent_dual.sav", image, sizeof(image)) == sizeof(image) &&
              memcmp(image, compare, sizeof(image)) == 0,
          "fsynced temp is installed when current is absent and rollback is retained");
    CHECK(FileExistsForTest("tmc_absent_dual.sav.rollback"),
          "old rollback remains after deterministic absent-current recovery");
    CHECK(!FileExistsForTest("tmc_absent_dual.sav.tmp"), "installed temp is consumed after recovery");

    CHECK(WriteBytes("tmc_corrupt_candidate.sav", shortFile, sizeof(shortFile)),
          "corrupt-candidate current fixture is written");
    BuildDiskImage(image, activeSignature, 0xB4);
    CorruptEveryStatusRecord(image);
    CHECK(WriteBytes("tmc_corrupt_candidate.sav.rollback", image, sizeof(image)),
          "signature-only recovery candidate is written");
    CHECK(Port_Save_SetActivePath("tmc_corrupt_candidate.sav"), "corrupt-candidate profile path is selected");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0, "signature-only recovery candidate is never installed");
    CHECK(ReadBytes("tmc_corrupt_candidate.sav", compare, sizeof(compare)) == sizeof(shortFile) &&
              memcmp(compare, shortFile, sizeof(shortFile)) == 0,
          "malformed current remains untouched when candidate has no valid records");
    CHECK(FileExistsForTest("tmc_corrupt_candidate.sav.rollback"),
          "signature-only recovery candidate remains available for manual inspection");

    BuildDiskImage(image, activeSignature, 0xB7);
    LeaveOnlyHeaderStatusValid(image);
    CHECK(WriteBytes("tmc_partial_current.sav", image, sizeof(image)),
          "one-of-five current recovery fixture is written");
    BuildDiskImage(compare, activeSignature, 0xB8);
    CHECK(WriteBytes("tmc_partial_current.sav.rollback", compare, sizeof(compare)),
          "full rollback beside partial current is written");
    CHECK(Port_Save_SetActivePath("tmc_partial_current.sav"), "partial-current profile path is selected");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0,
          "partial current plus full rollback blocks instead of guessing recency");
    CHECK(ReadBytes("tmc_partial_current.sav", compare, sizeof(compare)) == sizeof(compare) &&
              memcmp(compare, image, sizeof(image)) == 0,
          "partial current remains byte-for-byte untouched");
    CHECK(FileExistsForTest("tmc_partial_current.sav.rollback"), "full rollback beside partial current is retained");

    CHECK(WriteBytes("tmc_ambiguous.sav", shortFile, sizeof(shortFile)), "ambiguous current fixture is written");
    BuildDiskImage(image, activeSignature, 0xC1);
    CHECK(WriteBytes("tmc_ambiguous.sav.rollback", image, sizeof(image)), "ambiguous rollback fixture is written");
    BuildDiskImage(compare, activeSignature, 0xC2);
    CHECK(WriteBytes("tmc_ambiguous.sav.tmp", compare, sizeof(compare)), "ambiguous temp fixture is written");
    Port_Save_SetActivePath("tmc_ambiguous.sav");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0, "two valid recovery candidates fail closed");
    CHECK(ReadBytes("tmc_ambiguous.sav", compare, sizeof(compare)) == sizeof(shortFile) &&
              memcmp(compare, shortFile, sizeof(shortFile)) == 0,
          "ambiguous malformed current remains untouched");
    CHECK(FileExistsForTest("tmc_ambiguous.sav.rollback") && FileExistsForTest("tmc_ambiguous.sav.tmp"),
          "every ambiguous recovery candidate is retained");

    CHECK(WriteBytes("tmc_recovery_fault.sav", shortFile, sizeof(shortFile)),
          "recovery-install fault current fixture is written");
    BuildDiskImage(image, activeSignature, 0xD1);
    CHECK(WriteBytes("tmc_recovery_fault.sav.rollback", image, sizeof(image)),
          "recovery-install fault rollback fixture is written");
    Port_Save_TestFailNextRecoveryInstall();
    Port_Save_SetActivePath("tmc_recovery_fault.sav");
    EEPROMConfigure(0x40);
    CHECK(EEPROMWrite0_8k_Check(0, block) != 0, "failed recovery install leaves writes blocked");
    CHECK(ReadBytes("tmc_recovery_fault.sav", compare, sizeof(compare)) == sizeof(shortFile) &&
              memcmp(compare, shortFile, sizeof(shortFile)) == 0,
          "failed recovery install restores the original malformed current file");
    CHECK(FileExistsForTest("tmc_recovery_fault.sav.rollback"),
          "failed recovery install retains the valid candidate for manual recovery");

    {
        static const u8 sidecarBytes[] = {
            'T', 'M', 'C', 'R', 'N', 'D', 'O', '1', 6, 0, 0, 0, 0x31, 0x59, 0x26, 0x53,
        };
        static const u8 orphanBytes[] = { 0xBA, 0xAD, 0xF0, 0x0D };

        BuildDiskImage(image, activeSignature, 0xE1);
        CHECK(WriteBytes("tmc_profile_source.sav", image, sizeof(image)),
              "profile-operation source save is written");
        CHECK(WriteBytes("tmc_profile_source.randomizer", sidecarBytes, sizeof(sidecarBytes)),
              "profile-operation source sidecar is written");
        CHECK(Port_Save_SetActivePath("tmc_profile_source.sav"), "profile-operation source becomes active");
        EEPROMConfigure(0x40);

        CHECK(Port_Save_SaveAsProfile("tmc_profile_copy.sav"),
              "Save As commits the EEPROM and randomizer sidecar together");
        CHECK(FilesEqualForTest("tmc_profile_source.sav", "tmc_profile_copy.sav"),
              "Save As produces an exact raw 8 KiB EEPROM image");
        CHECK(ReadBytes("tmc_profile_copy.randomizer", compare, sizeof(compare)) == sizeof(sidecarBytes) &&
                  memcmp(compare, sidecarBytes, sizeof(sidecarBytes)) == 0,
              "Save As copies the complete sidecar byte-for-byte");

        CHECK(WriteBytes("tmc_save_as_collision.randomizer", orphanBytes, sizeof(orphanBytes)),
              "Save As collision sidecar is written");
        CHECK(!Port_Save_SaveAsProfile("tmc_save_as_collision.sav"),
              "an orphan destination sidecar blocks Save As instead of being paired silently");
        CHECK(!FileExistsForTest("tmc_save_as_collision.sav"),
              "blocked Save As never exposes a destination EEPROM");
        CHECK(ReadBytes("tmc_save_as_collision.randomizer", compare, sizeof(compare)) == sizeof(orphanBytes) &&
                  memcmp(compare, orphanBytes, sizeof(orphanBytes)) == 0,
              "blocked Save As leaves the destination sidecar untouched");

        Port_Save_TestFailNextProfileCommit();
        CHECK(!Port_Save_SaveAsProfile("tmc_save_as_fault.sav"),
              "injected Save As commit failure is reported");
        CHECK(!FileExistsForTest("tmc_save_as_fault.sav") &&
                  !FileExistsForTest("tmc_save_as_fault.randomizer"),
              "Save As rollback exposes neither half of a failed profile copy");
        CHECK(FileExistsForTest("tmc_profile_source.sav") &&
                  FileExistsForTest("tmc_profile_source.randomizer"),
              "Save As failure retains the complete source profile");

        CHECK(!Port_Save_RenameProfile("tmc_profile_source.sav", "tmc_profile_source_new.sav"),
              "active profile rename aborts without a durable config commit API");
        CHECK(strcmp(Port_Save_GetActivePath(), "tmc_profile_source.sav") == 0,
              "active rename refusal retains the configured runtime path");
        CHECK(FileExistsForTest("tmc_profile_source.sav") &&
                  FileExistsForTest("tmc_profile_source.randomizer") &&
                  !FileExistsForTest("tmc_profile_source_new.sav"),
              "active rename refusal leaves both source members intact");

        CHECK(Port_Save_RenameProfile("tmc_profile_copy.sav", "tmc_profile_renamed.sav"),
              "inactive profile rename commits save and sidecar together");
        CHECK(!FileExistsForTest("tmc_profile_copy.sav") &&
                  !FileExistsForTest("tmc_profile_copy.randomizer"),
              "successful rename removes both old live names");
        CHECK(FileExistsForTest("tmc_profile_renamed.sav") &&
                  ReadBytes("tmc_profile_renamed.randomizer", compare, sizeof(compare)) == sizeof(sidecarBytes) &&
                  memcmp(compare, sidecarBytes, sizeof(sidecarBytes)) == 0,
              "successful rename publishes both destination members");

        CHECK(Port_Save_SaveAsProfile("tmc_rename_fault.sav"),
              "rename-fault source profile is created transactionally");
        Port_Save_TestFailNextProfileCommit();
        CHECK(!Port_Save_RenameProfile("tmc_rename_fault.sav", "tmc_rename_fault_new.sav"),
              "injected rename commit failure is reported");
        CHECK(FileExistsForTest("tmc_rename_fault.sav") &&
                  FileExistsForTest("tmc_rename_fault.randomizer") &&
                  !FileExistsForTest("tmc_rename_fault_new.sav") &&
                  !FileExistsForTest("tmc_rename_fault_new.randomizer"),
              "rename rollback retains only the complete source pair");

        CHECK(Port_Save_SaveAsProfile("tmc_delete_fault.sav"),
              "delete-fault source profile is created transactionally");
        Port_Save_TestFailNextProfileCommit();
        CHECK(!Port_Save_DeleteProfile("tmc_delete_fault.sav"),
              "injected delete commit failure is reported");
        CHECK(FileExistsForTest("tmc_delete_fault.sav") &&
                  FileExistsForTest("tmc_delete_fault.randomizer"),
              "delete rollback restores the complete live profile pair");

        CHECK(Port_Save_DeleteProfile("tmc_profile_renamed.sav"),
              "confirmed inactive-profile delete removes the paired profile");
        CHECK(!FileExistsForTest("tmc_profile_renamed.sav") &&
                  !FileExistsForTest("tmc_profile_renamed.randomizer"),
              "successful delete leaves neither live profile member");
    }

    RemoveTestDirectory(tempDirectory, oldDirectory);
    if (sFailures != 0) return 1;
    puts("port_save_persistence_test: ALL PASS");
    return 0;
}
