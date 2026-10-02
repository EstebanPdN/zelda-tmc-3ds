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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define EEPROM_SIZE 8192                           /* 8 KB */
#define EEPROM_BLOCK 8                             /* 8 bytes per block */
#define EEPROM_BLOCKS (EEPROM_SIZE / EEPROM_BLOCK) /* 1024 */
#define DEFAULT_SAVE_FILENAME "tmc.sav"
#define SAVE_FILENAME_MAX 64
#define SAVE_AUX_PATH_MAX (SAVE_FILENAME_MAX + 16)

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
static char sActivePath[SAVE_FILENAME_MAX] = DEFAULT_SAVE_FILENAME;
static PortSaveStats sSaveStats;
/* 1 once the user has explicitly chosen a named profile (config.json), so the
 * per-region default below must NOT override their choice. 0 in the default
 * case, where the multi-region build isolates each region into its own file. */
static int sExplicitProfile = 0;

/* ---- On-disk byte order -------------------------------------------------- */

/* First 8-byte block of every initialized TMC save ("AGBZELDA:..."), in
 * game-RAM order and in on-disk (mGBA wire) order. Same in all regions. */
#define EEPROM_SIG_RAM "AGBZELDA"

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

static int FileExists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
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
    const int closeOk = fclose(file) == 0;
    return total == EEPROM_SIZE && trailing == EOF && closeOk;
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
static void RecoverInterruptedAtomicWrite(const char* path) {
    char temp[SAVE_AUX_PATH_MAX];
    char rollback[SAVE_AUX_PATH_MAX];
    MakeAuxiliaryPaths(path, temp, rollback);

    if (FileHasExactSize(path)) {
        remove(temp);
        remove(rollback);
        return;
    }

    const char* candidate = NULL;
    if (FileHasExactSize(rollback)) {
        candidate = rollback;
    } else if (FileHasExactSize(temp)) {
        candidate = temp;
    }
    if (!candidate) return;

    sSaveStats.lastStage = PORT_SAVE_STAGE_RECOVER;
    if (FileExists(path) && remove(path) != 0) {
        sSaveStats.lastErrno = errno != 0 ? errno : EIO;
        return;
    }
    if (rename(candidate, path) == 0) {
        ++sSaveStats.interruptedRecoveries;
        sSaveStats.lastErrno = 0;
        remove(temp);
        remove(rollback);
    } else {
        sSaveStats.lastErrno = errno != 0 ? errno : EIO;
    }
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
    MakeAuxiliaryPaths(path, tmp, rollback);
    ++sSaveStats.flushAttempts;
    BuildEepromDiskImage();

    if (strlen(path) + sizeof(".rollback") > sizeof(rollback)) {
        RecordSaveFailure(PORT_SAVE_STAGE_OPEN_TEMP, ENAMETOOLONG);
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
    if (FileExists(path)) {
        remove(rollback);
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
        remove(path);
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
static int sStandardProfile = 1;
static int HasUnsupportedProfile(const char* path) {
    char marker[512];
    struct stat info;
    if (!path) return 1;
    size_t n = strlen(path);
    if (n >= 10 && strcmp(path + n - 10, "_rando.sav") == 0) return 1;
    if (n + 12 >= sizeof(marker)) return 1;
    strcpy(marker, path);
    char* ext = strrchr(marker, '.');
    if (!ext) ext = marker + n;
    strcpy(ext, ".randomizer");
    if (stat(marker, &info) == 0) return 1;
    return errno != ENOENT;
}
int Port_Save_IsStandardProfile(void) { return sStandardProfile; }

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
    if (sExplicitProfile) {
        return;
    }
    if (REGION_IS_EU) {
        name = "tmc_eu.sav";
    } else if (REGION_IS_JP) {
        name = "tmc_jp.sav";
    } else {
        name = DEFAULT_SAVE_FILENAME;
    }
    snprintf(sActivePath, sizeof(sActivePath), "%s", name);
}
#endif

static void LoadEepromFile(void) {
#ifdef MULTI_REGION
    ResolveRegionDefaultPath();
#endif
    sStandardProfile = !HasUnsupportedProfile(sActivePath);
    if (!sStandardProfile) {
        memset(sEeprom, 0xFF, EEPROM_SIZE);
        return;
    }

#ifdef TMC_3DS
    RecoverInterruptedAtomicWrite(sActivePath);
#endif
    FILE* f = fopen(sActivePath, "rb");
    if (!f) {
        memset(sEeprom, 0xFF, EEPROM_SIZE); /* blank EEPROM = 0xFF */
        fprintf(stderr, "[SAVE] No save file at %s, starting fresh.\n", sActivePath);
        return;
    }
    const size_t got = fread(sEeprom, 1, EEPROM_SIZE, f);
    fclose(f);
    if (got != EEPROM_SIZE) {
        fprintf(stderr, "[SAVE] ERROR: short read on %s (%zu/%d bytes), starting fresh.\n", sActivePath, got,
                EEPROM_SIZE);
        memset(sEeprom, 0xFF, EEPROM_SIZE); /* blank EEPROM = 0xFF */
        return;
    }
    if (memcmp(sEeprom, EEPROM_SIG_RAM, EEPROM_BLOCK) == 0) {
        /* Legacy port-format file (game-RAM order on disk). The buffer
         * is already in the order we keep in memory; keep the original
         * bytes as .bak, then rewrite the file in on-disk order. */
        char bak[SAVE_FILENAME_MAX + 4];
        snprintf(bak, sizeof(bak), "%s.bak", sActivePath);
        int backedUp = 0;
        FILE* bf = fopen(bak, "wb");
        if (bf) {
            backedUp = fwrite(sEeprom, 1, EEPROM_SIZE, bf) == EEPROM_SIZE;
            backedUp &= fclose(bf) == 0;
        }
        fprintf(stderr, "[SAVE] Migrating %s to mGBA byte order (backup: %s)%s.\n", sActivePath, bak,
                backedUp ? "" : " — BACKUP FAILED");
        sEepromDirty = 1;
        FlushEepromFile();
    } else {
        /* mGBA/VBA-M order — or blank/uninitialized, where reversal is
         * inconsequential. Convert to game-RAM order in memory. */
        ReverseEepromBlocks(sEeprom);
        fprintf(stderr, "[SAVE] Loaded save file: %s\n", sActivePath);
    }
}

static void FlushEepromFile(void) {
    if (!sStandardProfile) return;
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
    return !sEepromDirty;
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
    if (!sStandardProfile) return 0xC000;
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    memcpy(&sEeprom[block * EEPROM_BLOCK], src, EEPROM_BLOCK);
    sEepromDirty = 1;

    /* Flush immediately (crash safety) unless a save transaction is open —
     * then the single flush happens at Port_Save_EndTransaction(). */
    if (sSaveTxnDepth == 0)
        FlushEepromFile();
    return 0; /* success */
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
void Port_Save_SetActivePath(const char* path) {
    if (path == NULL || path[0] == '\0') {
        path = DEFAULT_SAVE_FILENAME;
    } else if (!IsManagedProfilePath(path)) {
        /* The active-profile name comes from config.json (user-editable);
         * refuse anything outside the tmc.sav / tmc_<name>.sav lane so a
         * crafted value can't redirect saves elsewhere on disk. */
        fprintf(stderr, "[SAVE] Ignoring unmanaged save profile '%s'; using %s.\n", path, DEFAULT_SAVE_FILENAME);
        path = DEFAULT_SAVE_FILENAME;
    }
    /* If the EEPROM was already loaded under the old path, flush it
     * first so the user doesn't lose pending writes when switching. */
    if (sEepromInited && sEepromDirty) {
        FlushEepromFile();
    }
    strncpy(sActivePath, path, sizeof(sActivePath) - 1);
    sActivePath[sizeof(sActivePath) - 1] = '\0';
    /* A named profile (anything other than the default tmc.sav) is the user's
     * explicit, region-agnostic choice; the multi-region per-region default
     * (ResolveRegionDefaultPath) must not override it. */
    sExplicitProfile = (strcmp(path, DEFAULT_SAVE_FILENAME) != 0);
    sStandardProfile = !HasUnsupportedProfile(path);
    /* Force a reload on next access so any read after this point hits
     * the new file. */
    sEepromInited = 0;
    sEepromDirty = 0;
}

const char* Port_Save_GetActivePath(void) {
    return sActivePath;
}


/* Snapshot the in-memory EEPROM into a named profile file without
 * changing the active profile. Useful for "Save current state as a new
 * profile" — keep playing in the current profile while the named copy
 * captures right-now state. Returns 0 on failure. */
int Port_Save_SaveAsProfile(const char* path) {
    if (HasUnsupportedProfile(path) || !sStandardProfile) return 0;
    if (path == NULL || path[0] == '\0')
        return 0;
    /* Only allow writing into the managed profile lane so the "save as"
     * UI can't be pointed at an arbitrary host path. */
    if (!IsManagedProfilePath(path))
        return 0;
    /* Ensure EEPROM was loaded at least once so we have meaningful data
     * to copy. (Right after launch, before any read, sEeprom is zeroed.) */
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (!sStandardProfile) return 0;
    return WriteEepromAtomic(path);
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
        if (HasUnsupportedProfile(name)) continue;
        if (strcmp(name, DEFAULT_SAVE_FILENAME) == 0 || strncmp(name, "tmc_", 4) == 0) {
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
        if (HasUnsupportedProfile(name)) continue;
        const size_t len = strlen(name);
        if (len < 4)
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
    const size_t len = strlen(path);
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
    if (HasUnsupportedProfile(path)) return 0;
    if (!IsManagedProfilePath(path))
        return 0;
    if (strcmp(path, sActivePath) == 0)
        return 0; /* refuse to delete active */
    return remove(path) == 0 ? 1 : 0;
}

/* Rename a profile file. Both args must look like managed profile
 * names. The default tmc.sav cannot be renamed away (it's our fallback
 * for fresh installs). If renaming the active profile, also updates
 * sActivePath so subsequent reads/writes hit the new name. */
int Port_Save_RenameProfile(const char* oldPath, const char* newPath) {
    if (HasUnsupportedProfile(oldPath) || HasUnsupportedProfile(newPath)) return 0;
    if (!IsManagedProfilePath(oldPath))
        return 0;
    if (!IsManagedProfilePath(newPath))
        return 0;
    if (strcmp(oldPath, DEFAULT_SAVE_FILENAME) == 0)
        return 0; /* don't rename default away */
    if (strcmp(oldPath, newPath) == 0)
        return 1; /* no-op */
    /* Refuse clobbering an existing file — fail-stop is safer than
     * silently replacing somebody else's save. */
    FILE* probe = fopen(newPath, "rb");
    if (probe) {
        fclose(probe);
        return 0;
    }
    if (rename(oldPath, newPath) != 0)
        return 0;
    if (strcmp(oldPath, sActivePath) == 0) {
        strncpy(sActivePath, newPath, SAVE_FILENAME_MAX - 1);
        sActivePath[SAVE_FILENAME_MAX - 1] = '\0';
    }
    return 1;
}
