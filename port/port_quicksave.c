/*
 * port_quicksave.c — multi-slot save-states with disk persistence + auto-save.
 *
 * Snapshots a curated set of game-state regions into an in-memory slot
 * on save, restores them on load. Slots also get serialized to disk as
 * `state_<slot>.bin` next to the binary so they survive restart.
 *
 * Slot layout:
 *   slot 0:    F5 quicksave / F6 quickload (legacy single-slot API)
 *   slot 1-4:  F1..F4 load,  Shift+F1..F4 save (numbered manual slots)
 *   slot 5-7:  auto-save ring (Port_QuickSave_Auto cycles through these)
 *
 * File format (disk persistence):
 *   magic    "TMCS"                          (4 bytes)
 *   version  PORT_QUICKSAVE_VERSION          (u32 LE)
 *   total    sum of all region sizes         (u32 LE)
 *   data     concatenated region bytes       (in sRegions[] order)
 *
 * On load, if magic/version/size don't match, the file is rejected
 * silently — the in-memory snapshot (if any) stays untouched. This is
 * defensive against schema changes between builds; saves are best-effort,
 * not a contract.
 *
 * Coverage: emulated GBA memory (EWRAM/IWRAM/VRAM/IO), the save file,
 * the player + state, the room controls + transition, gMain, and the
 * full gEntities array. Anything not in this list (HUD state, OAM, gfx
 * slots, palette buffers) will visually catch up over the next frame.
 *
 * Caveats:
 *  - Snapshotting mid-frame is supported but the visible result is
 *    "next frame" — entity logic that ran this frame may have already
 *    written to OAM, which is not snapshotted.
 *  - Save-states are not the same as the game's in-engine save file
 *    (`tmc.sav`). The game's own save still goes through its file-select
 *    flow. Save-states capture transient runtime state including
 *    mid-cutscene positions.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "structures.h"
#include "save.h"
#include "main.h"
#include "entity.h"
#include "port_gba_mem.h"
#include "port_runtime_config.h"
#include "region.h" /* REGION_IS_EU/JP — per-region savestate isolation (#21) */

extern u8 gEwram[];
extern u8 gIwram[];
extern u8 gVram[];
extern u8 gIoMem[];

/* The gameplay PRNG seed. On GBA this lived in IWRAM (0x03001150) and was
 * therefore captured by an IWRAM-snapshotting savestate; in the port it is a
 * standalone host global (port_linked_stubs.c), so it must be listed explicitly
 * or QuickLoad would restore everything EXCEPT RNG state and desync manips. */
extern u32 gRand;

/* Speedrun-practice IGT frame counter (port_practice.c). Listed as a region
 * so loading any savestate rewinds the practice timer to the value captured
 * with that state — "reload the section and the timer comes back too". */
extern u64 gPracticeFrame;

/* Defined in src/player.c — re-resolves the player's .rodata hitbox pointer
   from the current form after a cross-process quickload (FixupEntityPointers
   only relocates pointers that land inside gEntities). */
void Port_RestorePlayerHitbox(void);

typedef struct {
    void* ptr;
    size_t size;
    const char* name;
} StateRegion;

/* List of regions captured by a save-state. The order doesn't matter for
 * save, but for restore the order must stay consistent with what was on
 * disk — which is why the disk format records the total size and we
 * reject files that don't match the current region layout. */
static StateRegion sRegions[] = {
    { gEwram, 0x40000, "gEwram" },
    { gIwram, 0x8000, "gIwram" },
    { gVram, 0x18000, "gVram" },
    { gIoMem, 0x400, "gIoMem" },
    { &gSave, sizeof(gSave), "gSave" },
    { &gPlayerEntity, sizeof(gPlayerEntity), "gPlayerEntity" },
    { &gPlayerState, sizeof(gPlayerState), "gPlayerState" },
    { &gMain, sizeof(gMain), "gMain" },
    { &gRoomControls, sizeof(gRoomControls), "gRoomControls" },
    { &gRoomTransition, sizeof(gRoomTransition), "gRoomTransition" },
    { gEntities, sizeof(gEntities), "gEntities" },
    { &gRand, sizeof(gRand), "gRand" },
    { &gPracticeFrame, sizeof(gPracticeFrame), "gPracticeFrame" },
};

#define NUM_REGIONS (sizeof(sRegions) / sizeof(sRegions[0]))
#define NUM_SLOTS 8 /* 0..4 manual + 5..7 auto-save ring */
#define AUTO_SLOT_BASE 5
#define NUM_AUTO_SLOTS 3
#define MAGIC 0x53434D54u /* "TMCS" little-endian */
#define VERSION                                         \
    6u /* v2: header carries gEntities base address for \
        * cross-process pointer-fixup on restore.       \
        * v3: gRand added to region list so RNG         \
        * state round-trips (GBA had it in IWRAM).      \
        * v4: gPracticeFrame added so speedrun IGT      \
        * timer rewinds with state.                     \
        * v5: ROM region tag — USA state restored     \
        * into a JP session contaminates tmc_jp.sav     \
        * (#21); cross-region loads are refused.        \
        * v6: entity subclass layouts changed; older    \
        * snapshots are rejected. */

typedef struct {
    u8* snapshot; /* heap, NULL if slot empty */
    size_t bytes;
    int valid;
    u64 saved_at_unix;       /* clock_gettime CLOCK_REALTIME seconds */
    u64 saved_entities_base; /* gEntities address at save time; 0 for in-RAM slots */
} Slot;

static Slot sSlots[NUM_SLOTS];
static int sAutoNextSlot = AUTO_SLOT_BASE; /* round-robin cursor */
static u64 sAutoLastSaveTicksMs = 0;
static int sAutoEnabled = 1;        /* on by default — the F8
                                       toggle (and config.json)
                                       can flip it off. */
static u32 sAutoIntervalMs = 60000; /* 60 seconds default */

/* Area-change auto-save (independent of the interval timer). Tracks
 * the last-observed (area, room) and saves to the ring whenever it
 * changes. Helps with the crash-on-load-then-lose-an-hour case Jester
 * flagged. */
static int sAutoOnAreaChange = 1;
static u8 sLastSeenArea = 0xFF;
static u8 sLastSeenRoom = 0xFF;

static size_t TotalRegionBytes(void) {
    size_t total = 0;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        total += sRegions[i].size;
    }
    return total;
}

static int Snapshot_Capture(Slot* s) {
    const size_t total = TotalRegionBytes();
    if (s->snapshot == NULL || s->bytes != total) {
        free(s->snapshot);
        s->snapshot = (u8*)malloc(total);
        if (s->snapshot == NULL) {
            s->bytes = 0;
            s->valid = 0;
            fprintf(stderr, "[quicksave] alloc failed (%zu bytes)\n", total);
            return 0;
        }
        s->bytes = total;
    }
    u8* dst = s->snapshot;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(dst, sRegions[i].ptr, sRegions[i].size);
        dst += sRegions[i].size;
    }
    s->valid = 1;
    s->saved_at_unix = (u64)time(NULL);
    return 1;
}

static int Snapshot_MatchesCurrent(const Slot* s, const char** region, size_t* offset, u8* expected, u8* actual) {
    const u8* src;
    size_t i;

    if (!s->valid || s->snapshot == NULL || s->bytes != TotalRegionBytes()) {
        *region = "snapshot";
        *offset = 0;
        *expected = 0;
        *actual = 0;
        return 0;
    }

    src = s->snapshot;
    for (i = 0; i < NUM_REGIONS; i++) {
        const u8* current = (const u8*)sRegions[i].ptr;
        size_t j;

        if (memcmp(src, current, sRegions[i].size) == 0) {
            src += sRegions[i].size;
            continue;
        }
        for (j = 0; j < sRegions[i].size && src[j] == current[j]; j++) {}
        *region = sRegions[i].name;
        *offset = j;
        *expected = src[j];
        *actual = current[j];
        return 0;
    }
    return 1;
}

/* When a slot was written by a previous process, every pointer captured
 * inside gEntities (prev/next/child/parent and any subclass-specific
 * Entity* fields) refers to the old ASLR base. Walk the restored bytes
 * 8-aligned and rewrite anything that falls in [saved_base, saved_base +
 * sizeof(gEntities)) to the corresponding offset under the new base.
 *
 * This is a heuristic — it can rewrite false positives if some non-
 * pointer field happens to hold a value in the saved range. In practice
 * the saved range is small (~5 MB) and that's vanishingly unlikely; the
 * alternative (parsing every Entity subclass to know which fields are
 * pointers) is unmaintainable. Misses the entity-list heads too —
 * those live in gEntityLists which isn't a saved region. */
static void FixupEntityPointers(u64 saved_base) {
    if (saved_base == 0)
        return;
    const uintptr_t cur_base = (uintptr_t)gEntities;
    if ((uintptr_t)saved_base == cur_base)
        return; /* same address, no-op */
    const uintptr_t saved_lo = (uintptr_t)saved_base;
    const uintptr_t saved_hi = saved_lo + sizeof(gEntities);
    const intptr_t delta = (intptr_t)cur_base - (intptr_t)saved_lo;
    uintptr_t* p = (uintptr_t*)gEntities;
    /* Whole-array word scan: total bytes / word size, NOT entity count.
     * The parens silence -Wsizeof-array-div, which mistakes this for an
     * element-count division (element type is GenericEntity, not uintptr_t). */
    const size_t n = sizeof(gEntities) / (sizeof(uintptr_t));
    size_t fixed = 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] >= saved_lo && p[i] < saved_hi) {
            p[i] = (uintptr_t)((intptr_t)p[i] + delta);
            ++fixed;
        }
    }
    fprintf(stderr,
            "[quicksave] pointer-fixup: %zu pointers shifted by %p "
            "(saved base %p → current %p)\n",
            fixed, (void*)delta, (void*)saved_lo, (void*)cur_base);
}

static int Snapshot_Restore(const Slot* s) {
    if (!s->valid || s->snapshot == NULL || s->bytes != TotalRegionBytes()) {
        return 0;
    }
    const u8* src = s->snapshot;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(sRegions[i].ptr, src, sRegions[i].size);
        src += sRegions[i].size;
    }
    /* gEntities was just overwritten; fix any pointer fields that
     * point to the saved process's gEntities range. Safe no-op when
     * the slot was made in this process (same base). */
    FixupEntityPointers(s->saved_entities_base);

    /* Cross-process restore only: pointers that target fixed globals / .rodata
     * (not gEntities) are NOT relocated by FixupEntityPointers and are left
     * pointing into the previous process's address space. Re-establish the two
     * that are dereferenced unguarded every frame after a load:
     *   - gRoomControls.camera_target — room restore paths deref it while
     *     rebuilding scroll. After fixup a usable target is an entity now inside
     *     the relocated gEntities range; NULL and non-NULL pointers outside
     *     gEntities both leave the restore path without a safe target, so point
     *     them at the live player.
     *   - gPlayerEntity.base.hitbox — a stale .rodata pointer; the player tile-
     *     probe / interactable scan deref it without the IsColliding guard.
     * In-process F5/F6 keeps the same base (FixupEntityPointers no-ops), so
     * these are skipped there to leave the fast path untouched. */
    if (s->saved_entities_base != 0 && (uintptr_t)s->saved_entities_base != (uintptr_t)gEntities) {
        uintptr_t ct = (uintptr_t)gRoomControls.camera_target;
        uintptr_t lo = (uintptr_t)gEntities;
        uintptr_t hi = lo + sizeof(gEntities);
        if (ct == 0 || ct < lo || ct >= hi) {
            gRoomControls.camera_target = (Entity*)&gPlayerEntity;
        }
        Port_RestorePlayerHitbox();
    }
    return 1;
}

/* Headless deterministic restore/replay gate. With no external input, advance
 * the same 120 engine frames twice from one snapshot and require every saved
 * byte to agree. Runs here so capture/restore stays on a frame boundary. */
static int QuickSaveReplayTestTick(void) {
    enum {
        SETTLE_FRAMES = 30,
        DEFAULT_REPLAY_FRAMES = 120,
        MAX_REPLAY_FRAMES = 3600,
    };
    static int enabled = -1;
    static int phase = 0;
    static unsigned int phaseFrame = 0;
    static unsigned int replayFrames = DEFAULT_REPLAY_FRAMES;
    static Slot start;
    static Slot expectedEnd;
    const char* region;
    size_t offset;
    u8 expected;
    u8 actual;

    if (enabled < 0) {
        const char* env = getenv("TMC_REPRO_QUICKSAVE_ROUNDTRIP");
        enabled = env != NULL && env[0] != '\0' && strcmp(env, "0") != 0;
        if (enabled) {
            const char* frames = getenv("TMC_REPRO_QUICKSAVE_FRAMES");
            char* end;
            unsigned long requested = frames != NULL ? strtoul(frames, &end, 10) : 0;
            if (frames != NULL && requested > 0 && requested <= MAX_REPLAY_FRAMES && end != frames && *end == '\0') {
                replayFrames = (unsigned int)requested;
            }
            fprintf(stderr, "[quicksave-test] enabled: settle=%d replay=%u frames\n", SETTLE_FRAMES, replayFrames);
        }
    }
    if (!enabled) {
        return 0;
    }

    if (phase == 0 && gMain.task != TASK_GAME) {
        phaseFrame = 0;
        return 1;
    }

    phaseFrame++;
    if (phase == 0 && phaseFrame >= SETTLE_FRAMES) {
        if (!Snapshot_Capture(&start)) {
            fprintf(stderr, "[quicksave-test] FAIL: initial capture failed\n");
            fflush(stderr);
            _Exit(1);
        }
        fprintf(stderr, "[quicksave-test] captured start (%zu bytes)\n", start.bytes);
        phase = 1;
        phaseFrame = 0;
    } else if (phase == 1 && phaseFrame >= replayFrames) {
        if (!Snapshot_Capture(&expectedEnd) || !Snapshot_Restore(&start)) {
            fprintf(stderr, "[quicksave-test] FAIL: endpoint capture or restore failed\n");
            fflush(stderr);
            _Exit(1);
        }
        if (!Snapshot_MatchesCurrent(&start, &region, &offset, &expected, &actual)) {
            fprintf(stderr, "[quicksave-test] FAIL: restore mismatch %s+0x%zx expected=%02x actual=%02x\n", region,
                    offset, (unsigned)expected, (unsigned)actual);
            fflush(stderr);
            _Exit(1);
        }
        fprintf(stderr, "[quicksave-test] restored start; replaying\n");
        phase = 2;
        phaseFrame = 0;
    } else if (phase == 2 && phaseFrame >= replayFrames) {
        if (!Snapshot_MatchesCurrent(&expectedEnd, &region, &offset, &expected, &actual)) {
            fprintf(stderr, "[quicksave-test] FAIL: replay mismatch %s+0x%zx expected=%02x actual=%02x\n", region,
                    offset, (unsigned)expected, (unsigned)actual);
            fflush(stderr);
            _Exit(1);
        }
        fprintf(stderr, "[quicksave-test] PASS: %u-frame replay byte-identical (%zu bytes)\n", replayFrames,
                expectedEnd.bytes);
        fflush(stderr);
        _Exit(0);
    }

    return 1;
}

/* Region tag for the state header + per-region state filenames. EU/JP get
 * their own state files (like tmc_eu.sav / tmc_jp.sav) so sessions never
 * even see another region's states; USA keeps the legacy names. */
static u32 ActiveRegionTag(void) {
    if (REGION_IS_EU)
        return 2;
    if (REGION_IS_JP)
        return 3;
    return 1; /* USA */
}

static void SlotFilename(int slot, char* out, size_t cap) {
    const char* prefix = REGION_IS_EU ? "state_eu" : REGION_IS_JP ? "state_jp" : "state";
    if (slot >= AUTO_SLOT_BASE) {
        snprintf(out, cap, "%s_auto_%d.bin", prefix, slot - AUTO_SLOT_BASE);
    } else if (slot == 0) {
        snprintf(out, cap, "%s_quick.bin", prefix);
    } else {
        snprintf(out, cap, "%s_%d.bin", prefix, slot);
    }
}

static int WriteSlotToDisk(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    Slot* s = &sSlots[slot];
    if (!s->valid || s->snapshot == NULL)
        return 0;

    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[quicksave] open %s for write failed\n", path);
        return 0;
    }
    const u32 magic = MAGIC;
    const u32 version = VERSION;
    const u32 total = (u32)s->bytes;
    const u64 saved_at = s->saved_at_unix;
    /* gEntities base — needed to fix up internal pointers when the
     * file is loaded by a later process (different ASLR base). The
     * captured bytes still contain prev/next/child/parent pointers
     * into the old process's gEntities[]; without fixup, restoring
     * them and running the entity-update loop dereferences unmapped
     * memory. */
    const u64 entities_base = (u64)(uintptr_t)gEntities;
    const u32 region_tag = ActiveRegionTag();
    if (fwrite(&magic, sizeof(magic), 1, f) != 1 || fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&total, sizeof(total), 1, f) != 1 || fwrite(&saved_at, sizeof(saved_at), 1, f) != 1 ||
        fwrite(&entities_base, sizeof(entities_base), 1, f) != 1 ||
        fwrite(&region_tag, sizeof(region_tag), 1, f) != 1) {
        fprintf(stderr, "[quicksave] header write failed for %s\n", path);
        fclose(f);
        return 0;
    }
    const size_t written = fwrite(s->snapshot, 1, s->bytes, f);
    fclose(f);
    if (written != s->bytes) {
        fprintf(stderr, "[quicksave] short write %s (%zu/%zu)\n", path, written, s->bytes);
        return 0;
    }
    return 1;
}


/* Reads the fixed 4-field slot header (magic, version, total, saved_at).
 * Returns 1 if all four fields read, 0 on short read. Field values are
 * not validated and no diagnostics are emitted — callers decide. */
static int ReadSlotHeader(FILE* f, u32* magic, u32* version, u32* total, u64* saved_at) {
    return fread(magic, sizeof(*magic), 1, f) == 1 && fread(version, sizeof(*version), 1, f) == 1 &&
           fread(total, sizeof(*total), 1, f) == 1 && fread(saved_at, sizeof(*saved_at), 1, f) == 1;
}

static int ReadSlotFromDisk(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    u32 magic = 0, version = 0, total = 0;
    u64 saved_at = 0;
    u64 saved_entities_base = 0;
    if (!ReadSlotHeader(f, &magic, &version, &total, &saved_at)) {
        fprintf(stderr, "[quicksave] short read on %s header, ignoring slot file\n", path);
        fclose(f);
        return 0;
    }
    if (magic != MAGIC || version != VERSION || total != (u32)TotalRegionBytes()) {
        fclose(f);
        return 0;
    }
    if (fread(&saved_entities_base, sizeof(saved_entities_base), 1, f) != 1) {
        fprintf(stderr, "[quicksave] short read on %s entity-base header, ignoring slot file\n", path);
        fclose(f);
        return 0;
    }
    {
        u32 region_tag = 0;
        if (fread(&region_tag, sizeof(region_tag), 1, f) != 1) {
            fprintf(stderr, "[quicksave] short read on %s region header, ignoring slot file\n", path);
            fclose(f);
            return 0;
        }
        if (region_tag != ActiveRegionTag()) {
            fprintf(stderr, "[quicksave] %s was saved in a different ROM region (%u != %u) — refusing load\n", path,
                    region_tag, ActiveRegionTag());
            fclose(f);
            return 0;
        }
    }
    Slot* s = &sSlots[slot];
    if (s->snapshot == NULL || s->bytes != total) {
        free(s->snapshot);
        s->snapshot = (u8*)malloc(total);
        if (!s->snapshot) {
            s->bytes = 0;
            s->valid = 0;
            fclose(f);
            return 0;
        }
        s->bytes = total;
    }
    const size_t got = fread(s->snapshot, 1, total, f);
    fclose(f);
    if (got != total) {
        fprintf(stderr, "[quicksave] short read on %s (%zu/%u bytes), ignoring slot file\n", path, got, total);
        s->valid = 0;
        return 0;
    }
    s->valid = 1;
    s->saved_at_unix = saved_at;
    s->saved_entities_base = saved_entities_base;
    return 1;
}

/* ============================================================
 *   Public API
 * ============================================================ */

int Port_QuickSave_SaveSlot(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    if (!Snapshot_Capture(&sSlots[slot]))
        return 0;
    /* Best-effort disk persistence — failure is non-fatal, the in-memory
     * snapshot still works for the session. */
    WriteSlotToDisk(slot);
    fprintf(stderr, "[quicksave] slot %d saved (%zu bytes)\n", slot, sSlots[slot].bytes);
    return 1;
}

int Port_QuickSave_LoadSlot(int slot) {
    /* Refuse any state restore under Console-Parity — covers the menu/imgui
     * load buttons too, not just the F-key hotkeys. */
    if (Port_Config_GetConsoleParity())
        return 0;
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    if (!sSlots[slot].valid) {
        /* Try loading from disk first — handles "fresh launch, never
         * saved this session but slot exists on disk from last run". */
        if (!ReadSlotFromDisk(slot)) {
            fprintf(stderr, "[quicksave] slot %d empty\n", slot);
            return 0;
        }
    }
    if (!Snapshot_Restore(&sSlots[slot])) {
        fprintf(stderr, "[quicksave] slot %d restore failed\n", slot);
        return 0;
    }
    fprintf(stderr, "[quicksave] slot %d restored\n", slot);
    {
        /* Tell the Reborn-parity layer a resume just happened so it
         * can swallow the next queued Ezlo hint (if that toggle is on). */
        extern void Port_Reborn_NotifyJustResumed(void);
        Port_Reborn_NotifyJustResumed();
    }
    return 1;
}

int Port_QuickSave_HasSlot(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    if (sSlots[slot].valid)
        return 1;
    /* Probe disk so the menu can label populated-on-disk slots correctly
     * before the user touches them. */
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* ---- Practice point -------------------------------------------------- *
 * A dedicated in-memory snapshot for the speedrun practice mode, kept
 * separate from the F1..F5 user slots so practising a segment never clobbers
 * a manual save. In-process only (no disk, no pointer fixup needed — the
 * entities base is unchanged within a run), so set/reload is a sub-ms memcpy.
 * Driven from port_practice.c via the Port_Practice_SetPoint/LoadPoint API. */
static Slot sPracticeSlot;

int Port_QuickSave_SavePractice(void) {
    if (!Snapshot_Capture(&sPracticeSlot))
        return 0;
    fprintf(stderr, "[quicksave] practice point set (%zu bytes)\n", sPracticeSlot.bytes);
    return 1;
}

int Port_QuickSave_LoadPractice(void) {
    if (Port_Config_GetConsoleParity())
        return 0;
    if (!sPracticeSlot.valid) {
        fprintf(stderr, "[quicksave] practice point empty\n");
        return 0;
    }
    if (!Snapshot_Restore(&sPracticeSlot)) {
        fprintf(stderr, "[quicksave] practice point restore failed\n");
        return 0;
    }
    {
        extern void Port_Reborn_NotifyJustResumed(void);
        Port_Reborn_NotifyJustResumed();
    }
    return 1;
}

int Port_QuickSave_HasPractice(void) {
    return sPracticeSlot.valid ? 1 : 0;
}

u64 Port_QuickSave_SlotTimestamp(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return 0;
    if (sSlots[slot].valid)
        return sSlots[slot].saved_at_unix;
    /* Probe the disk file's timestamp header so the menu can show
     * "last saved" even for slots that haven't been loaded into memory. */
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    u32 magic = 0, version = 0, total = 0;
    u64 saved_at = 0;
    if (ReadSlotHeader(f, &magic, &version, &total, &saved_at)) {
        fclose(f);
        return saved_at;
    }
    fclose(f);
    return 0;
}

/* Legacy single-slot API — slot 0 is the F5/F6 quicksave. */
int Port_QuickSave(void) {
    return Port_QuickSave_SaveSlot(0);
}
int Port_QuickLoad(void) {
    return Port_QuickSave_LoadSlot(0);
}
int Port_QuickSave_HasSnapshot(void) {
    return Port_QuickSave_HasSlot(0);
}

/* Auto-save — call once per frame from VBlankIntrWait. Saves to the
 * next slot in the auto-save ring if enabled and the configured
 * interval has elapsed since the last auto-save. */
static void TakeAutoSnapshot(const char* reason) {
    const int slot = sAutoNextSlot;
    sAutoNextSlot++;
    if (sAutoNextSlot >= AUTO_SLOT_BASE + NUM_AUTO_SLOTS) {
        sAutoNextSlot = AUTO_SLOT_BASE;
    }
    if (Port_QuickSave_SaveSlot(slot)) {
        fprintf(stderr, "[autosave] saved to ring slot %d (%s)\n", slot, reason);
    }
}

void Port_QuickSave_AutoTick(void) {
    if (QuickSaveReplayTestTick() || !sAutoEnabled)
        return;
    const u64 now = SDL_GetTicks();

    /* Area-change trigger. gRoomControls is the engine's source-of-
     * truth for the current area/room; we just compare against the
     * last value we observed and fire a snapshot on transition. The
     * first poll seeds the cache without saving (sLastSeen* both
     * 0xFF) so we don't double-save on boot. */
    if (sAutoOnAreaChange) {
        /* gRoomControls is declared in include/room.h, already in scope
         * via include/save.h above. */
        const u8 area = gRoomControls.area;
        const u8 room = gRoomControls.room;
        if (sLastSeenArea == 0xFF && sLastSeenRoom == 0xFF) {
            sLastSeenArea = area;
            sLastSeenRoom = room;
        } else if (area != sLastSeenArea || room != sLastSeenRoom) {
            sLastSeenArea = area;
            sLastSeenRoom = room;
            sAutoLastSaveTicksMs = now;
            TakeAutoSnapshot("area-change");
            return;
        }
    }

    /* Interval trigger. */
    if (sAutoLastSaveTicksMs == 0) {
        sAutoLastSaveTicksMs = now;
        return;
    }
    if (now - sAutoLastSaveTicksMs < sAutoIntervalMs)
        return;
    sAutoLastSaveTicksMs = now;
    TakeAutoSnapshot("interval");
}

int Port_QuickSave_AutoOnAreaChangeEnabled(void) {
    return sAutoOnAreaChange;
}
void Port_QuickSave_SetAutoOnAreaChange(int on) {
    sAutoOnAreaChange = on ? 1 : 0;
    /* Reset the seen-area cache when toggled on so the next change
     * doesn't fire spuriously against pre-toggle history. */
    if (on) {
        sLastSeenArea = 0xFF;
        sLastSeenRoom = 0xFF;
    }
}

void Port_QuickSave_SetAutoEnabled(int enabled) {
    sAutoEnabled = enabled ? 1 : 0;
    if (enabled)
        sAutoLastSaveTicksMs = SDL_GetTicks();
}

int Port_QuickSave_AutoEnabled(void) {
    return sAutoEnabled;
}

void Port_QuickSave_SetAutoIntervalMs(u32 ms) {
    if (ms < 5000)
        ms = 5000; /* clamp to 5s minimum — anything
                      faster would thrash on busy
                      frames and risk visible hitches. */
    if (ms > 600000)
        ms = 600000; /* 10 minute cap */
    sAutoIntervalMs = ms;
}

u32 Port_QuickSave_AutoIntervalMs(void) {
    return sAutoIntervalMs;
}

int Port_QuickSave_SlotCount(void) {
    return NUM_SLOTS;
}
int Port_QuickSave_AutoSlotBase(void) {
    return AUTO_SLOT_BASE;
}
int Port_QuickSave_AutoSlotCount(void) {
    return NUM_AUTO_SLOTS;
}
