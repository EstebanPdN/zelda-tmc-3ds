/*
 * port_save.c — File-backed EEPROM emulation + multi-profile support.
 *
 * The GBA Minish Cap uses 8 KB EEPROM (1024 blocks of 8 bytes).
 * This module stores it in `tmc.sav` (the default profile) or
 * `tmc_<name>.sav` (named profile) next to the executable.
 *
 * On-disk format (mGBA-compatible)
 * --------------------------------
 * Files are stored in the byte order mGBA/VBA-M use for EEPROM saves:
 * each 8-byte block holds its 64-bit unit in wire-transmission order,
 * which is byte-reversed relative to the game's RAM buffer (the GBA
 * driver in src/eeprom.c shifts units out data[3]→data[0], MSB-first).
 * In memory we keep game-RAM order so the BIOS shims stay straight
 * memcpys; blocks are reversed on load/flush. A Minish Cap .sav from
 * mGBA drops in directly and vice versa. Legacy port saves (RAM order
 * on disk) are detected by the save signature and migrated once, with
 * the original kept as <name>.sav.bak.
 *
 * Profile model
 * -------------
 * A *profile* is one named save file. The active profile's filename is
 * persisted in config.json so it sticks across launches. The first run
 * uses `tmc.sav` for backwards compatibility with existing installs.
 *
 * Switching the active profile mid-game is allowed: the next time the
 * game reads EEPROM (e.g. when the user returns to the file-select
 * screen) it will see the new profile's data. The current in-memory
 * `gSave` does NOT auto-reload — players who want to be loading from
 * the new profile should return to title and pick a save slot.
 *
 * Implements the four EEPROM BIOS functions:
 *   EEPROMConfigure(u16 type)
 *   EEPROMRead(u16 block, u16* dest)
 *   EEPROMWrite0_8k_Check(u16 block, const u16* src)
 *   EEPROMCompare(u16 block, const u16* src)
 *
 * Plus a small profile-management API consumed by port_debug_menu.cpp.
 */

#include "port_types.h"
#include "port_save.h"
#include "region.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#define EEPROM_SIZE 8192                           /* 8 KB */
#define EEPROM_BLOCK 8                             /* 8 bytes per block */
#define EEPROM_BLOCKS (EEPROM_SIZE / EEPROM_BLOCK) /* 1024 */
#define DEFAULT_SAVE_FILENAME "tmc.sav"
#define SAVE_FILENAME_MAX 64
#define SAVE_AUX_PATH_MAX (SAVE_FILENAME_MAX + 64)

static u8 sEeprom[EEPROM_SIZE];
static int sEepromDirty = 0; /* set on write, cleared on flush */
/* >0 while inside a game save transaction. Block writes inside a transaction
 * only mark dirty; the single atomic flush happens at EndTransaction(). The
 * game brackets both initial EEPROM setup and normal file saves. */
static int sSaveTxnDepth = 0;
/* 1 after a failed flush, reset on success — so a persistent ENOSPC/EIO
 * logs once per failure burst instead of once per write. */
static int sFlushFailedLast = 0;
static int sEepromInited = 0;
/* Existing malformed/wrong-region files are readable only as an unavailable
 * EEPROM. Never let InitSaveData turn them into a fresh save implicitly. The
 * user can still explicitly clear/switch the profile through the existing UI. */
static int sEepromWriteBlocked = 0;
static char sActivePath[SAVE_FILENAME_MAX] = DEFAULT_SAVE_FILENAME;
/* The first semantic fuser repair preserves the complete profile (all three
 * slots and duplicate records). Further contaminated fusers from that same
 * E1 image must not create a new 8 KiB backup on every NPC update. */
static char sFuserRepairPreservedPath[SAVE_FILENAME_MAX];
static PortSaveStats sSaveStats;
/* 1 once the user has explicitly chosen a named profile (config.json), so the
 * per-region default below must NOT override their choice. 0 in the default
 * case, where the multi-region build isolates each region into its own file. */
static int sExplicitProfile = 0;

#ifdef PORT_SAVE_TEST
static int sTestFailNextPreserve;
static int sTestFailNextAtomicWrite;
static int sTestFailEepromBlockArmed;
static u16 sTestFailEepromBlock;

void Port_Save_TestFailNextPreserve(void) {
    sTestFailNextPreserve = 1;
}

void Port_Save_TestFailNextAtomicWrite(void) {
    sTestFailNextAtomicWrite = 1;
}

void Port_Save_TestFailNextEepromWriteAtBlock(uint16_t block) {
    sTestFailEepromBlock = block;
    sTestFailEepromBlockArmed = 1;
}
#endif

/* ---- On-disk byte order -------------------------------------------------- */

/* First 8-byte block of every initialized TMC save ("AGBZELDA:..."), in
 * game-RAM order and in on-disk (mGBA wire) order. Same in all regions. */
#define EEPROM_SIG_RAM "AGBZELDA"
#define EEPROM_SIGNATURE_USA "AGBZELDA:THE MINISH CAP:ZELDA 5"
#define EEPROM_SIGNATURE_EU_JP "AGBZELDA:THE MINISH CAP:ZELDA 3"

/* Reverse each 8-byte block in place: converts between game-RAM order
 * (in-memory) and mGBA/VBA-M wire order (on-disk). Involution: applying
 * it twice is the identity, so blank 0xFF images are unaffected. */
static void ReverseEepromBlocks(u8* buf) {
    for (int b = 0; b < EEPROM_SIZE; b += EEPROM_BLOCK) {
        for (int i = 0; i < EEPROM_BLOCK / 2; i++) {
            u8 t = buf[b + i];
            buf[b + i] = buf[b + EEPROM_BLOCK - 1 - i];
            buf[b + EEPROM_BLOCK - 1 - i] = t;
        }
    }
}

static u8 sDiskImage[EEPROM_SIZE];

static void BuildEepromDiskImage(void) {
    memcpy(sDiskImage, sEeprom, EEPROM_SIZE);
    ReverseEepromBlocks(sDiskImage);
}

typedef enum PathState {
    PATH_STATE_ERROR = -1,
    PATH_STATE_MISSING = 0,
    PATH_STATE_EXISTS = 1,
} PathState;

static PathState GetPathState(const char* path) {
    struct stat info;
    if (stat(path, &info) == 0) return PATH_STATE_EXISTS;
    return errno == ENOENT ? PATH_STATE_MISSING : PATH_STATE_ERROR;
}

static int FileHasExactSize(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return 0;

    u8 scratch[256];
    size_t total = 0;
    while (total < EEPROM_SIZE) {
        const size_t wanted = EEPROM_SIZE - total < sizeof(scratch) ? EEPROM_SIZE - total : sizeof(scratch);
        const size_t got = fread(scratch, 1, wanted, file);
        total += got;
        if (got != wanted) break;
    }
    const int trailing = total == EEPROM_SIZE ? fgetc(file) : EOF;
    const int readOk = !ferror(file);
    const int closeOk = fclose(file) == 0;
    return total == EEPROM_SIZE && trailing == EOF && readOk && closeOk;
}

static int BufferIsAll(const u8* data, size_t size, u8 value) {
    size_t i;
    for (i = 0; i < size; ++i) {
        if (data[i] != value) return 0;
    }
    return 1;
}

static int EepromSignatureKindAt(const u8* ramImage, u32 offset) {
    if (memcmp(ramImage + offset, EEPROM_SIGNATURE_USA, 0x20) == 0) return 1;
    if (memcmp(ramImage + offset, EEPROM_SIGNATURE_EU_JP, 0x20) == 0) return 2;
    return 0;
}

static u16 ReadU16LE(const u8* data) {
    return (u16)((u16)data[0] | ((u16)data[1] << 8));
}

static u32 ReadU32LE(const u8* data) {
    return (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

static u16 CalculateImageChecksum(const u8* data, u32 size) {
    u32 checksum = 0;
    while (size != 0) {
        checksum += ReadU16LE(data) ^ size;
        data += 2;
        size -= 2;
    }
    return (u16)checksum;
}

static int EepromStatusValidForData(const u8* ramImage, u32 statusOffset, u32 dataOffset, u32 dataSize) {
    unsigned statusCopy;
    if (statusOffset > EEPROM_SIZE || 16u > EEPROM_SIZE - statusOffset || dataOffset > EEPROM_SIZE ||
        dataSize > EEPROM_SIZE - dataOffset) {
        return 0;
    }
    for (statusCopy = 0; statusCopy < 2; ++statusCopy) {
        const u8* statusBytes = ramImage + statusOffset + statusCopy * 8u;
        const u16 checksum1 = ReadU16LE(statusBytes);
        const u16 checksum2 = ReadU16LE(statusBytes + 2);
        const u32 status = ReadU32LE(statusBytes + 4);
        if ((status == (u32)'TINI' || status == (u32)'FleD') && checksum1 == 0xFFFF && checksum2 == 0xFFFF) {
            return 1;
        }
        if (status == (u32)'MCZ3' && checksum2 == (u16)(-checksum1)) {
            u16 expected = CalculateImageChecksum(statusBytes + 4, 4);
            expected = (u16)(expected + CalculateImageChecksum(ramImage + dataOffset, dataSize));
            if (checksum1 == expected) return 1;
        }
    }
    return 0;
}

static unsigned EepromValidRecordCount(const u8* ramImage) {
    static const struct {
        u16 size;
        u16 status1;
        u16 status2;
        u16 data1;
        u16 data2;
    } records[] = {
        { 0x500, 0x30, 0x1030, 0x80, 0x1080 },
        { 0x500, 0x40, 0x1040, 0x580, 0x1580 },
        { 0x500, 0x50, 0x1050, 0xA80, 0x1A80 },
        { 0x10, 0x20, 0x1020, 0x70, 0x1070 },
        { 0x20, 0x60, 0x1060, 0xF80, 0x1F80 },
    };
    size_t i;
    unsigned validCount = 0;

    for (i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        const int primaryValid = EepromStatusValidForData(ramImage, records[i].status1, records[i].data1,
                                                          records[i].size);
        const int backupValid = EepromStatusValidForData(ramImage, records[i].status2, records[i].data2,
                                                         records[i].size);
        if (primaryValid || backupValid) ++validCount;
    }
    return validCount;
}

typedef enum EepromImageClass {
    EEPROM_IMAGE_INVALID = 0,
    EEPROM_IMAGE_BLANK,
    EEPROM_IMAGE_ACTIVE_REGION,
    EEPROM_IMAGE_OTHER_REGION,
} EepromImageClass;

static EepromImageClass ClassifyRamEepromImage(const u8* ramImage, unsigned* validRecords) {
    const int signature1 = EepromSignatureKindAt(ramImage, 0);
    const int signature2 = EepromSignatureKindAt(ramImage, 0x1000);
    const int activeSignature = (REGION_IS_EU || REGION_IS_JP) ? 2 : 1;

    if (validRecords != NULL) *validRecords = 0;

    if (BufferIsAll(ramImage, EEPROM_SIZE, 0xFF)) {
        return EEPROM_IMAGE_BLANK;
    }
    /* Two recognized but different region signatures cannot be repaired
     * automatically: choosing either one would overwrite the other copy. */
    if (signature1 != 0 && signature2 != 0 && signature1 != signature2) {
        return EEPROM_IMAGE_INVALID;
    }
    if (signature1 == activeSignature || signature2 == activeSignature) {
        /* Retail reads each record independently: one damaged slot must not
         * hide other recoverable slots. Require semantic evidence beyond the
         * signature, but accept a partial image when any record still has a
         * valid duplicated status/checksum. */
        const unsigned count = EepromValidRecordCount(ramImage);
        if (validRecords != NULL) *validRecords = count;
        return count != 0 ? EEPROM_IMAGE_ACTIVE_REGION : EEPROM_IMAGE_INVALID;
    }
    if (signature1 != 0 || signature2 != 0) {
        return EEPROM_IMAGE_OTHER_REGION;
    }
    return EEPROM_IMAGE_INVALID;
}

/* Load exactly one raw 8 KiB file and select its byte order only when a known
 * signature (or an all-FF blank image) proves the interpretation. */
static EepromImageClass ReadAndClassifyEepromFile(const char* path, u8* ramImage, int* legacyRamOrder,
                                                   unsigned* validRecords) {
    FILE* file = fopen(path, "rb");
    size_t got;
    int trailing;
    int readOk;
    int closeOk;
    EepromImageClass rawClass;
    EepromImageClass diskClass;

    if (validRecords != NULL) *validRecords = 0;
    if (file == NULL) return EEPROM_IMAGE_INVALID;
    got = fread(ramImage, 1, EEPROM_SIZE, file);
    trailing = got == EEPROM_SIZE ? fgetc(file) : EOF;
    readOk = !ferror(file);
    closeOk = fclose(file) == 0;
    if (got != EEPROM_SIZE || trailing != EOF || !readOk || !closeOk) {
        return EEPROM_IMAGE_INVALID;
    }

    rawClass = ClassifyRamEepromImage(ramImage, validRecords);
    if (rawClass == EEPROM_IMAGE_ACTIVE_REGION || rawClass == EEPROM_IMAGE_OTHER_REGION) {
        if (legacyRamOrder != NULL) *legacyRamOrder = 1;
        return rawClass;
    }
    if (rawClass == EEPROM_IMAGE_BLANK) {
        if (legacyRamOrder != NULL) *legacyRamOrder = 0;
        return rawClass;
    }

    ReverseEepromBlocks(ramImage);
    diskClass = ClassifyRamEepromImage(ramImage, validRecords);
    if (diskClass != EEPROM_IMAGE_INVALID) {
        if (legacyRamOrder != NULL) *legacyRamOrder = 0;
        return diskClass;
    }

    return EEPROM_IMAGE_INVALID;
}

static int FilesMatch(const char* leftPath, const char* rightPath) {
    FILE* left = fopen(leftPath, "rb");
    FILE* right = fopen(rightPath, "rb");
    u8 leftBytes[256];
    u8 rightBytes[256];
    int ok = left != NULL && right != NULL;

    while (ok) {
        size_t leftCount = fread(leftBytes, 1, sizeof(leftBytes), left);
        size_t rightCount = fread(rightBytes, 1, sizeof(rightBytes), right);
        if (leftCount != rightCount || memcmp(leftBytes, rightBytes, leftCount) != 0) {
            ok = 0;
            break;
        }
        if (leftCount < sizeof(leftBytes)) {
            if (ferror(left) || ferror(right)) ok = 0;
            break;
        }
    }
    if (left != NULL && fclose(left) != 0) ok = 0;
    if (right != NULL && fclose(right) != 0) ok = 0;
    return ok;
}

static int BuildSidecarPathForSave(const char* savePath, char* sidecar, size_t sidecarSize) {
    char* extension;
    int length;
    if (savePath == NULL || sidecar == NULL || sidecarSize == 0) return 0;
    length = snprintf(sidecar, sidecarSize, "%s", savePath);
    if (length < 0 || (size_t)length >= sidecarSize) return 0;
    extension = strrchr(sidecar, '.');
    if (extension != NULL) {
        length = snprintf(extension, sidecarSize - (size_t)(extension - sidecar), ".randomizer");
        return length >= 0 && (size_t)length < sidecarSize - (size_t)(extension - sidecar);
    }
    length = snprintf(sidecar + strlen(sidecar), sidecarSize - strlen(sidecar), ".randomizer");
    return length >= 0 && (size_t)length < sidecarSize - strlen(sidecar);
}

/* Copy without ever replacing a destination. The source remains authoritative
 * throughout; an interrupted/failed copy can only leave a new partial file. */
static int CopyFileDurableExclusive(const char* sourcePath, const char* destPath) {
    FILE* source;
    FILE* dest;
    u8 bytes[512];
    int ok = 1;

    if (GetPathState(sourcePath) != PATH_STATE_EXISTS || GetPathState(destPath) != PATH_STATE_MISSING) return 0;
    source = fopen(sourcePath, "rb");
    if (source == NULL) return 0;
    dest = fopen(destPath, "wbx");
    if (dest == NULL) {
        fclose(source);
        return 0;
    }
    while (ok) {
        const size_t count = fread(bytes, 1, sizeof(bytes), source);
        if (count != 0 && fwrite(bytes, 1, count, dest) != count) ok = 0;
        if (count < sizeof(bytes)) {
            if (ferror(source)) ok = 0;
            break;
        }
    }
    if (ok && fflush(dest) != 0) ok = 0;
#ifdef _WIN32
    if (ok && _commit(_fileno(dest)) != 0) ok = 0;
#else
    if (ok && fsync(fileno(dest)) != 0) ok = 0;
#endif
    if (fclose(source) != 0) ok = 0;
    if (fclose(dest) != 0) ok = 0;
    if (ok) ok = FilesMatch(sourcePath, destPath);
    if (!ok) {
        /* destPath was created exclusively by this call; source is untouched. */
        if (remove(destPath) != 0 && errno != ENOENT) {
            fprintf(stderr, "[SAVE] Incomplete exclusive copy retained at %s.\n", destPath);
        }
    }
    return ok;
}

static int BuildUniqueTransactionPath(const char* basePath, const char* tag, char* transactionPath,
                                      size_t transactionPathSize, int requireSaveAuxPaths) {
    unsigned sequence;

    if (basePath == NULL || tag == NULL || transactionPath == NULL || transactionPathSize == 0) return 0;
    for (sequence = 0; sequence < 1000; ++sequence) {
        char temp[SAVE_AUX_PATH_MAX];
        char rollback[SAVE_AUX_PATH_MAX];
        const int length = snprintf(transactionPath, transactionPathSize, "%s.%s.%03u", basePath, tag, sequence);
        PathState transactionState;
        if (length < 0 || (size_t)length >= transactionPathSize) return 0;
        transactionState = GetPathState(transactionPath);
        if (transactionState == PATH_STATE_ERROR) return 0;
        if (transactionState != PATH_STATE_MISSING) continue;
        if (!requireSaveAuxPaths) return 1;
        if ((size_t)snprintf(temp, sizeof(temp), "%s.tmp", transactionPath) >= sizeof(temp) ||
            (size_t)snprintf(rollback, sizeof(rollback), "%s.rollback", transactionPath) >= sizeof(rollback)) {
            return 0;
        }
        if (GetPathState(temp) == PATH_STATE_ERROR || GetPathState(rollback) == PATH_STATE_ERROR) return 0;
        if (GetPathState(temp) == PATH_STATE_MISSING && GetPathState(rollback) == PATH_STATE_MISSING) return 1;
    }
    return 0;
}

static void RemoveCreatedFileOrLog(const char* path, const char* operation) {
    if (path != NULL && path[0] != '\0' && remove(path) != 0 && errno != ENOENT) {
        fprintf(stderr, "[SAVE] %s rollback could not remove %s; the original profile remains intact.\n",
                operation, path);
    }
}

/* Durable, never-overwriting preservation used before any automatic format or
 * layout migration. A failed backup is a hard stop: the source remains intact. */
static int PreserveFileUnique(const char* path, const char* tag) {
    char backup[SAVE_AUX_PATH_MAX];
    FILE* source;
    FILE* dest = NULL;
    u8 bytes[512];
    unsigned sequence;
    int ok = 1;

#ifdef PORT_SAVE_TEST
    if (sTestFailNextPreserve) {
        sTestFailNextPreserve = 0;
        errno = EIO;
        return 0;
    }
#endif
    if (!FileHasExactSize(path)) return 0;
    for (sequence = 0; sequence < 1000; ++sequence) {
        int length = sequence == 0 ? snprintf(backup, sizeof(backup), "%s.%s.bak", path, tag)
                                   : snprintf(backup, sizeof(backup), "%s.%s.%03u.bak", path, tag, sequence);
        if (length < 0 || (size_t)length >= sizeof(backup)) return 0;
        const PathState backupState = GetPathState(backup);
        if (backupState == PATH_STATE_ERROR) return 0;
        if (backupState == PATH_STATE_MISSING) break;
    }
    if (sequence == 1000) return 0;

    source = fopen(path, "rb");
    if (source == NULL) return 0;
    dest = fopen(backup, "wb");
    if (dest == NULL) {
        fclose(source);
        return 0;
    }
    while (ok) {
        size_t count = fread(bytes, 1, sizeof(bytes), source);
        if (count != 0 && fwrite(bytes, 1, count, dest) != count) ok = 0;
        if (count < sizeof(bytes)) {
            if (ferror(source)) ok = 0;
            break;
        }
    }
    if (ok && fflush(dest) != 0) ok = 0;
#ifdef _WIN32
    if (ok && _commit(_fileno(dest)) != 0) ok = 0;
#else
    if (ok && fsync(fileno(dest)) != 0) ok = 0;
#endif
    if (fclose(source) != 0) ok = 0;
    if (fclose(dest) != 0) ok = 0;
    if (ok) ok = FilesMatch(path, backup);
    if (!ok) {
        remove(backup);
        return 0;
    }
    fprintf(stderr, "[SAVE] Preserved %s before migration: %s\n", path, backup);
    return 1;
}

static int MoveFileUniqueWithPath(const char* path, const char* tag, char* movedPath, size_t movedPathSize) {
    char preserved[SAVE_AUX_PATH_MAX];
    unsigned sequence;

    {
        const PathState sourceState = GetPathState(path);
        if (sourceState == PATH_STATE_MISSING) return 1;
        if (sourceState == PATH_STATE_ERROR) return 0;
    }
    for (sequence = 0; sequence < 1000; ++sequence) {
        int length = sequence == 0 ? snprintf(preserved, sizeof(preserved), "%s.%s.bak", path, tag)
                                   : snprintf(preserved, sizeof(preserved), "%s.%s.%03u.bak", path, tag, sequence);
        if (length < 0 || (size_t)length >= sizeof(preserved)) return 0;
        const PathState preservedState = GetPathState(preserved);
        if (preservedState == PATH_STATE_ERROR) return 0;
        if (preservedState == PATH_STATE_MISSING) break;
    }
    if (sequence == 1000 || rename(path, preserved) != 0) return 0;
    if (movedPath != NULL && movedPathSize != 0) {
        snprintf(movedPath, movedPathSize, "%s", preserved);
    }
    fprintf(stderr, "[SAVE] Preserved interrupted-write file as %s\n", preserved);
    return 1;
}

static int MoveFileUnique(const char* path, const char* tag) {
    return MoveFileUniqueWithPath(path, tag, NULL, 0);
}

static int FileMatchesDiskImage(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return 0;

    u8 scratch[256];
    size_t offset = 0;
    int ok = 1;
    while (offset < EEPROM_SIZE) {
        const size_t wanted = EEPROM_SIZE - offset < sizeof(scratch) ? EEPROM_SIZE - offset : sizeof(scratch);
        if (fread(scratch, 1, wanted, file) != wanted || memcmp(scratch, sDiskImage + offset, wanted) != 0) {
            ok = 0;
            break;
        }
        offset += wanted;
    }
    if (ok && fgetc(file) != EOF) ok = 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static void RecordSaveFailure(PortSaveStage stage, int errorCode) {
    sSaveStats.lastStage = stage;
    sSaveStats.lastErrno = errorCode != 0 ? errorCode : EIO;
    ++sSaveStats.flushFailures;
}

static void MakeAuxiliaryPaths(const char* path, char* temp, char* rollback) {
    snprintf(temp, SAVE_AUX_PATH_MAX, "%s.tmp", path);
    snprintf(rollback, SAVE_AUX_PATH_MAX, "%s.rollback", path);
}

#ifdef TMC_3DS
typedef enum RecoveryResult {
    RECOVERY_NONE = 0,
    RECOVERY_OK,
    RECOVERY_BLOCKED,
} RecoveryResult;

#ifdef PORT_SAVE_TEST
static int sTestFailRecoveryInstall;
static int sTestFailProfileCommit;
void Port_Save_TestFailNextRecoveryInstall(void) {
    sTestFailRecoveryInstall = 1;
}
void Port_Save_TestFailNextProfileCommit(void) {
    sTestFailProfileCommit = 1;
}

static int TestShouldFailProfileCommit(void) {
    if (!sTestFailProfileCommit) return 0;
    sTestFailProfileCommit = 0;
    errno = EIO;
    return 1;
}
#else
static int TestShouldFailProfileCommit(void) {
    return 0;
}
#endif

static RecoveryResult RecoverInterruptedAtomicWrite(const char* path) {
    char temp[SAVE_AUX_PATH_MAX];
    char rollback[SAVE_AUX_PATH_MAX];
    char preservedCurrent[SAVE_AUX_PATH_MAX] = { 0 };
    u8 imageScratch[EEPROM_SIZE];
    EepromImageClass currentClass;
    EepromImageClass rollbackClass;
    EepromImageClass tempClass;
    unsigned currentQuality = 0;
    unsigned rollbackQuality = 0;
    unsigned tempQuality = 0;
    PathState currentState;
    PathState rollbackState;
    PathState tempState;
    MakeAuxiliaryPaths(path, temp, rollback);
    currentState = GetPathState(path);
    rollbackState = GetPathState(rollback);
    tempState = GetPathState(temp);

    if (currentState == PATH_STATE_ERROR) return RECOVERY_BLOCKED;
    currentClass = ReadAndClassifyEepromFile(path, imageScratch, NULL, &currentQuality);
    /* A full active-region current file always wins. Stale candidates are
     * retained rather than deleted because the format has no generation. */
    if (currentClass == EEPROM_IMAGE_ACTIVE_REGION && currentQuality == 5) {
        return RECOVERY_OK;
    }

    if (rollbackState == PATH_STATE_ERROR || tempState == PATH_STATE_ERROR) {
        return RECOVERY_BLOCKED;
    }
    /* A blank candidate cannot safely replace a malformed current file: it
     * contains no progress and has no generation metadata proving recency. */
    rollbackClass = ReadAndClassifyEepromFile(rollback, imageScratch, NULL, &rollbackQuality);
    tempClass = ReadAndClassifyEepromFile(temp, imageScratch, NULL, &tempQuality);
    const int rollbackValid = rollbackClass == EEPROM_IMAGE_ACTIVE_REGION;
    const int tempValid = tempClass == EEPROM_IMAGE_ACTIVE_REGION;
    const char* candidate;
    if (currentClass == EEPROM_IMAGE_ACTIVE_REGION) {
        if (!rollbackValid && !tempValid) return RECOVERY_OK;
        fprintf(stderr,
                "[SAVE] Recovery for %s has a partially valid current image and another save candidate; "
                "preserving every file instead of guessing.\n",
                path);
        return RECOVERY_BLOCKED;
    }
    if (currentClass == EEPROM_IMAGE_BLANK && (rollbackValid || tempValid)) {
        fprintf(stderr,
                "[SAVE] Recovery for %s has a blank current image and saved progress in a candidate; preserving "
                "every file for explicit recovery.\n",
                path);
        return RECOVERY_BLOCKED;
    }
    if (currentClass == EEPROM_IMAGE_OTHER_REGION && (rollbackValid || tempValid)) {
        fprintf(stderr,
                "[SAVE] Recovery for %s would displace another region; preserving every file for explicit "
                "recovery.\n",
                path);
        return RECOVERY_BLOCKED;
    }
    if (rollbackValid + tempValid == 2 && currentState == PATH_STATE_MISSING && tempQuality >= rollbackQuality) {
        /* Exact crash point between path->rollback and tmp->path: the fsynced
         * temp is the new image and rollback is the old image. Install tmp,
         * but deliberately retain rollback because neither has generation
         * metadata beyond those protocol roles. */
        candidate = temp;
    } else if (rollbackValid + tempValid != 1) {
        if (rollbackValid || tempValid) {
            fprintf(stderr, "[SAVE] Recovery for %s is ambiguous; preserving every candidate.\n", path);
        }
        /* A complete blank current file is a usable empty profile only when
         * no active recovery candidate exists. */
        if (!rollbackValid && !tempValid && currentClass == EEPROM_IMAGE_BLANK) return RECOVERY_OK;
        return (rollbackValid || tempValid) ? RECOVERY_BLOCKED : RECOVERY_NONE;
    } else {
        candidate = rollbackValid ? rollback : temp;
    }

    sSaveStats.lastStage = PORT_SAVE_STAGE_RECOVER;
    if (currentState == PATH_STATE_EXISTS &&
        !MoveFileUniqueWithPath(path, "interrupted", preservedCurrent, sizeof(preservedCurrent))) {
        sSaveStats.lastErrno = errno != 0 ? errno : EIO;
        return RECOVERY_BLOCKED;
    }
    {
        int installResult;
#ifdef PORT_SAVE_TEST
        if (sTestFailRecoveryInstall) {
            sTestFailRecoveryInstall = 0;
            errno = EIO;
            installResult = -1;
        } else
#endif
        {
            installResult = rename(candidate, path);
        }
        if (installResult == 0) {
            ++sSaveStats.interruptedRecoveries;
            sSaveStats.lastErrno = 0;
            return RECOVERY_OK;
        } else {
            const int installError = errno != 0 ? errno : EIO;
            if (preservedCurrent[0] != '\0') {
                if (rename(preservedCurrent, path) != 0) {
                    fprintf(stderr,
                            "[SAVE] ERROR: recovery install and restoration both failed for %s; writes stay "
                            "disabled.\n",
                            path);
                }
            }
            sSaveStats.lastErrno = installError;
            return RECOVERY_BLOCKED;
        }
    }
}
#endif

#ifndef TMC_3DS
static int TestShouldFailProfileCommit(void) {
    return 0;
}
#endif

/* Write the in-memory EEPROM to `path` atomically: serialize to a sibling
 * temp file, flush it through to disk, then rename over the target. A crash
 * or power loss leaves either the old complete file or the new complete
 * file — never the truncated one that plain "wb" + fwrite produced (which
 * the next load treated as a blank save, silently wiping progress).
 * Returns 1 on success. */
static int WriteEepromAtomic(const char* path) {
    char tmp[SAVE_AUX_PATH_MAX];
    char rollback[SAVE_AUX_PATH_MAX];
    PathState pathState;
    PathState tmpState;
    MakeAuxiliaryPaths(path, tmp, rollback);
    ++sSaveStats.flushAttempts;
#ifdef PORT_SAVE_TEST
    if (sTestFailNextAtomicWrite) {
        sTestFailNextAtomicWrite = 0;
        RecordSaveFailure(PORT_SAVE_STAGE_OPEN_TEMP, EIO);
        return 0;
    }
#endif
    BuildEepromDiskImage();

    if (strlen(path) + sizeof(".rollback") > sizeof(rollback)) {
        RecordSaveFailure(PORT_SAVE_STAGE_OPEN_TEMP, ENAMETOOLONG);
        return 0;
    }

    pathState = GetPathState(path);
    tmpState = GetPathState(tmp);
    if (pathState == PATH_STATE_ERROR || tmpState == PATH_STATE_ERROR) {
        RecordSaveFailure(PORT_SAVE_STAGE_OPEN_TEMP, errno);
        return 0;
    }

    /* Never truncate a candidate left by an interrupted write. It has no
     * trustworthy generation number, so preserve it under a unique name. */
    if (tmpState == PATH_STATE_EXISTS && !MoveFileUnique(tmp, "stale")) {
        RecordSaveFailure(PORT_SAVE_STAGE_OPEN_TEMP, errno);
        return 0;
    }

    sSaveStats.lastStage = PORT_SAVE_STAGE_OPEN_TEMP;
    FILE* f = fopen(tmp, "wb");
    if (!f) {
        RecordSaveFailure(PORT_SAVE_STAGE_OPEN_TEMP, errno);
        return 0;
    }

    sSaveStats.lastStage = PORT_SAVE_STAGE_WRITE_TEMP;
    int ok = fwrite(sDiskImage, 1, EEPROM_SIZE, f) == EEPROM_SIZE;
    int failureErrno = ok ? 0 : errno;
    PortSaveStage failureStage = PORT_SAVE_STAGE_WRITE_TEMP;
    if (ok) {
        sSaveStats.lastStage = PORT_SAVE_STAGE_FLUSH_TEMP;
        ok = fflush(f) == 0;
        if (!ok) {
            failureErrno = errno;
            failureStage = PORT_SAVE_STAGE_FLUSH_TEMP;
        }
#ifdef _WIN32
        if (ok) {
            sSaveStats.lastStage = PORT_SAVE_STAGE_SYNC_TEMP;
            ok = _commit(_fileno(f)) == 0;
        }
#else
        if (ok) {
            sSaveStats.lastStage = PORT_SAVE_STAGE_SYNC_TEMP;
            ok = fsync(fileno(f)) == 0;
        }
#endif
        if (!ok && failureStage == PORT_SAVE_STAGE_WRITE_TEMP) {
            failureErrno = errno;
            failureStage = PORT_SAVE_STAGE_SYNC_TEMP;
        }
    }
    sSaveStats.lastStage = PORT_SAVE_STAGE_CLOSE_TEMP;
    if (fclose(f) != 0) {
        if (ok) {
            failureErrno = errno;
            failureStage = PORT_SAVE_STAGE_CLOSE_TEMP;
        }
        ok = 0;
    }
    if (!ok) {
        remove(tmp);
        RecordSaveFailure(failureStage, failureErrno);
        return 0;
    }

    sSaveStats.lastStage = PORT_SAVE_STAGE_VERIFY_TEMP;
    if (!FileMatchesDiskImage(tmp)) {
        const int verifyErrno = errno;
        remove(tmp);
        RecordSaveFailure(PORT_SAVE_STAGE_VERIFY_TEMP, verifyErrno);
        return 0;
    }

#ifdef _WIN32
    sSaveStats.lastStage = PORT_SAVE_STAGE_INSTALL_TEMP;
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        remove(tmp);
        RecordSaveFailure(PORT_SAVE_STAGE_INSTALL_TEMP, (int)GetLastError());
        return 0;
    }
#elif defined(TMC_3DS)
    int movedCurrent = 0;
    if (pathState == PATH_STATE_EXISTS) {
        const PathState rollbackState = GetPathState(rollback);
        if (rollbackState == PATH_STATE_ERROR) {
            remove(tmp);
            RecordSaveFailure(PORT_SAVE_STAGE_BACKUP_CURRENT, errno);
            return 0;
        }
        if (rollbackState == PATH_STATE_EXISTS && !MoveFileUnique(rollback, "stale")) {
            remove(tmp);
            RecordSaveFailure(PORT_SAVE_STAGE_BACKUP_CURRENT, errno);
            return 0;
        }
        sSaveStats.lastStage = PORT_SAVE_STAGE_BACKUP_CURRENT;
        if (rename(path, rollback) != 0) {
            const int backupErrno = errno;
            remove(tmp);
            RecordSaveFailure(PORT_SAVE_STAGE_BACKUP_CURRENT, backupErrno);
            return 0;
        }
        movedCurrent = 1;
    }

    sSaveStats.lastStage = PORT_SAVE_STAGE_INSTALL_TEMP;
    if (rename(tmp, path) != 0) {
        const int installErrno = errno;
        if (movedCurrent) {
            sSaveStats.lastStage = PORT_SAVE_STAGE_RESTORE_BACKUP;
            if (rename(rollback, path) == 0) {
                ++sSaveStats.rollbackRestores;
            } else {
                ++sSaveStats.rollbackFailures;
            }
        }
        remove(tmp);
        RecordSaveFailure(PORT_SAVE_STAGE_INSTALL_TEMP, installErrno);
        return 0;
    }

    sSaveStats.lastStage = PORT_SAVE_STAGE_VERIFY_INSTALLED;
    if (!FileMatchesDiskImage(path)) {
        const int verifyErrno = errno;
        if (!MoveFileUnique(path, "failed-install")) {
            RecordSaveFailure(PORT_SAVE_STAGE_VERIFY_INSTALLED, verifyErrno);
            return 0;
        }
        if (movedCurrent) {
            sSaveStats.lastStage = PORT_SAVE_STAGE_RESTORE_BACKUP;
            if (rename(rollback, path) == 0) {
                ++sSaveStats.rollbackRestores;
            } else {
                ++sSaveStats.rollbackFailures;
            }
        }
        RecordSaveFailure(PORT_SAVE_STAGE_VERIFY_INSTALLED, verifyErrno);
        return 0;
    }
    if (movedCurrent) remove(rollback);
#else
    sSaveStats.lastStage = PORT_SAVE_STAGE_INSTALL_TEMP;
    if (rename(tmp, path) != 0) {
        const int installErrno = errno;
        remove(tmp);
        RecordSaveFailure(PORT_SAVE_STAGE_INSTALL_TEMP, installErrno);
        return 0;
    }
#endif

    sSaveStats.lastStage = PORT_SAVE_STAGE_COMPLETE;
    sSaveStats.lastErrno = 0;
    ++sSaveStats.flushSuccesses;
    return 1;
}

static void FlushEepromFile(void);
static int IsManagedProfilePath(const char* path);

/* ---- Persistence -------------------------------------------------------- */

#ifdef MULTI_REGION
/*
 * Region-isolated saves (M5). The fat binary plays USA/EU/JP from one executable;
 * each region's in-game save signature differs ("ZELDA 5" USA vs "ZELDA 3" EU/JP),
 * so a shared save file makes InitSaveData wipe the other region's data on load.
 * Give each region its own default store so switching ROMs never wipes or corrupts:
 *   USA -> tmc.sav (unchanged, back-compat with existing installs)
 *   EU  -> tmc_eu.sav
 *   JP  -> tmc_jp.sav
 * An explicit named profile (config.json) is region-agnostic and left untouched.
 * Resolved lazily here because the active region is only known after Port_LoadRom,
 * which runs after the startup Port_Save_SetActivePath() call.
 */
static void ResolveRegionDefaultPath(void) {
    const char* name;
    extern bool Port_Config_GetRandoEnabled(void);
    if (sExplicitProfile) {
        return;
    }
    if (REGION_IS_EU) {
        name = Port_Config_GetRandoEnabled() ? "tmc_eu_rando.sav" : "tmc_eu.sav";
    } else if (REGION_IS_JP) {
        name = Port_Config_GetRandoEnabled() ? "tmc_jp_rando.sav" : "tmc_jp.sav";
    } else {
        name = Port_Config_GetRandoEnabled() ? "tmc_rando.sav" : DEFAULT_SAVE_FILENAME;
    }
    snprintf(sActivePath, sizeof(sActivePath), "%s", name);
}
#endif

static void LoadEepromFile(void) {
    EepromImageClass imageClass;
    int legacyRamOrder = 0;
    FILE* probe;
#ifdef MULTI_REGION
    ResolveRegionDefaultPath();
#endif
#ifdef TMC_3DS
    if (RecoverInterruptedAtomicWrite(sActivePath) == RECOVERY_BLOCKED) {
        memset(sEeprom, 0xFF, EEPROM_SIZE);
        sEepromWriteBlocked = 1;
        fprintf(stderr,
                "[SAVE] ERROR: interrupted-write recovery for %s is unresolved; writes are disabled and every "
                "candidate is retained.\n",
                sActivePath);
        return;
    }
#endif
    sEepromWriteBlocked = 0;
    probe = fopen(sActivePath, "rb");
    if (!probe) {
        const int openError = errno;
        memset(sEeprom, 0xFF, EEPROM_SIZE); /* blank EEPROM = 0xFF */
        if (openError == ENOENT) {
            fprintf(stderr, "[SAVE] No save file at %s, starting fresh.\n", sActivePath);
        } else {
            sEepromWriteBlocked = 1;
            fprintf(stderr, "[SAVE] ERROR: cannot read %s; writes are disabled and the file is untouched.\n",
                    sActivePath);
        }
        return;
    }
    fclose(probe);

    imageClass = ReadAndClassifyEepromFile(sActivePath, sEeprom, &legacyRamOrder, NULL);
    if (imageClass == EEPROM_IMAGE_INVALID) {
        memset(sEeprom, 0xFF, EEPROM_SIZE);
        sEepromWriteBlocked = 1;
        fprintf(stderr,
                "[SAVE] ERROR: %s is not one exact, recognized 8 KiB EEPROM image; writes are disabled and the "
                "file is untouched.\n",
                sActivePath);
        return;
    }
    if (imageClass == EEPROM_IMAGE_OTHER_REGION) {
        memset(sEeprom, 0xFF, EEPROM_SIZE);
        sEepromWriteBlocked = 1;
        fprintf(stderr,
                "[SAVE] ERROR: %s belongs to another ROM region; writes are disabled so InitSaveData cannot erase "
                "it.\n",
                sActivePath);
        return;
    }
    if (imageClass == EEPROM_IMAGE_BLANK) {
        fprintf(stderr, "[SAVE] Loaded blank EEPROM image: %s\n", sActivePath);
        return;
    }
    if (legacyRamOrder) {
        /* Legacy port-format file (game-RAM order on disk). The buffer
         * is already in the order we keep in memory. Conversion is allowed
         * only after a durable, never-overwritten copy of the original. */
        if (!PreserveFileUnique(sActivePath, "pre-byte-order")) {
            sEepromWriteBlocked = 1;
            fprintf(stderr,
                    "[SAVE] ERROR: backup before byte-order migration failed; %s remains untouched and writes are "
                    "disabled.\n",
                    sActivePath);
            return;
        }
        fprintf(stderr, "[SAVE] Migrating %s to mGBA byte order.\n", sActivePath);
        sEepromDirty = 1;
        FlushEepromFile();
    } else {
        /* ReadAndClassifyEepromFile already converted mGBA/VBA-M order to
         * game-RAM order after validating the active-region signature. */
        fprintf(stderr, "[SAVE] Loaded save file: %s\n", sActivePath);
    }
}

static void FlushEepromFile(void) {
    if (!sEepromDirty)
        return;
    if (WriteEepromAtomic(sActivePath)) {
        sEepromDirty = 0;
        if (sFlushFailedLast) {
            fprintf(stderr, "[SAVE] atomic write of %s recovered.\n", sActivePath);
            sFlushFailedLast = 0;
        }
    } else {
        /* Keep the dirty flag so the next flush retries; log once per
         * failure burst, not once per block write. */
        if (!sFlushFailedLast) {
            fprintf(stderr, "[SAVE] ERROR: atomic write of %s failed; will retry.\n", sActivePath);
            sFlushFailedLast = 1;
        }
    }
}

/* ---- Save transactions (#19) --------------------------------------------
 * A single in-game save is ~324 back-to-back EEPROMWrite0_8k_Check calls
 * (two full SaveFile copies + status blocks). Flushing the 8 KB file
 * atomically (temp+fsync+rename) per BLOCK meant ~324 fsync'd rewrites on
 * the game thread — a 150 ms..multi-second hitch. The game's save entry
 * points (src/save.c DataDoubleWriteWithStatus) bracket the burst so the
 * whole transaction flushes exactly once at the end. */
void Port_Save_BeginTransaction(void) {
    sSaveTxnDepth++;
}

/* Returns 1 when everything the transaction wrote is durably on disk. */
int Port_Save_EndTransaction(void) {
    if (sSaveTxnDepth > 0)
        sSaveTxnDepth--;
    if (sSaveTxnDepth == 0)
        FlushEepromFile();
    return !sEepromWriteBlocked && !sEepromDirty;
}

int Port_Save_PreserveBeforeMigration(void) {
    if (!sEepromInited || sEepromWriteBlocked) return 0;
    return PreserveFileUnique(sActivePath, "pre-migration");
}

int Port_Save_PreserveBeforeFuserRepair(void) {
    if (!sEepromInited || sEepromWriteBlocked) return 0;
    if (strcmp(sFuserRepairPreservedPath, sActivePath) == 0) return 1;
    /* A repair can be requested while EEPROM writes are still pending after
     * an I/O failure. Back up the latest in-memory raw image, never the older
     * file which merely happened to be durable before that failure. Never
     * flush through the middle of a game save transaction. */
    if (sSaveTxnDepth != 0) return 0;
    if (sEepromDirty) {
        FlushEepromFile();
        if (sEepromDirty) return 0;
    }
    if (!PreserveFileUnique(sActivePath, "pre-fuser-repair")) return 0;
    snprintf(sFuserRepairPreservedPath, sizeof(sFuserRepairPreservedPath), "%s", sActivePath);
    return 1;
}

void Port_Save_GetStats(PortSaveStats* stats) {
    if (!stats) return;
    *stats = sSaveStats;
    stats->transactionDepth = sSaveTxnDepth;
    stats->initialized = sEepromInited != 0;
    stats->dirty = sEepromDirty != 0;
    snprintf(stats->activePath, sizeof(stats->activePath), "%s", sActivePath);
}

const char* Port_Save_StageName(PortSaveStage stage) {
    switch (stage) {
        case PORT_SAVE_STAGE_IDLE: return "idle";
        case PORT_SAVE_STAGE_RECOVER: return "recover";
        case PORT_SAVE_STAGE_OPEN_TEMP: return "open temp";
        case PORT_SAVE_STAGE_WRITE_TEMP: return "write temp";
        case PORT_SAVE_STAGE_FLUSH_TEMP: return "flush temp";
        case PORT_SAVE_STAGE_SYNC_TEMP: return "sync temp";
        case PORT_SAVE_STAGE_CLOSE_TEMP: return "close temp";
        case PORT_SAVE_STAGE_VERIFY_TEMP: return "verify temp";
        case PORT_SAVE_STAGE_BACKUP_CURRENT: return "backup current";
        case PORT_SAVE_STAGE_INSTALL_TEMP: return "install temp";
        case PORT_SAVE_STAGE_VERIFY_INSTALLED: return "verify installed";
        case PORT_SAVE_STAGE_RESTORE_BACKUP: return "restore backup";
        case PORT_SAVE_STAGE_COMPLETE: return "complete";
        default: return "unknown";
    }
}

/* ---- EEPROM BIOS API ---------------------------------------------------- */

u16 EEPROMConfigure(u16 type) {
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    /* type = 0x40 → 8 KB, type = 4 → 512 B. We always emulate 8 KB. */
    (void)type;
    return 0; /* success */
}

u16 EEPROMRead(u16 block, u16* dest) {
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    memcpy(dest, &sEeprom[block * EEPROM_BLOCK], EEPROM_BLOCK);
    return 0; /* success */
}

u16 EEPROMWrite0_8k_Check(u16 block, const u16* src) {
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */
    if (sEepromWriteBlocked)
        return 0x8000; /* preserve malformed/wrong-region backing file */
#ifdef PORT_SAVE_TEST
    if (sTestFailEepromBlockArmed && block == sTestFailEepromBlock) {
        sTestFailEepromBlockArmed = 0;
        return 0x8000;
    }
#endif

    memcpy(&sEeprom[block * EEPROM_BLOCK], src, EEPROM_BLOCK);
    sEepromDirty = 1;

    /* Flush immediately (crash safety) unless a save transaction is open —
     * then the single flush happens at Port_Save_EndTransaction(). */
    if (sSaveTxnDepth == 0)
        FlushEepromFile();
    /* The emulated EEPROM block write itself succeeded. Top-level save
     * transactions use Port_Save_EndTransaction() for host-file durability;
     * returning a host flush error here would make retail DataWrite replace
     * this valid RAM block with its "DAMEDAME" failure sentinel. */
    return 0;
}

u16 EEPROMCompare(u16 block, const u16* src) {
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    if (memcmp(&sEeprom[block * EEPROM_BLOCK], src, EEPROM_BLOCK) != 0)
        return 0x8000; /* EEPROM_COMPARE_FAILED */

    return 0; /* match */
}

/* ---- Profile management ------------------------------------------------- */

/* Public: invoked by port_main.c once at startup to honour the persisted
 * choice from config.json. Quietly no-ops on a missing/null path so the
 * default tmc.sav stays in effect. */
int Port_Save_SetActivePath(const char* path) {
    if (path == NULL || path[0] == '\0') {
        path = DEFAULT_SAVE_FILENAME;
    } else if (!IsManagedProfilePath(path)) {
        /* The active-profile name comes from config.json (user-editable);
         * refuse anything outside the tmc.sav / tmc_<name>.sav lane so a
         * crafted value can't redirect saves elsewhere on disk. */
        fprintf(stderr, "[SAVE] Ignoring unmanaged save profile '%s'; active profile is unchanged.\n", path);
        return 0;
    }
    if (strcmp(path, sActivePath) == 0) return 1;
    /* A transaction belongs entirely to one backing file. Even if it has not
     * dirtied EEPROM yet, switching here would let its eventual End call flush
     * a different profile. */
    if (sSaveTxnDepth != 0) {
        fprintf(stderr, "[SAVE] Refusing profile switch during an active save transaction.\n");
        return 0;
    }
    /* If the EEPROM was already loaded under the old path, flush it
     * first so the user doesn't lose pending writes when switching. */
    if (sEepromInited && sEepromDirty) {
        FlushEepromFile();
        if (sEepromDirty) {
            fprintf(stderr,
                    "[SAVE] Refusing profile switch while pending writes for %s are not durable.\n",
                    sActivePath);
            return 0;
        }
    }
    strncpy(sActivePath, path, sizeof(sActivePath) - 1);
    sActivePath[sizeof(sActivePath) - 1] = '\0';
    /* A named profile (anything other than the default tmc.sav) is the user's
     * explicit, region-agnostic choice; the multi-region per-region default
     * (ResolveRegionDefaultPath) must not override it. */
    sExplicitProfile = (strcmp(path, DEFAULT_SAVE_FILENAME) != 0);
    /* Force a reload on next access so any read after this point hits
     * the new file. A profile may have been replaced while it was inactive,
     * so its previous fuser-repair preservation cannot be reused after a
     * switch away and back. */
    sEepromInited = 0;
    sEepromDirty = 0;
    sEepromWriteBlocked = 0;
    sFuserRepairPreservedPath[0] = '\0';
    return 1;
}

const char* Port_Save_GetActivePath(void) {
    return sActivePath;
}

/* Destructive mode-switch helper used only after an explicit confirmation.
 * Clear the complete active profile and every derivative that could carry
 * normal/randomized state across the boundary. ROM files are never touched. */
int Port_Save_ClearActiveProfileData(void) {
    extern int Port_QuickSave_ClearAll(void);
#ifdef MULTI_REGION
    ResolveRegionDefaultPath();
#endif
    char temp[SAVE_AUX_PATH_MAX];
    char rollback[SAVE_AUX_PATH_MAX];
    char backup[SAVE_AUX_PATH_MAX];
    char sidecar[SAVE_AUX_PATH_MAX];
    char sidecarTemp[SAVE_AUX_PATH_MAX + sizeof(".tmp")];
    char sidecarBackup[SAVE_AUX_PATH_MAX + sizeof(".bak")];
    MakeAuxiliaryPaths(sActivePath, temp, rollback);
    snprintf(backup, sizeof(backup), "%s.bak", sActivePath);
    if (!BuildSidecarPathForSave(sActivePath, sidecar, sizeof(sidecar))) return 0;
    snprintf(sidecarTemp, sizeof(sidecarTemp), "%s.tmp", sidecar);
    snprintf(sidecarBackup, sizeof(sidecarBackup), "%s.bak", sidecar);

    const char* files[] = { sActivePath, temp, rollback, backup, sidecar, sidecarTemp, sidecarBackup };
    int ok = 1;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        if (remove(files[i]) != 0 && errno != ENOENT) ok = 0;
    }

    if (!Port_QuickSave_ClearAll()) ok = 0;

    memset(sEeprom, 0xFF, sizeof(sEeprom));
    sEepromDirty = 0;
    sEepromInited = 0;
    sEepromWriteBlocked = 0;
    sSaveTxnDepth = 0;
    sFlushFailedLast = 0;
    sFuserRepairPreservedPath[0] = '\0';
#ifdef PORT_SAVE_TEST
    sTestFailNextPreserve = 0;
    sTestFailNextAtomicWrite = 0;
    sTestFailEepromBlockArmed = 0;
#endif
    return ok;
}

/* Snapshot the in-memory EEPROM into a named profile file without
 * changing the active profile. Useful for "Save current state as a new
 * profile" — keep playing in the current profile while the named copy
 * captures right-now state. Returns 0 on failure. */
int Port_Save_SaveAsProfile(const char* path) {
    char sourceSidecar[SAVE_AUX_PATH_MAX];
    char destinationSidecar[SAVE_AUX_PATH_MAX];
    char stagedSave[SAVE_AUX_PATH_MAX] = { 0 };
    char stagedSidecar[SAVE_AUX_PATH_MAX] = { 0 };
    PathState sourceSidecarState;
    int sidecarInstalled = 0;

    if (path == NULL || path[0] == '\0') return 0;
    /* Only allow writing into the managed profile lane so the "save as"
     * UI can't be pointed at an arbitrary host path. */
    if (!IsManagedProfilePath(path) || strcmp(path, sActivePath) == 0) return 0;
    if (sSaveTxnDepth != 0) return 0;
    if (!BuildSidecarPathForSave(sActivePath, sourceSidecar, sizeof(sourceSidecar)) ||
        !BuildSidecarPathForSave(path, destinationSidecar, sizeof(destinationSidecar))) {
        return 0;
    }
    sourceSidecarState = GetPathState(sourceSidecar);
    if (sourceSidecarState == PATH_STATE_ERROR || GetPathState(path) != PATH_STATE_MISSING ||
        GetPathState(destinationSidecar) != PATH_STATE_MISSING) {
        return 0;
    }
    /* Ensure EEPROM was loaded at least once so we have meaningful data
     * to copy. (Right after launch, before any read, sEeprom is zeroed.) */
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (sEepromWriteBlocked) return 0;

    /* Stage both members before either destination becomes visible. The
     * source sidecar is copied (not moved), so every failure path retains the
     * original profile intact. */
    if (!BuildUniqueTransactionPath(path, "save-as-stage", stagedSave, sizeof(stagedSave), 1) ||
        !WriteEepromAtomic(stagedSave)) {
        RemoveCreatedFileOrLog(stagedSave, "Save As");
        return 0;
    }
    if (sourceSidecarState == PATH_STATE_EXISTS) {
        if (!BuildUniqueTransactionPath(destinationSidecar, "save-as-stage", stagedSidecar,
                                        sizeof(stagedSidecar), 0) ||
            !CopyFileDurableExclusive(sourceSidecar, stagedSidecar)) {
            RemoveCreatedFileOrLog(stagedSidecar, "Save As");
            RemoveCreatedFileOrLog(stagedSave, "Save As");
            return 0;
        }
        if (rename(stagedSidecar, destinationSidecar) != 0) {
            RemoveCreatedFileOrLog(stagedSidecar, "Save As");
            RemoveCreatedFileOrLog(stagedSave, "Save As");
            return 0;
        }
        sidecarInstalled = 1;
        stagedSidecar[0] = '\0';
    }

    if (TestShouldFailProfileCommit() || rename(stagedSave, path) != 0) {
        if (sidecarInstalled) RemoveCreatedFileOrLog(destinationSidecar, "Save As");
        RemoveCreatedFileOrLog(stagedSave, "Save As");
        return 0;
    }
    stagedSave[0] = '\0';
    if (!FileMatchesDiskImage(path)) {
        RemoveCreatedFileOrLog(path, "Save As verification");
        if (sidecarInstalled) RemoveCreatedFileOrLog(destinationSidecar, "Save As verification");
        return 0;
    }
    return 1;
}

/* List `tmc.sav` and `tmc_*.sav` files in cwd. Caller passes a fixed-
 * size [count][SAVE_FILENAME_MAX] char buffer; we fill up to `max` entries
 * and return the count written. Order is filesystem-defined. */
int Port_Save_ListProfiles(char out[][SAVE_FILENAME_MAX], int max) {
    int n = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("tmc*.sav", &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (n >= max)
            break;
        const char* name = fd.cFileName;
        const size_t len = strlen(name);
        if (len < SAVE_FILENAME_MAX &&
            (strcmp(name, DEFAULT_SAVE_FILENAME) == 0 || strncmp(name, "tmc_", 4) == 0)) {
            strncpy(out[n], name, SAVE_FILENAME_MAX - 1);
            out[n][SAVE_FILENAME_MAX - 1] = '\0';
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(".");
    if (!d)
        return 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (n >= max)
            break;
        const char* name = ent->d_name;
        const size_t len = strlen(name);
        if (len < 4 || len >= SAVE_FILENAME_MAX)
            continue;
        /* Match `tmc.sav` exactly OR `tmc_*.sav`. */
        if (strcmp(name, DEFAULT_SAVE_FILENAME) == 0 ||
            (strncmp(name, "tmc_", 4) == 0 && len > 8 && strcmp(name + len - 4, ".sav") == 0)) {
            strncpy(out[n], name, SAVE_FILENAME_MAX - 1);
            out[n][SAVE_FILENAME_MAX - 1] = '\0';
            n++;
        }
    }
    closedir(d);
#endif
    return n;
}

int Port_Save_FilenameMax(void) {
    return SAVE_FILENAME_MAX;
}

/* Returns 1 if the path is something we created and should be willing
 * to delete or rename — tmc.sav or tmc_<name>.sav. Anything else gets
 * refused so a stray ../../etc/passwd argument can't escape the
 * profile lane. */
static int IsManagedProfilePath(const char* path) {
    if (path == NULL || path[0] == '\0')
        return 0;
    const size_t len = strlen(path);
    if (len >= SAVE_FILENAME_MAX)
        return 0;
    if (strchr(path, '/') != NULL)
        return 0;
    if (strchr(path, '\\') != NULL)
        return 0;
    if (strstr(path, "..") != NULL)
        return 0;
    if (strcmp(path, DEFAULT_SAVE_FILENAME) == 0)
        return 1;
    if (strncmp(path, "tmc_", 4) != 0)
        return 0;
    if (len <= 8)
        return 0; /* "tmc_X.sav" minimum */
    if (strcmp(path + len - 4, ".sav") != 0)
        return 0;
    return 1;
}

/* Delete a profile file. Refuses if the profile is currently active
 * (caller should switch first) or if the name doesn't look like one
 * of ours. Returns 1 on success. */
int Port_Save_DeleteProfile(const char* path) {
    char sidecar[SAVE_AUX_PATH_MAX];
    char movedSave[SAVE_AUX_PATH_MAX] = { 0 };
    char movedSidecar[SAVE_AUX_PATH_MAX] = { 0 };
    PathState sidecarState;

    if (!IsManagedProfilePath(path) || strcmp(path, sActivePath) == 0) return 0;
    if (GetPathState(path) != PATH_STATE_EXISTS ||
        !BuildSidecarPathForSave(path, sidecar, sizeof(sidecar))) {
        return 0;
    }
    sidecarState = GetPathState(sidecar);
    if (sidecarState == PATH_STATE_ERROR) return 0;

    /* First move every member out of the live profile namespace. Until the
     * save move commits, a sidecar move can be rolled back losslessly. */
    if (sidecarState == PATH_STATE_EXISTS &&
        !MoveFileUniqueWithPath(sidecar, "deleted-profile", movedSidecar, sizeof(movedSidecar))) {
        return 0;
    }
    if (TestShouldFailProfileCommit() ||
        !MoveFileUniqueWithPath(path, "deleted-profile", movedSave, sizeof(movedSave))) {
        if (movedSidecar[0] != '\0' && rename(movedSidecar, sidecar) != 0) {
            fprintf(stderr, "[SAVE] Delete rollback retained the randomizer data at %s.\n", movedSidecar);
        }
        return 0;
    }

    /* Logical deletion is committed: neither live filename exists. Cleanup is
     * best-effort and deliberately recoverable if the filesystem refuses it. */
    RemoveCreatedFileOrLog(movedSidecar, "Delete cleanup");
    RemoveCreatedFileOrLog(movedSave, "Delete cleanup");
    return 1;
}

/* Rename a profile file. Both args must look like managed profile
 * names. The default tmc.sav cannot be renamed away (it's our fallback
 * for fresh installs). If renaming the active profile, also updates
 * sActivePath so subsequent reads/writes hit the new name. */
int Port_Save_RenameProfile(const char* oldPath, const char* newPath) {
    char oldSidecar[SAVE_AUX_PATH_MAX];
    char newSidecar[SAVE_AUX_PATH_MAX];
    PathState oldSidecarState;

    if (!IsManagedProfilePath(oldPath) || !IsManagedProfilePath(newPath)) return 0;
    if (strcmp(oldPath, DEFAULT_SAVE_FILENAME) == 0)
        return 0; /* don't rename default away */
    if (strcmp(oldPath, newPath) == 0)
        return 1; /* no-op */
    /* The public config setters do not report whether config.json reached
     * durable storage. Renaming the active profile without that guarantee
     * could make the next launch select the old, now-missing filename. */
    if (strcmp(oldPath, sActivePath) == 0) return 0;
    if (GetPathState(oldPath) != PATH_STATE_EXISTS || GetPathState(newPath) != PATH_STATE_MISSING ||
        !BuildSidecarPathForSave(oldPath, oldSidecar, sizeof(oldSidecar)) ||
        !BuildSidecarPathForSave(newPath, newSidecar, sizeof(newSidecar))) {
        return 0;
    }
    oldSidecarState = GetPathState(oldSidecar);
    if (oldSidecarState == PATH_STATE_ERROR || GetPathState(newSidecar) != PATH_STATE_MISSING) return 0;

    /* Copy first so the source pair remains authoritative until the save
     * rename commits. If commit fails, the newly created copy is discarded. */
    if (oldSidecarState == PATH_STATE_EXISTS && !CopyFileDurableExclusive(oldSidecar, newSidecar)) return 0;
    if (TestShouldFailProfileCommit() || rename(oldPath, newPath) != 0) {
        if (oldSidecarState == PATH_STATE_EXISTS) RemoveCreatedFileOrLog(newSidecar, "Rename");
        return 0;
    }
    if (oldSidecarState == PATH_STATE_EXISTS && remove(oldSidecar) != 0 && errno != ENOENT) {
        /* Both copies are byte-identical and the new profile is complete; an
         * undeletable old sidecar is only a recoverable orphan. */
        fprintf(stderr, "[SAVE] Renamed profile; redundant source sidecar remains at %s.\n", oldSidecar);
    }
    return 1;
}
