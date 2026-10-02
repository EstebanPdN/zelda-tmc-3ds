/*
 * port_rom.c — Load baserom.gba and resolve ROM data symbols.
 *
 * GBA ROM at 0x08000000. Pointer tables and data blobs are translated
 * or copied as needed for the PC port.
 * are translated to native pointers. ROM pages (4 KB) are extracted to
 * rom_data/ so the game can run without the full ROM after first boot.
 */

#include <stddef.h>

#include "port_rom.h"
#include "area.h"
#include "main.h"
#include "map.h"
#include "port_asset_loader.h"
#include "port_config.h"
#include "port_runtime_config.h"
#include "port_gba_mem.h"
#include "structures.h"
#include "tileMap.h"
#if !defined(TMC_N64) && !defined(TMC_3DS)
#include <SDL3/SDL.h>
#elif defined(TMC_3DS)
#include "platform_3ds.h"
extern void* linearAlloc(size_t size);
extern void linearFree(void* memory);
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <dirent.h>
#include <mach-o/dyld.h>
#include <stdlib.h> /* realpath */
#include <sys/stat.h>
#elif !defined(TMC_N64)
#include <dirent.h>
#include <sys/stat.h>
/* Forward-declare readlink to avoid pulling in _POSIX_C_SOURCE feature test
 * macros, which the c11 build mode otherwise hides. */
extern long readlink(const char* path, char* buf, unsigned long bufsiz);
#endif

#ifdef TMC_N64
/* N64/newlib lacks <dirent.h> and several POSIX FS calls. The host ROM
 * file-finding functions that use them are never called on N64 (Port_LoadRom
 * skips file I/O) and are dropped by --gc-sections, but must still COMPILE,
 * so provide just enough declarations. */
typedef struct __n64_DIR DIR;
struct dirent {
    char d_name[256];
};
DIR* opendir(const char*);
struct dirent* readdir(DIR*);
int closedir(DIR*);
int mkdir(const char*, unsigned);
extern long readlink(const char*, char*, unsigned long);
#endif

u8* gRomData = NULL;
static char sLoadedRomPath[4096];

const char* Port_GetLoadedRomPath(void) {
    return sLoadedRomPath[0] ? sLoadedRomPath : NULL;
}
u32 gRomSize = 0;
static SpritePtr sSpritePtrsStable[512];

static u8* AllocateRomBuffer(u32 size, int clear) {
#ifdef TMC_3DS
    /* libctru's normal heap begins at 0x08000000, exactly where GBA ROM
     * addresses begin. Keeping the ROM in linear memory gives native ROM
     * pointers a disjoint address range and removes that ambiguity. */
    u8* buffer = (u8*)linearAlloc(size);
    if (buffer && clear) memset(buffer, 0, size);
    return buffer;
#else
    return clear ? (u8*)calloc(1, size) : (u8*)malloc(size);
#endif
}

static void FreeRomBuffer(u8* buffer) {
#ifdef TMC_3DS
    linearFree(buffer);
#else
    free(buffer);
#endif
}

/* Single source of truth for "what counts as a baserom file".
 * Probed in order — the first hit wins, both by Port_FindBaseRomPath
 * (the pre-window check in port_main.c) and by Port_LoadRom below.
 * Bare filenames are also probed under the binary's own directory so
 * release-tarball layouts work regardless of the user's cwd. */
static const char* kRomCandidates[] = {
    "baserom.gba",           /* USA default */
    "baserom_eu.gba",        /* EU default */
    "baserom_jp.gba",        /* JP default */
    "synthetic_baserom.gba", /* generated from extracted assets */
    "build/pc/baserom.gba",  /* copied to build dir */
    "build/pc/baserom_eu.gba",
    "build/pc/baserom_jp.gba",
    "tmc.gba", /* common alternate names */
    "tmc_eu.gba",
    "tmc_jp.gba",
    /* Developer-tree fallbacks for `cd build/pc && ./tmc_pc`. */
    "../../baserom.gba",
    "../../baserom_eu.gba",
    "../../baserom_jp.gba",
    "../../tmc.gba",
    "../../tmc_eu.gba",
    "../../tmc_jp.gba",
};
#define ROM_CANDIDATE_COUNT ((int)(sizeof(kRomCandidates) / sizeof(kRomCandidates[0])))

static const char* kRomCandidates_USA[] = {
    "baserom.gba", "synthetic_baserom.gba", "build/pc/baserom.gba", "tmc.gba", "../../baserom.gba", "../../tmc.gba",
};
static const char* kRomCandidates_EU[] = {
    "baserom_eu.gba", "build/pc/baserom_eu.gba", "tmc_eu.gba", "../../baserom_eu.gba", "../../tmc_eu.gba",
};
static const char* kRomCandidates_JP[] = {
    "baserom_jp.gba", "build/pc/baserom_jp.gba", "tmc_jp.gba", "../../baserom_jp.gba", "../../tmc_jp.gba",
};

/* Forward declaration so FatalRomError + Port_FindBaseRomPath can
 * sit at the top of the file alongside the candidate list while
 * TryOpenRom keeps its existing position lower down. */
static FILE* TryOpenRom(const char** paths, int count, char* foundPath, int foundPathLen);

/* Surface a fatal ROM-loading failure as both a stderr line and an
 * SDL message box, then exit. Replaces the bare abort() calls that
 * previously left the user with a black screen and no UI feedback.
 * Safe to call before SDL_CreateWindow — SDL_ShowSimpleMessageBox
 * accepts a NULL parent. Exported (declared in port_rom.h) so the
 * region cross-check in port_main.c can reuse it. */
void Port_FatalRomError(const char* title, const char* message) {
    fprintf(stderr, "ERROR: %s\n", message);
    fflush(stderr);
#if defined(TMC_3DS)
    Platform3DS_ShowFatal(title, message);
#elif !defined(TMC_N64)
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, NULL);
    SDL_Quit();
#else
    (void)title;
#endif
    exit(1);
}

const char* Port_FindBaseRomPath(void) {
    static char sFoundPath[4096];
    sFoundPath[0] = '\0';
    /* Explicit override (TMC_BASEROM=/path/to/rom.gba) takes precedence over the
     * probe list. Lets headless/CI and multi-region verification point the binary
     * at a specific region ROM without depending on the exe-dir/cwd probe order,
     * and lets users keep their ROM outside the default locations. */
    {
        const char* override = getenv("TMC_BASEROM");
        if (override && override[0]) {
            FILE* of = fopen(override, "rb");
            if (of) {
                fclose(of);
                snprintf(sFoundPath, sizeof(sFoundPath), "%s", override);
                return sFoundPath;
            }
            fprintf(stderr, "TMC_BASEROM='%s' not openable; using probe list.\n", override);
            sFoundPath[0] = '\0';
        }
    }
    FILE* f = NULL;
    int preferred = Port_Config_PreferredRegion();
    if (preferred == 0 /* USA */) {
        f = TryOpenRom(kRomCandidates_USA, (int)(sizeof(kRomCandidates_USA) / sizeof(kRomCandidates_USA[0])),
                       sFoundPath, (int)sizeof(sFoundPath));
    } else if (preferred == 1 /* EU */) {
        f = TryOpenRom(kRomCandidates_EU, (int)(sizeof(kRomCandidates_EU) / sizeof(kRomCandidates_EU[0])), sFoundPath,
                       (int)sizeof(sFoundPath));
    } else if (preferred == 2 /* JP */) {
        f = TryOpenRom(kRomCandidates_JP, (int)(sizeof(kRomCandidates_JP) / sizeof(kRomCandidates_JP[0])), sFoundPath,
                       (int)sizeof(sFoundPath));
    }
    if (!f) {
        f = TryOpenRom(kRomCandidates, ROM_CANDIDATE_COUNT, sFoundPath, (int)sizeof(sFoundPath));
    }
    if (!f)
        return NULL;
    fclose(f);
    return sFoundPath;
}

/* ------------------------------------------------------------------ */
/*  ROM page extraction (4 KB pages)                                  */
/* ------------------------------------------------------------------ */
#define ROM_EXTRACT_DIR "rom_data"
#define ROM_PAGE_SHIFT 12
#define ROM_PAGE_SIZE (1u << ROM_PAGE_SHIFT)              /* 4096 */
#define ROM_EXPECTED_SIZE 0x1000000u                      /* 16 MB USA ROM */
#define ROM_MAX_PAGES (ROM_EXPECTED_SIZE / ROM_PAGE_SIZE) /* 4096 */

/* Bitfield: 1 = page already extracted this session */
static u8 sExtractedPages[ROM_MAX_PAGES / 8];

static void MarkPageExtracted(u32 page) {
    sExtractedPages[page / 8] |= (u8)(1 << (page % 8));
}
static int IsPageExtracted(u32 page) {
    return (sExtractedPages[page / 8] >> (page % 8)) & 1;
}

static void EnsureExtractDir(void) {
#ifdef _WIN32
    _mkdir(ROM_EXTRACT_DIR);
#else
    mkdir(ROM_EXTRACT_DIR, 0755);
#endif
}

/* Extract a single 4 KB page to rom_data/XXXXXXXX.bin */
static void ExtractPage(u32 page) {
    if (page >= ROM_MAX_PAGES || !gRomData)
        return;
    if (IsPageExtracted(page))
        return;
    MarkPageExtracted(page);

    u32 offset = page << ROM_PAGE_SHIFT;
    u32 size = ROM_PAGE_SIZE;
    if (offset + size > gRomSize)
        size = gRomSize - offset;
    if (size == 0)
        return;

    EnsureExtractDir();

    char path[256];
    snprintf(path, sizeof(path), ROM_EXTRACT_DIR "/%08X.bin", offset);

    /* Don't overwrite if file already exists with correct size */
    FILE* chk = fopen(path, "rb");
    if (chk) {
        fseek(chk, 0, SEEK_END);
        long existing = ftell(chk);
        fclose(chk);
        if ((u32)existing == size)
            return;
    }

    FILE* f = fopen(path, "wb");
    if (f) {
        const size_t wrote = fwrite(&gRomData[offset], 1, size, f);
        const int closed = fclose(f);
        if (wrote != size || closed != 0) {
            fprintf(stderr, "WARNING: short write extracting %s (%zu/%u bytes); removing\n", path, wrote, size);
            remove(path);
        }
    }
}

/* Extract all pages covering [rom_offset .. rom_offset+size) */
static void ExtractRegion(u32 rom_offset, u32 size) {
    if (!gRomData || size == 0)
        return;
    u32 first_page = rom_offset >> ROM_PAGE_SHIFT;
    u32 last_page = (rom_offset + size - 1) >> ROM_PAGE_SHIFT;
    for (u32 p = first_page; p <= last_page && p < ROM_MAX_PAGES; p++)
        ExtractPage(p);
}

/* Load rom_data/*.bin files from a specific directory into gRomData.
 * Returns the number of pages loaded. */
static int LoadExtractedPagesFrom(const char* dir) {
    int loaded = 0;

    /* Allocate gRomData if not yet done */
    if (!gRomData) {
        gRomSize = ROM_EXPECTED_SIZE;
        gRomData = AllocateRomBuffer(gRomSize, 1);
        if (!gRomData) {
            fprintf(stderr, "ERROR: Failed to allocate %u bytes for ROM buffer\n", gRomSize);
            return 0;
        }
    }

#ifdef _WIN32
    char pattern[4096 + 16];
    if (snprintf(pattern, sizeof(pattern), "%s\\*.bin", dir) >= (int)sizeof(pattern)) {
        fprintf(stderr, "WARNING: rom_data dir path too long, skipping: %s\n", dir);
        return 0;
    }
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;
    do {
        u32 offset = 0;
        if (sscanf(fd.cFileName, "%08X.bin", &offset) != 1)
            continue;
        if (offset >= gRomSize)
            continue;

        char path[4096 + 280];
        if (snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName) >= (int)sizeof(path)) {
            fprintf(stderr, "WARNING: rom_data path too long, skipping: %s\n", fd.cFileName);
            continue;
        }
        FILE* f = fopen(path, "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        long ftellRes = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (ftellRes < 0) {
            fclose(f);
            continue;
        }
        /* Clamp in 64-bit: `offset + fsize` wraps in u32 for a >=4 GB file,
         * which would defeat the bound and overflow gRomData. offset is
         * already known < gRomSize. */
        u64 avail = (u64)gRomSize - offset;
        u32 fsize = (u64)ftellRes > avail ? (u32)avail : (u32)ftellRes;
        const size_t got = fread(&gRomData[offset], 1, fsize, f);
        fclose(f);
        if (got != fsize) {
            fprintf(stderr, "WARNING: short read on %s (%zu/%u bytes); skipping\n", path, got, fsize);
            continue;
        }

        /* Mark pages as extracted so we don't re-write them */
        u32 first_page = offset >> ROM_PAGE_SHIFT;
        u32 last_page = (offset + fsize - 1) >> ROM_PAGE_SHIFT;
        for (u32 p = first_page; p <= last_page; p++)
            MarkPageExtracted(p);
        loaded++;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* dirp = opendir(dir);
    if (!dirp)
        return 0;
    struct dirent* ent;
    while ((ent = readdir(dirp)) != NULL) {
        u32 offset = 0;
        if (sscanf(ent->d_name, "%08X.bin", &offset) != 1)
            continue;
        if (offset >= gRomSize)
            continue;

        char path[4096 + 280];
        if (snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name) >= (int)sizeof(path)) {
            fprintf(stderr, "WARNING: rom_data path too long, skipping: %s\n", ent->d_name);
            continue;
        }
        FILE* f = fopen(path, "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        long ftellRes = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (ftellRes < 0) {
            fclose(f);
            continue;
        }
        /* Clamp in 64-bit: `offset + fsize` wraps in u32 for a >=4 GB file,
         * which would defeat the bound and overflow gRomData. offset is
         * already known < gRomSize. */
        u64 avail = (u64)gRomSize - offset;
        u32 fsize = (u64)ftellRes > avail ? (u32)avail : (u32)ftellRes;
        const size_t got = fread(&gRomData[offset], 1, fsize, f);
        fclose(f);
        if (got != fsize) {
            fprintf(stderr, "WARNING: short read on %s (%zu/%u bytes); skipping\n", path, got, fsize);
            continue;
        }

        u32 first_page = offset >> ROM_PAGE_SHIFT;
        u32 last_page = (offset + fsize - 1) >> ROM_PAGE_SHIFT;
        for (u32 p = first_page; p <= last_page; p++)
            MarkPageExtracted(p);
        loaded++;
    }
    closedir(dirp);
#endif

    return loaded;
}

/* Try multiple rom_data directories and return total pages loaded */
static int LoadExtractedPages(void) {
    const char* dirs[] = {
        ROM_EXTRACT_DIR,  /* rom_data/ (cwd) */
        "../../rom_data", /* project root from build/pc/ */
    };
    int total = 0;
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        int n = LoadExtractedPagesFrom(dirs[i]);
        if (n > 0) {
            fprintf(stderr, "  rom_data: %d pages from %s\n", n, dirs[i]);
            total += n;
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/*  ROM access logging — now also extracts the touched page           */
/* ------------------------------------------------------------------ */
void Port_LogRomAccess(u32 gba_addr, const char* caller) {
#ifdef TMC_3DS
    (void)gba_addr;
    (void)caller;
    return;
#else
    if (gba_addr < 0x08000000u)
        return;
    u32 offset = gba_addr - 0x08000000u;
    u32 page = offset >> ROM_PAGE_SHIFT;
    if (page < ROM_MAX_PAGES && !IsPageExtracted(page)) {
        fprintf(stderr, "[ROM] New page %08X accessed from %s\n", page << ROM_PAGE_SHIFT, caller ? caller : "?");
        fflush(stderr);
    }
    ExtractPage(page);
#endif
}

void Port_PrintRomAccessSummary(void) {
#ifdef TMC_3DS
    fprintf(stderr, "\n[ROM] Full ROM is resident in memory; SD page extraction is disabled.\n");
#else
    int count = 0;
    for (u32 p = 0; p < ROM_MAX_PAGES; p++) {
        if (IsPageExtracted(p))
            count++;
    }
    fprintf(stderr, "\n[ROM] Summary: %d pages (4 KB each, %d KB total) extracted to " ROM_EXTRACT_DIR "/\n", count,
            count * 4);
#endif
    fflush(stderr);
}

/* ------------------------------------------------------------------ */
/*  ROM region detection & offset tables (USA / EU)                   */
/* ------------------------------------------------------------------ */
RomRegion gRomRegion = ROM_REGION_UNKNOWN;

#if defined(PC_PORT) && defined(MULTI_REGION)
/* Runtime region identity read by the REGION_IS_* macros in region.h. Defaults
 * to USA until Port_DetectRomRegion publishes the loaded ROM's region. */
#include "region.h"
int gActiveRegion = TMC_REGION_USA;
#endif
const RomOffsets* gRomOffsets = NULL;

/* USA offsets (from build/USA/tmc.map) */
const RomOffsets kRomOffsets_USA = {
    .gfxAndPalettes = 0x5A2E80,
    .gfxGroups = 0x100AA8,
    .paletteGroups = 0x0FF850,
    .objPalettes = 0x133368,
    .frameObjLists = 0x2F3D74,
    .extraFrameOffsets = 0x9FB770,
    .fixedTypeGfx = 0x132B30,
    .spritePtrs = 0x0029B4,
    .collisionMatrix = 0x0B7B74,
    .collisionShapePtrs = PORT_COLLISION_SHAPE_PTRS_USA,
    .tileTypeProperties = PORT_TILE_TYPE_PROPERTIES_USA,
    .figurines = 0x1281A8,
    .fuserEnemyData = 0x00232E,
    .fuserNpcData = 0x002342,
    .fusionTextPtrs = PORT_FUSION_TEXT_PTRS_USA,
    .fuserFusionPtrs = PORT_FUSER_FUSION_PTRS_USA,
    .lakeHyliaEnemies = 0x0F3D44,
    .lakeHyliaCleared = 0x0F3EA4,
    .lilypadRails = 0x0FED98,
    .songTable = 0xA11DBC,
    .translations = 0x109214,
    .text09230 = 0x109230,
    .text09244 = 0x109244,
    .text09248 = 0x109248,
    .text0926C = 0x10926C,
    .text092AC = 0x1092AC,
    .text092D4 = 0x1092D4,
    .text0942E = 0x10942E,
    .text094CE = 0x1094CE,
    .uiData = 0x0C9044,
    .fadeData = 0x000F54,
    .overlaySizeTable = 0x0B2BE8,
    .mapDataBase = 0x324AE4,
    .areaRoomHeaders = 0x11E214,
    .areaTileSets = 0x10246C,
    .areaTileSetsCount = 0x90,
    .areaRoomMaps = 0x107988,
    .areaTable = 0x0D50FC,
    .areaTiles = 0x10309C,
    .exitLists = 0x13A7F0,
    .bgAnimTable = 0x0B755C,
    .localFlagBanks = 0x11E454,
    .townspersonSpriteLoadPtrs = 0x10B6EC,
    .guardPatrolData = 0x10F6BC,
    .innWestEntities = 0x0D6A74,
    .innMiddleEntities = 0x0D6B18,
    .innEastEntities = 0x0D6BB8,
    .simonEntityLists = 0x0F0CB8,
    .simonEnemyPatterns = 0x0F0D58,
    .simonChestPatterns = 0x0F0E08,
    .gustJarAnimTable = 0x132714,
    .gustJarHitbox = 0x132B28,
    .gfxGroupsCount = 133,
    .paletteGroupsCount = 208,
    .objPalettesCount = 360,
    .frameObjListsSize = 200045,
    .fixedTypeGfxCount = 527,
    .spritePtrsCount = 329,
    .expectedRomSize = 0x1000000,
    .gameCode = "BZME",
};

/* EU offsets (from build/EU/tmc_eu.map) */
const RomOffsets kRomOffsets_EU = {
    .gfxAndPalettes = 0x5A23D0,
    .gfxGroups = 0x100204,
    .paletteGroups = 0x0FED88,
    .objPalettes = 0x1329B4,
    .frameObjLists = 0x2F3460,
    .extraFrameOffsets = 0xB07080,
    .fixedTypeGfx = 0x132180,
    .spritePtrs = 0x002A5C,
    .collisionMatrix = 0x0B729C,
    .collisionShapePtrs = PORT_COLLISION_SHAPE_PTRS_EU,
    .tileTypeProperties = PORT_TILE_TYPE_PROPERTIES_EU,
    .figurines = 0x1278E0,
    .fuserEnemyData = 0x0023D6,
    .fuserNpcData = 0x0023EA,
    .fusionTextPtrs = PORT_FUSION_TEXT_PTRS_EU,
    .fuserFusionPtrs = PORT_FUSER_FUSION_PTRS_EU,
    .lakeHyliaEnemies = 0x0F3368,
    .lakeHyliaCleared = 0x0F34C8,
    .lilypadRails = 0x0FE2DC,
    .songTable = 0xB1D414,
    .translations = 0x108968,
    .text09230 = 0x108984,
    .text09244 = 0x108998,
    .text09248 = 0x10899C,
    .text0926C = 0x1089C0,
    .text092AC = 0x108A00,
    .text092D4 = 0x108A28,
    .text0942E = 0x108B82,
    .text094CE = 0x108C22,
    .uiData = 0x0C876C,
    .fadeData = 0x000F9C,
    /* ram_0x80b2be8 is copied from a different position in the EU ROM.
     * 0x0B25E8 is an adjacent function-pointer table, not OBJ geometry;
     * treating those pointer bytes as anchors clips and displaces nearly every
     * multipart sprite.  The 240-byte EU size table is at 0x0B2310 and is
     * byte-identical to the USA table. */
    .overlaySizeTable = 0x0B2310,
    .mapDataBase = 0x323FEC,
    .areaRoomHeaders = 0x11D95C,
    .areaTileSets = 0x101BC8,
    .areaTileSetsCount = 0x90,
    .areaRoomMaps = 0x1070E4,
    .areaTable = 0x0D4828,
    .areaTiles = 0x1027F8,
    .exitLists = 0x139EDC,
    .bgAnimTable = 0x0B6C84,
    .localFlagBanks = 0x11DB9C,
    .townspersonSpriteLoadPtrs = 0x10AE40,
    .guardPatrolData = 0x10EE10,
    .innWestEntities = 0x0D61D0,
    .innMiddleEntities = 0x0D6274,
    .innEastEntities = 0x0D6314,
    .simonEntityLists = 0x0F02EC,
    .simonEnemyPatterns = 0x0F038C,
    .simonChestPatterns = 0x0F043C,
    .gustJarAnimTable = 0x131D64,
    .gustJarHitbox = 0x132178,
    .gfxGroupsCount = 133,
    .paletteGroupsCount = 207,
    .objPalettesCount = 360,
    .frameObjListsSize = 200045,
    .fixedTypeGfxCount = 527,
    .spritePtrsCount = 329,
    .expectedRomSize = 0x1000000,
    .gameCode = "BZMP",
};

/*
 * JP (BZMJ) offsets — derived from the retail USA + JP ROMs by content-anchoring
 * (the USA addresses are known-correct, so each USA table is located in the JP
 * ROM). Method + evidence per field in docs/JP_PORT_ENABLEMENT.md. In brief:
 *   - direct table-content match (unique 64B signature) for version-stable tables;
 *   - pointer-table dereference / shift-search where the whole 0x08xxxxxx pointer
 *     array is consistent under one shift;
 *   - the text/translations cluster's uniform -0x33C shift, fixed by 4 anchors.
 * Regional shifts grow monotonically with address (0 -> -0x260 -> ~-0x338 ->
 * -0x33C -> -0x3D4), each value corroborated by its neighbours, and the whole
 * table is validated by booting the JP build against the retail JP ROM.
 *
 * NOTE: this tree's decomp ROM build is non-matching (port edits shift symbols),
 * so build/JP/tmc_jp.map is NOT a valid source here — these come straight from
 * the retail ROMs instead. Count/size fields are content-invariant (identical
 * across USA and EU) and carried over.
 */
const RomOffsets kRomOffsets_JP = {
    .gfxAndPalettes = 0x5A2B20,
    .gfxGroups = 0x100770,
    .paletteGroups = 0xFF500,
    .objPalettes = 0x132F94,
    .frameObjLists = 0x2F39A0,
    .extraFrameOffsets = 0,
    .fixedTypeGfx = 0x13275C,
    .spritePtrs = 0x29B4,
    .collisionMatrix = 0,
    .collisionShapePtrs = 0,
    .tileTypeProperties = 0,
    .figurines = 0,
    .fuserEnemyData = 0,
    .fuserNpcData = 0,
    .fusionTextPtrs = 0,
    .fuserFusionPtrs = 0,
    .lakeHyliaEnemies = 0,
    .lakeHyliaCleared = 0,
    .lilypadRails = 0,
    .songTable = 0,
    .translations = 0x108ED8,
    .text09230 = 0x108EF4,
    .text09244 = 0x108F08,
    .text09248 = 0x108F0C,
    .text0926C = 0x108F30,
    .text092AC = 0x108F70,
    .text092D4 = 0x108F98,
    .text0942E = 0x1090F2,
    .text094CE = 0x109192,
    .uiData = 0xC8DE4,
    .fadeData = 0xF54,
    .overlaySizeTable = 0xB2988,
    .mapDataBase = 0x324710,
    .areaRoomHeaders = 0x11DED8,
    .areaTileSets = 0x102134,
    .areaTileSetsCount = 0x90,
    .areaRoomMaps = 0x107650,
    .areaTable = 0xD4E9C,
    .areaTiles = 0x102D64,
    .exitLists = 0x13A41C,
    .bgAnimTable = 0xB72FC,
    .localFlagBanks = 0x11E118,
    .townspersonSpriteLoadPtrs = 0x10B3B0,
    .guardPatrolData = 0,
    .innWestEntities = 0,
    .innMiddleEntities = 0,
    .innEastEntities = 0,
    .simonEntityLists = 0,
    .simonEnemyPatterns = 0,
    .simonChestPatterns = 0,
    .gustJarAnimTable = 0,
    .gustJarHitbox = 0,
    .gfxGroupsCount = 133,
    .paletteGroupsCount = 208,
    .objPalettesCount = 360,
    .frameObjListsSize = 200045,
    .fixedTypeGfxCount = 527,
    .spritePtrsCount = 329,
    .expectedRomSize = 0x1000000,
    .gameCode = "BZMJ",
};

/* Accessor for the active region's townsperson SpriteLoadData* table offset
 * (gUnk_0810B6EC). Lets the autogenerated data_stubs_autogen.c resolve the table
 * region-aware without including the RomOffsets struct definition. Falls back to
 * the USA offset before region detection has run. */
u32 Port_TownspersonSpriteLoadPtrsOffset(void) {
    return gRomOffsets ? gRomOffsets->townspersonSpriteLoadPtrs : 0x10B6ECu;
}

u32 Port_CollisionMatrixOffset(void) {
    return (gRomOffsets && gRomOffsets->collisionMatrix) ? gRomOffsets->collisionMatrix : 0x0B7B74u;
}

const u16* Port_GetCollisionShapeData(u32 index) {
    if (gRomOffsets == NULL || gRomOffsets->collisionShapePtrs == 0) {
        return NULL;
    }
    return Port_ResolveCollisionShapeFromRom(gRomData, gRomSize, gRomOffsets->collisionShapePtrs, index);
}

u16 Port_GetTileTypeProperty(u32 tileType) {
    if (gRomOffsets == NULL || gRomOffsets->tileTypeProperties == 0) {
        return 0;
    }
    return Port_ReadTileTypePropertyFromRom(gRomData, gRomSize, gRomOffsets->tileTypeProperties, tileType);
}

RomRegion Port_DetectRomRegion(const u8* romData, u32 romSize) {
    if (!romData || romSize < 0xB0)
        return ROM_REGION_UNKNOWN;

    if (memcmp(&romData[0xAC], "BZME", 4) == 0) {
        gRomRegion = ROM_REGION_USA;
        gRomOffsets = &kRomOffsets_USA;
        fprintf(stderr, "ROM region detected: USA (BZME)\n");
    } else if (memcmp(&romData[0xAC], "BZMP", 4) == 0) {
        gRomRegion = ROM_REGION_EU;
        gRomOffsets = &kRomOffsets_EU;
        fprintf(stderr, "ROM region detected: EU (BZMP)\n");
    } else if (memcmp(&romData[0xAC], "BZMJ", 4) == 0) {
        gRomRegion = ROM_REGION_JP;
        fprintf(stderr, "ROM region detected: JP (BZMJ)\n");
#if defined(JP) || defined(MULTI_REGION)
        /* JP binary (or fat multi-region binary): use JP offsets, but refuse to
         * proceed if the table is still the unpopulated placeholder. */
        if (kRomOffsets_JP.gfxAndPalettes == 0) {
            Port_FatalRomError("Minish Cap PC Port - JP not yet supported",
                               "This is a Japanese (BZMJ) ROM, but this build's JP data tables "
                               "are not populated yet.\n\n"
                               "Use a USA (BZME) or EU (BZMP) ROM for now. See "
                               "docs/JP_PORT_ENABLEMENT.md for the JP status.");
        }
        gRomOffsets = &kRomOffsets_JP;
#else
        /* JP ROM fed to a single-region non-JP binary: code/data version
         * mismatch. Keep USA offsets so the region cross-check in port_main.c
         * reports the mismatch instead of crashing on the JP table. */
        fprintf(stderr, "WARNING: JP ROM in a non-JP binary — rebuild with --game_version=JP.\n");
        gRomOffsets = &kRomOffsets_USA;
#endif
    } else {
        fprintf(stderr, "WARNING: Unknown ROM game code '%.4s'. Defaulting to USA offsets.\n", &romData[0xAC]);
        gRomRegion = ROM_REGION_USA;
        gRomOffsets = &kRomOffsets_USA;
    }
#if defined(PC_PORT) && defined(MULTI_REGION)
    /* Fat binary: publish the detected region to the runtime REGION_IS_* macros
     * so the converted gameplay-behavior branches follow the loaded ROM. */
    gActiveRegion = (gRomRegion == ROM_REGION_EU)   ? TMC_REGION_EU
                    : (gRomRegion == ROM_REGION_JP) ? TMC_REGION_JP
                                                    : TMC_REGION_USA;
    fprintf(stderr, "Active region set to %s (multi-region binary).\n",
            gActiveRegion == TMC_REGION_EU   ? "EU"
            : gActiveRegion == TMC_REGION_JP ? "JP"
                                             : "USA");

    /* Asset/offset baseline vs loaded ROM (issue #3).
     *
     * A MULTI_REGION binary detects the ROM at runtime, but it is still
     * COMPILED against one region's baseline: the -DUSA/-DEU/-DJP define picked
     * by --game_version, and with it build/<version>/assets/{map,gfx}_offsets.h
     * plus every `#ifdef EU` in src/ that has not yet been converted to the
     * runtime REGION_IS_* form. Divergent compiled blob offsets are translated
     * at the known call sites (Port_Remap{Map,Gfx}Offset), but the unconverted
     * preprocessor sites are not — those silently take the baseline region's
     * behaviour whatever ROM is loaded.
     *
     * So "baseline != ROM" is a supported-but-hybrid configuration, not a fatal
     * one, and the port must not pretend otherwise. Log both identities on one
     * line: a bug report that opens with a black screen or wrong graphics is
     * then one logcat grep away from showing which combination produced it,
     * instead of leaving the reader to guess how the APK was built. */
    {
#if defined(JP)
        const char* baseline = "JP";
        const RomRegion baselineRegion = ROM_REGION_JP;
#elif defined(EU)
        const char* baseline = "EU";
        const RomRegion baselineRegion = ROM_REGION_EU;
#else
        const char* baseline = "USA";
        const RomRegion baselineRegion = ROM_REGION_USA;
#endif
        const char* detected = gRomRegion == ROM_REGION_EU   ? "EU"
                               : gRomRegion == ROM_REGION_JP ? "JP"
                                                             : "USA";
        if (gRomRegion == baselineRegion) {
            fprintf(stderr, "Region baseline: build=%s rom=%s (matched).\n", baseline, detected);
        } else {
            fprintf(stderr,
                    "WARNING: Region baseline: build=%s rom=%s (MISMATCH). This binary plays %s ROMs, but its "
                    "compiled asset/offset baseline is %s, so region-specific code paths that are still "
                    "compile-time follow %s. Build with --game_version=%s for a fully %s port.\n",
                    baseline, detected, detected, baseline, baseline, detected, detected);
        }
    }
#endif
    return gRomRegion;
}

/* Max table sizes (for static arrays) */
#define GFX_GROUPS_COUNT_MAX 133
#define PALETTE_GROUPS_COUNT_MAX 208
#define AREA_COUNT 0x90 /* 144 areas (0x00..0x8F) */
#define MORE_SPRITE_PTRS_COUNT 16
#define SPRITE_ANIM_322_COUNT 128

extern u32 gFrameObjLists[];
/* gUnk_08133368 now const — provided by src/data/objPalettes.c */
extern SpritePtr gSpritePtrs[];
extern u32 gFixedTypeGfxData[];
extern u16* gMoreSpritePtrs[MORE_SPRITE_PTRS_COUNT];
extern Frame* gSpriteAnimations_322[SPRITE_ANIM_322_COUNT];
extern void Port_LoadOverlayData(const u8* romData, u32 romSize, u32 overlayOffset);

/* ---- Compile-time ROM tables (port_rom_tables.c) ---- */
extern const u8 kFrameObjListsData[];
extern const u8 kFixedTypeGfxInitData[];
extern const u8 kOverlaySizeData[];
extern const u8 kFontText09244Data[];
extern const u8 kFontText0926CData[];
extern const u8 kFontText092D4Data[];
extern const u8 kFontText0942EData[];
extern const u8 kFontText094CEData[];
extern const u8 kUiInitData[];
extern const u32 kSpritePtrEntries[][4];
extern const u32 kAreaRoomHeaderOffsets[];
extern const u32 kAreaTileSetOffsets[];
extern const u32 kAreaRoomMapOffsets[];
extern const u32 kAreaTableOffsets[];
extern const u32 kAreaTilesOffsets[];
extern const u32 kTranslationOffsets[];
extern const u32 kUnk09230Offsets[];
extern const u32 kUnk09248Offsets[];
extern const u32 kUnk092ACOffsets[];

/* Helper: resolve an offset from a compile-time offset table.
 * 0xFFFFFFFF means NULL. */
static inline void* ResolveTableOffset(u32 offset) {
    if (offset == 0xFFFFFFFF || !gRomData)
        return NULL;
    if (offset < gRomSize)
        return &gRomData[offset];
    return NULL;
}

/* Area / room data tables (port_linked_stubs.c) */
extern RoomHeader* gAreaRoomHeaders[];
extern void* gAreaRoomMaps[];
extern void* gAreaTable[];
extern void* gAreaTileSets[];
extern void* gAreaTiles[];
/* gExitLists now const — provided by src/data/transitions.c */

/*
 * Shadow arrays for second-level ROM pointer resolution.
 * On GBA, sub-arrays inside gAreaTileSets[area], gAreaRoomMaps[area],
 * gAreaTable[area], and gExitLists[area] contain packed 32-bit GBA ROM
 * pointers. On 64-bit PC, sizeof(void*)==8, so we can't dereference them
 * directly. These shadow arrays hold pre-resolved native pointers.
 */
static void* sTileSetsResolved[AREA_COUNT][MAX_ROOMS];
static void* sRoomMapsResolved[AREA_COUNT][MAX_ROOMS];
static void* sAreaTableResolved[AREA_COUNT][MAX_ROOMS];
/* sExitListsResolved removed — gExitLists now compile-time const from transitions.c */

bool32 Port_IsAreaTablePtrReadable(u32 area, const void* ptr) {
    uintptr_t at;
    uintptr_t romStart;
    uintptr_t romEnd;
    const uintptr_t packedTableBytes = MAX_ROOMS * sizeof(u32);

    if (area >= AREA_COUNT || ptr == NULL) {
        return FALSE;
    }

    if (gRomData != NULL && gRomSize >= packedTableBytes) {
        at = (uintptr_t)ptr;
        romStart = (uintptr_t)gRomData;
        romEnd = romStart + (uintptr_t)gRomSize;
        if (at >= romStart && at <= romEnd - packedTableBytes) {
            return TRUE;
        }
    }

    if (ptr == (const void*)sAreaTableResolved[area]) {
        return TRUE;
    }

    return Port_IsAreaTablePtrFromAssets(area, ptr);
}

/* Forward declaration */
static inline void* ResolveRomPtr(u32 gba_addr);

/* Read a GBA little-endian u16/u32 from a ROM byte pointer, host-endian-safe.
 * GBA ROM data is little-endian; a native u16/u32 load byte-swaps it on a
 * big-endian host (N64). Reading byte-by-byte in LE order is correct on both
 * little-endian PC and big-endian N64. */
static inline u32 RomLE32(const void* p) {
    /* Use a 32-bit load: N64 CPU byte reads (lbu) from the cart are corrupted
     * (odd 16-bit halfwords dropped), but 32-bit loads (lw) work. The lw returns
     * the GBA little-endian bytes in big-endian order on N64, so byte-swap. */
    u32 v = *(const u32*)p;
#ifdef TMC_N64
    v = __builtin_bswap32(v);
#endif
    return v;
}

/* Resolve a ROM sub-table of 32-bit GBA pointers into a native pointer array. */
static void ResolveSubTable(void* romBase, void** dest, u32 count) {
    u8* base = (u8*)romBase;
    for (u32 j = 0; j < count; j++) {
        u32 ptr = RomLE32(base + j * 4);
        dest[j] = ResolveRomPtr(ptr);
    }
}

static u32 ScanSubArrayCount(const void* base) {
    const u8* bytes = (const u8*)base;
    u32 count = 0;

    if (bytes == NULL) {
        return 0;
    }

    for (u32 i = 0; i < MAX_ROOMS; i++) {
        u32 value = RomLE32(bytes + i * 4);
        if (value == 0 || (value >= 0x08000000u && value < 0x08000000u + gRomSize)) {
            count = i + 1;
        } else {
            break;
        }
    }

    return count;
}

void Port_RefreshAreaData(u32 area) {
    void* tileSetBase;
    void* roomMapBase;
    void* areaTableBase;
    u32 subCount;

    if (area >= AREA_COUNT) {
        return;
    }

    if (Port_RefreshAreaDataFromAssets(area)) {
        return;
    }

    if (gRomData == NULL || gRomOffsets == NULL) {
        return;
    }

    gAreaRoomHeaders[area] = (RoomHeader*)Port_UnpackRomDataPtr(&gRomData[gRomOffsets->areaRoomHeaders], area);
    gAreaTiles[area] = Port_UnpackRomDataPtr(&gRomData[gRomOffsets->areaTiles], area);

    tileSetBase = Port_UnpackRomDataPtr(&gRomData[gRomOffsets->areaTileSets], area);
    gAreaTileSets[area] = tileSetBase;
    memset(sTileSetsResolved[area], 0, sizeof(sTileSetsResolved[area]));
    subCount = ScanSubArrayCount(tileSetBase);
    if (subCount > 0) {
        ResolveSubTable(tileSetBase, sTileSetsResolved[area], subCount);
        gAreaTileSets[area] = sTileSetsResolved[area];
    }

    roomMapBase = Port_UnpackRomDataPtr(&gRomData[gRomOffsets->areaRoomMaps], area);
    gAreaRoomMaps[area] = roomMapBase;
    memset(sRoomMapsResolved[area], 0, sizeof(sRoomMapsResolved[area]));
    subCount = ScanSubArrayCount(roomMapBase);
    if (subCount > 0) {
        ResolveSubTable(roomMapBase, sRoomMapsResolved[area], subCount);
        gAreaRoomMaps[area] = sRoomMapsResolved[area];
    }

    areaTableBase = Port_UnpackRomDataPtr(&gRomData[gRomOffsets->areaTable], area);
    gAreaTable[area] = areaTableBase;
    memset(sAreaTableResolved[area], 0, sizeof(sAreaTableResolved[area]));
    subCount = ScanSubArrayCount(areaTableBase);
    if (subCount > 0) {
        ResolveSubTable(areaTableBase, sAreaTableResolved[area], subCount);
        gAreaTable[area] = sAreaTableResolved[area];
    }

    fprintf(stderr, "[AREA] refreshed area data area=%u headers=%p tilesets=%p roomMaps=%p table=%p tiles=%p\n", area,
            (void*)gAreaRoomHeaders[area], gAreaTileSets[area], gAreaRoomMaps[area], gAreaTable[area],
            gAreaTiles[area]);
}

/* Font data tables (data_stubs_autogen.c) */
extern void* gTextVariableSources[];
extern u8 gUnk_08109244[];
extern u32* gTranslations[];
extern void* gUnk_08109248[];
extern u8 gUnk_0810926C[];
extern void* gUnk_081092AC[];
extern u8 gUnk_081092D4[];
extern u8 gUnk_0810942E[];
extern u8 gUnk_081094CE[];
#ifdef PC_PORT
extern u8 gUnk_020227DC[];
extern u8 gUnk_020227F0[];
extern u8 gUnk_020227F8[];
extern u8 gUnk_02022800[];
extern struct_020227E8 gUnk_020227E8[];
#endif

/* Resolved pointer tables */
const u8* gGlobalGfxAndPalettes = NULL;
const void* gGfxGroups[GFX_GROUPS_COUNT_MAX];
const void* gPaletteGroups[PALETTE_GROUPS_COUNT_MAX];

/* Helper: resolve a 32-bit GBA ROM pointer to native */
static inline void* ResolveRomPtr(u32 gba_addr) {
    if (gba_addr == 0)
        return NULL;
    if (gba_addr >= 0x08000000u && gba_addr < 0x08000000u + gRomSize)
        return &gRomData[gba_addr - 0x08000000u];
    fprintf(stderr, "ResolveRomPtr: address 0x%08X is outside ROM\n", gba_addr);
    return NULL;
}

/* Read a packed 32-bit GBA ROM pointer at base + index*4.
 * Returns resolved native pointer for data, NULL for function pointers. */
void* Port_ReadPackedRomPtr(const void* base, u32 index) {
    if (!base) {
        fprintf(stderr, "Port_ReadPackedRomPtr: base is NULL (index=%u)\n", index);
        return NULL;
    }
    /* base may point into gRomData (direct ROM) OR into a PC `.rodata` stub
     * created by data_const_stubs.c — the stub holds verbatim ROM bytes for
     * symbols that aren't resolved through gRomData at runtime.  Either way,
     * read 4 bytes at base + index*4 and treat as a GBA ROM pointer. */
    if (gRomData && (const u8*)base >= gRomData && (const u8*)base < gRomData + gRomSize) {
        const u8* readPtr = (const u8*)base + index * 4;
        if (readPtr + 4 > gRomData + gRomSize) {
            fprintf(stderr, "Port_ReadPackedRomPtr: read at %p+%u*4 would exceed ROM bounds [%p..%p] (index=%u)\n",
                    base, index, (void*)gRomData, (void*)(gRomData + gRomSize), index);
            return NULL;
        }
    }
    /* For PC `.rodata` stubs we have no size info — trust the caller. */

    const u8* readPtr = (const u8*)base + index * 4;
    u32 raw = RomLE32(readPtr);
    if (raw == 0)
        return NULL;
    raw &= ~1u;
    return ResolveRomPtr(raw);
}

/* Read a packed-pointer table whose base is selected from RomOffsets for the
 * active retail ROM. Unlike Port_ReadPackedRomPtr on a compiled `.rodata`
 * stub, both the table and its entries therefore have regional provenance. */
void* Port_ReadActiveRomPtrTable(u32 tableOffset, u32 index) {
    if (tableOffset == 0) {
        return NULL;
    }
    return (void*)Port_ResolvePackedRomDataPtrFromRom(gRomData, gRomSize, tableOffset, index, 1u);
}

void* Port_GetFusionTextData(u32 fuserId) {
    if (gRomOffsets == NULL) {
        return NULL;
    }
    return (void*)Port_ResolveFusionTextDataFromRom(gRomData, gRomSize, gRomOffsets->fusionTextPtrs, fuserId);
}

void* Port_GetPairedFusionTextData(u32 fuserId) {
    if (gRomOffsets == NULL) {
        return NULL;
    }
    return (void*)Port_ResolvePairedFusionTextDataFromRom(gRomData, gRomSize, gRomOffsets->fusionTextPtrs, fuserId);
}

void* Port_GetFuserFusionData(u32 fuserId) {
    if (gRomOffsets == NULL) {
        return NULL;
    }
    return (void*)Port_ResolveFuserDataFromRom(gRomData, gRomSize, gRomOffsets->fuserFusionPtrs, fuserId, 6u);
}

static void* Port_ResolveAreaFirstLevelTable(u32 tableOffset, u32 area) {
    if (gRomData == NULL || gRomOffsets == NULL || tableOffset == 0 || area >= AREA_COUNT) {
        return NULL;
    }
    return Port_UnpackRomDataPtr(&gRomData[tableOffset], area);
}

void* Port_ResolveAreaTileSetFromRom(u32 area, u32 tileSetId) {
    void* table = Port_ResolveAreaFirstLevelTable(gRomOffsets ? gRomOffsets->areaTileSets : 0, area);
    return table != NULL ? Port_ReadPackedRomPtr(table, tileSetId) : NULL;
}

void* Port_ResolveAreaRoomMapFromRom(u32 area, u32 room) {
    void* table = Port_ResolveAreaFirstLevelTable(gRomOffsets ? gRomOffsets->areaRoomMaps : 0, area);
    return table != NULL ? Port_ReadPackedRomPtr(table, room) : NULL;
}

void* Port_ResolveAreaPropertiesFromRom(u32 area, u32 room) {
    void* table = Port_ResolveAreaFirstLevelTable(gRomOffsets ? gRomOffsets->areaTable : 0, area);
    return table != NULL ? Port_ReadPackedRomPtr(table, room) : NULL;
}

/* Region-correct room exit list. The compile-time gExitLists in transitions.c is
 * USA-baseline-sized; EU/JP room indices can exceed it (global-buffer-overflow in
 * InitRoomResInfo). Resolve from the active ROM's exit-list table instead. */
void* Port_ResolveAreaExitsFromRom(u32 area, u32 room) {
    void* table = Port_ResolveAreaFirstLevelTable(gRomOffsets ? gRomOffsets->exitLists : 0, area);
    return table != NULL ? Port_ReadPackedRomPtr(table, room) : NULL;
}

/*
 * Port_ResolveEwramPtr — resolve a GBA EWRAM address to a native PC pointer.
 *
 * On GBA, MapLayer (gMapBottom/gMapTop) starts with a 4-byte pointer bgSettings.
 * Native 64-bit builds shift every later field by four bytes; 32-bit targets
 * such as 3DS keep the original offsets. Translate from the actual native
 * layout instead of applying the PC64 delta unconditionally.
 *
 * Known EWRAM globals (same addresses for USA and EU):
 *   0x02025EB0  gMapBottom      (MapLayer, 0xC004 GBA bytes)
 *   0x0200B650  gMapTop         (MapLayer, 0xC004 GBA bytes)
 *   0x02019EE0  gMapDataBottomSpecial
 *   0x02002F00  gMapDataTopSpecial
 */
void* Port_ResolveEwramPtr(u32 gba_addr) {
    /* --- gMapBottom (MapLayer at GBA 0x02025EB0) --- */
    {
        const u32 GBA_BASE = 0x02025EB0u;
        const u32 GBA_SIZE = 0xC004u;
        if (gba_addr >= GBA_BASE && gba_addr < GBA_BASE + GBA_SIZE) {
            u32 gba_off = gba_addr - GBA_BASE;
            return (u8*)&gMapBottom + Port_MapLayerNativeOffset(gba_off);
        }
    }
    /* --- gMapTop (MapLayer at GBA 0x0200B650) --- */
    {
        const u32 GBA_BASE = 0x0200B650u;
        const u32 GBA_SIZE = 0xC004u;
        if (gba_addr >= GBA_BASE && gba_addr < GBA_BASE + GBA_SIZE) {
            u32 gba_off = gba_addr - GBA_BASE;
            return (u8*)&gMapTop + Port_MapLayerNativeOffset(gba_off);
        }
    }
    /* --- gMapDataBottomSpecial at GBA 0x02019EE0 --- */
    {
        const u32 GBA_BASE = 0x02019EE0u;
        if (gba_addr >= GBA_BASE && gba_addr < GBA_BASE + sizeof(gMapDataBottomSpecial)) {
            return (u8*)&gMapDataBottomSpecial + (gba_addr - GBA_BASE);
        }
    }
    /* --- gMapDataTopSpecial at GBA 0x02002F00 --- */
    {
        const u32 GBA_BASE = 0x02002F00u;
        if (gba_addr >= GBA_BASE && gba_addr < GBA_BASE + sizeof(gMapDataTopSpecial)) {
            return (u8*)&gMapDataTopSpecial + (gba_addr - GBA_BASE);
        }
    }
    /* Fallback to generic gba_TryMemPtr for other EWRAM or non-EWRAM addresses */
    return gba_TryMemPtr(gba_addr);
}

/*
 * Port_DecodeFontGBA — decode a 24-byte GBA Font blob into a native Font struct.
 */
extern u8 gTextGfxBuffer[];

void Port_DecodeFontGBA(const void* gba_data, Font* out) {
    const u8* r = (const u8*)gba_data;

    /* Read 32-bit little-endian GBA pointer values */
    u32 dest_gba = r[0] | (r[1] << 8) | (r[2] << 16) | (r[3] << 24);
    u32 gfx_dest_gba = r[4] | (r[5] << 8) | (r[6] << 16) | (r[7] << 24);
    u32 buffer_loc_gba = r[8] | (r[9] << 8) | (r[10] << 16) | (r[11] << 24);

    /* Resolve pointer fields to native addresses */
    out->dest = (u16*)gba_TryMemPtr(dest_gba);
    out->gfx_dest = gba_TryMemPtr(gfx_dest_gba);

    /* gTextGfxBuffer is a standalone array on PC, not inside gEwram.
     * GBA address 0x02000D00 = EWRAM offset 0xD00 = gTextGfxBuffer on GBA. */
    if (buffer_loc_gba == 0x02000D00u) {
        out->buffer_loc = gTextGfxBuffer;
    } else {
        out->buffer_loc = gba_TryMemPtr(buffer_loc_gba);
    }

    /* Non-pointer fields: read from their 32-bit layout offsets */
    out->_c = r[12] | (r[13] << 8) | (r[14] << 16) | (r[15] << 24);
    out->gfx_src = r[16] | (r[17] << 8);
    out->width = r[18];
    out->right_align = (r[19] >> 0) & 1;
    out->sm_border = (r[19] >> 1) & 1;
    out->unused = (r[19] >> 2) & 1;
    out->draw_border = (r[19] >> 3) & 1;
    out->border_type = (r[19] >> 4) & 0xF;
    out->fill_type = r[20];
    out->charColor = r[21];
    out->_16 = r[22];
    out->stylized = r[23];
}

const SpritePtr* Port_GetSpritePtr(u16 sprite_idx) {
    /* Use the runtime-extended count (gSpritePtrsLoadedCount, set
     * after the ROM-walk extension in startup) instead of the
     * compile-time spritePtrsCount=329. Falls back to the compile
     * count for early calls before the extension finished. */
    extern u32 gSpritePtrsLoadedCount;
    if (Port_AreSpritePtrsLoadedFromAssets()) {
        if (!gRomOffsets)
            return NULL;
        const u32 cap = gSpritePtrsLoadedCount ? gSpritePtrsLoadedCount : gRomOffsets->spritePtrsCount;
        if (sprite_idx >= cap)
            return NULL;
        return &gSpritePtrs[sprite_idx];
    }
    if (!gRomOffsets)
        return NULL;
    const u32 cap = gSpritePtrsLoadedCount ? gSpritePtrsLoadedCount : gRomOffsets->spritePtrsCount;
    if (sprite_idx >= cap)
        return NULL;
    return &sSpritePtrsStable[sprite_idx];
}

u16 Port_RemapSpriteIndex(u16 sprite_idx) {
    return Port_RemapFixedUiSpriteIndexForRegion(gRomRegion, sprite_idx);
}

/*
 * Resolve the directory containing the running executable. Lets us look for
 * baserom.gba next to tmc_pc, which is how the release tarball is laid out.
 * Falls back to "" on failure (caller skips exe-dir candidates).
 */
static int GetExeDir(char* out, size_t n) {
    if (!out || n == 0)
        return 0;
    out[0] = '\0';
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf))
        return 0;
    char* slash = strrchr(buf, '\\');
    if (!slash)
        slash = strrchr(buf, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    snprintf(out, n, "%s", buf);
    return 1;
#elif defined(__APPLE__)
    /* macOS: ask once for required size, allocate, then resolve symlinks via
     * realpath() so a tmc_pc.app bundle launch points at the actual binary's
     * directory rather than the launcher symlink. */
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    char* raw = (char*)malloc(size);
    if (!raw)
        return 0;
    if (_NSGetExecutablePath(raw, &size) != 0) {
        free(raw);
        return 0;
    }
    char resolved[4096];
    char* canonical = realpath(raw, resolved);
    free(raw);
    if (!canonical)
        return 0;
    char* slash = strrchr(canonical, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    snprintf(out, n, "%s", canonical);
    return 1;
#else
    char buf[4096];
    long len = readlink("/proc/self/exe", buf, (unsigned long)(sizeof(buf) - 1));
    if (len <= 0)
        return 0;
    buf[(size_t)len] = '\0';
    char* slash = strrchr(buf, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    snprintf(out, n, "%s", buf);
    return 1;
#endif
}

static FILE* TryOpenRom(const char** paths, int count, char* foundPath, int foundPathLen) {
    /* Pass 1: exe_dir/<basename> for any candidate that's a bare filename. */
    char exeDir[4096];
    if (GetExeDir(exeDir, sizeof(exeDir))) {
        for (int i = 0; i < count; i++) {
            const char* p = paths[i];
            if (!p)
                continue;
            /* Only try exe-dir prefix for plain filenames (no path separators).
             * Directory-prefixed candidates ("build/pc/..", "../..") are
             * developer-tree paths and don't make sense beside the binary. */
            if (strchr(p, '/') || strchr(p, '\\'))
                continue;
            char prefixed[4096 + 256];
            if (snprintf(prefixed, sizeof(prefixed), "%s/%s", exeDir, p) >= (int)sizeof(prefixed)) {
                fprintf(stderr, "WARNING: exe-dir ROM path too long, skipping candidate %s\n", p);
                continue;
            }
            FILE* f = fopen(prefixed, "rb");
            if (f) {
                if (foundPath && snprintf(foundPath, foundPathLen, "%s", prefixed) >= foundPathLen)
                    fprintf(stderr, "WARNING: ROM path truncated in foundPath (%s)\n", prefixed);
                return f;
            }
        }
    }

    /* Pass 2: original cwd-relative candidates. */
    for (int i = 0; i < count; i++) {
        FILE* f = fopen(paths[i], "rb");
        if (f) {
            if (foundPath && snprintf(foundPath, foundPathLen, "%s", paths[i]) >= foundPathLen)
                fprintf(stderr, "WARNING: ROM path truncated in foundPath (%s)\n", paths[i]);
            return f;
        }
    }
    return NULL;
}

/*
 * LoadRomGaps — load rom_gaps.bin to fill assembled data regions
 * (pointer tables, GfxItem arrays, etc.) that are NOT in asset files.
 *
 * File format: "GAPD" magic, u32 chunk_count,
 *   then for each chunk: u32 offset, u32 size, u8[size] data.
 *
 * Generated by tools/generate_rom_gaps.py from baserom.gba.
 */
static int LoadRomGaps(void) {
    const char* candidates[] = {
        "rom_gaps.bin",       "build/USA/rom_gaps.bin",       "build/pc/rom_gaps.bin",
        "../../rom_gaps.bin", "../../build/USA/rom_gaps.bin", "../rom_gaps.bin",
    };
    FILE* f = NULL;
    const char* usedPath = NULL;
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        f = fopen(candidates[i], "rb");
        if (f) {
            usedPath = candidates[i];
            break;
        }
    }
    if (!f)
        return 0;

    /* Allocate ROM buffer if not already done */
    if (!gRomData) {
        gRomSize = ROM_EXPECTED_SIZE;
        gRomData = AllocateRomBuffer(gRomSize, 1);
        if (!gRomData) {
            fclose(f);
            return 0;
        }
    }

    /* Read and verify header */
    char magic[4];
    u32 chunkCount;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "GAPD", 4) != 0) {
        fprintf(stderr, "WARNING: %s has invalid magic\n", usedPath);
        fclose(f);
        return 0;
    }
    if (fread(&chunkCount, 4, 1, f) != 1) {
        fprintf(stderr, "WARNING: %s truncated (missing chunk count); skipping gap data\n", usedPath);
        fclose(f);
        return 0;
    }

    /* Read chunks and patch gRomData */
    u32 loaded = 0;
    u32 totalBytes = 0;
    for (u32 i = 0; i < chunkCount; i++) {
        u32 offset, size;
        if (fread(&offset, 4, 1, f) != 1 || fread(&size, 4, 1, f) != 1) {
            fprintf(stderr, "WARNING: %s truncated at chunk %u/%u header; aborting gap load\n", usedPath, i,
                    chunkCount);
            break;
        }
        if (offset + size > gRomSize) {
            fseek(f, (long)size, SEEK_CUR);
            continue;
        }
        if (fread(&gRomData[offset], 1, size, f) != size) {
            fprintf(stderr, "WARNING: %s truncated in chunk %u/%u (offset 0x%X, %u bytes); aborting gap load\n",
                    usedPath, i, chunkCount, offset, size);
            break;
        }
        loaded++;
        totalBytes += size;
    }

    fclose(f);
    fprintf(stderr, "Gap data loaded: %u chunks (%u KB) from %s\n", loaded, totalBytes / 1024, usedPath);
    return (int)loaded;
}

void Port_LoadRom(const char* path) {
    sLoadedRomPath[0] = '\0';
#ifndef TMC_N64
    memset(sExtractedPages, 0, sizeof(sExtractedPages));

#ifndef TMC_3DS
    /* ---- Step 1: try loading from rom_data/ extracted pages ---- */
    int pagesLoaded = LoadExtractedPages();
    if (pagesLoaded > 0) {
        fprintf(stderr, "ROM data: loaded %d extracted pages from " ROM_EXTRACT_DIR "/\n", pagesLoaded);
    }
#endif

    /* ---- Step 2: try ROM files (USA first, then EU) ---- */
    /*
     * Load a ROM file BEFORE assets so that ALL data regions are filled,
     * including assembled pointer tables (GfxGroups, PaletteGroups, area
     * tables, etc.) that are NOT covered by .incbin asset files.
     * Assets are loaded afterwards and safely overwrite the .incbin regions
     * with identical data.
     */
    int romLoaded = 0;
    {
        char usedPath[4096] = { 0 };
        FILE* f = NULL;

        /* TMC_BASEROM overrides the probe list entirely (headless/CI and
         * multi-region verification, or a ROM kept outside the default
         * locations). Checked BEFORE TryOpenRom because TryOpenRom's
         * exe-dir prefix pass would otherwise match a bundled baserom.gba
         * ahead of an absolute override path. */
        const char* romOverride = getenv("TMC_BASEROM");
        if (romOverride && romOverride[0]) {
            f = fopen(romOverride, "rb");
            if (f)
                snprintf(usedPath, sizeof(usedPath), "%s", romOverride);
            else
                fprintf(stderr, "TMC_BASEROM='%s' not openable; using probe list.\n", romOverride);
        }

        if (!f) {
            /* Try caller-supplied path first */
            if (path && path[0]) {
                const char* singlePath[1] = { path };
                f = TryOpenRom(singlePath, 1, usedPath, (int)sizeof(usedPath));
            }
        }
        if (!f) {
            /* Try preferred region next */
            int preferred = Port_Config_PreferredRegion();
            if (preferred == 0 /* USA */) {
                f = TryOpenRom(kRomCandidates_USA, (int)(sizeof(kRomCandidates_USA) / sizeof(kRomCandidates_USA[0])),
                               usedPath, (int)sizeof(usedPath));
            } else if (preferred == 1 /* EU */) {
                f = TryOpenRom(kRomCandidates_EU, (int)(sizeof(kRomCandidates_EU) / sizeof(kRomCandidates_EU[0])),
                               usedPath, (int)sizeof(usedPath));
            } else if (preferred == 2 /* JP */) {
                f = TryOpenRom(kRomCandidates_JP, (int)(sizeof(kRomCandidates_JP) / sizeof(kRomCandidates_JP[0])),
                               usedPath, (int)sizeof(usedPath));
            }
        }
        if (!f) {
            /* Fallback to default candidates list */
            f = TryOpenRom(kRomCandidates, ROM_CANDIDATE_COUNT, usedPath, (int)sizeof(usedPath));
        }
        if (f) {
            fseek(f, 0, SEEK_END);
            u32 fileSize = (u32)ftell(f);
            fseek(f, 0, SEEK_SET);

            int allocatedHere = 0;
            if (!gRomData) {
                gRomSize = fileSize;
                gRomData = AllocateRomBuffer(gRomSize, 0);
                allocatedHere = 1;
                if (!gRomData) {
                    char msg[160];
                    snprintf(msg, sizeof(msg),
                             "Failed to allocate %u bytes for ROM.\n\n"
                             "The system is out of memory.",
                             gRomSize);
                    Port_FatalRomError("Minish Cap PC Port - ROM allocation failed", msg);
                }
            }
            if (fileSize <= gRomSize) {
                const size_t got = fread(gRomData, 1, fileSize, f);
                if (got == (size_t)fileSize) {
                    gRomSize = fileSize;
                    romLoaded = 1;
                } else {
                    fprintf(stderr, "ERROR: short read on ROM %s (%zu/%u bytes); ignoring ROM file\n", usedPath, got,
                            fileSize);
                    if (allocatedHere) {
                        FreeRomBuffer(gRomData);
                        gRomData = NULL;
                        gRomSize = 0;
                    }
                }
            } else {
                /* Oversized file with a pre-filled buffer: keep prior
                 * behaviour (treated as loaded without re-reading). */
                romLoaded = 1;
            }
            fclose(f);
            if (romLoaded) {
                snprintf(sLoadedRomPath, sizeof(sLoadedRomPath), "%s", usedPath);
                fprintf(stderr, "ROM loaded: %u bytes (0x%X) from %s\n", gRomSize, gRomSize, usedPath);
            }
        }
    }

    /* ---- Step 3: load gap data (assembled tables not in assets) ---- */
    /*
     * Assets cover .incbin binary blobs. If a ROM was loaded, the .incbin
     * regions already have correct data and this is a harmless overwrite.
     * If no ROM was loaded, assets fill those regions from build output.
     * NOTE: assembled pointer tables (gGfxGroups, gPaletteGroups, area
     * tables, etc.) are NOT in assets — they require a ROM file.
     */

    /*
     * rom_gaps.bin contains ROM data from regions NOT covered by .incbin
     * asset files: pointer tables, GfxItem arrays, PaletteGroup structs,
     * area sub-tables, etc. Generated once from baserom.gba by
     * tools/generate_rom_gaps.py.
     */
    int gapsLoaded = 0;
    if (!romLoaded) {
        gapsLoaded = LoadRomGaps();
    }

    /* ---- Check that we have some data ---- */
    /* A full ROM file is required for normal play; extracted pages
     * (rom_data/) and rom_gaps.bin are only useful as supplemental
     * sources alongside a real ROM. Surface every "no real ROM" case
     * as a fatal dialog rather than letting the engine boot into a
     * black screen. */
    if (!romLoaded) {
        Port_FatalRomError("Minish Cap PC Port - ROM not found",
                           "Could not load baserom.gba.\n\n"
                           "Place baserom.gba (USA) or baserom_eu.gba (EU) next to tmc_pc and try again.\n"
                           "Supported names: baserom.gba, baserom_eu.gba, tmc.gba, tmc_eu.gba.");
    }

    if (!gRomData || gRomSize == 0) {
        Port_FatalRomError("Minish Cap PC Port - ROM load failed", "No ROM data available after loading.\n\n"
                                                                   "The ROM file may be empty or unreadable.");
    }
#else
    /* #N64: gRomData/gRomSize already point at the embedded cart ROM (set by
     * n64_main before calling Port_LoadRom). Skip all host file I/O. */
    (void)path;
    if (!gRomData || gRomSize == 0) {
        return;
    }
#endif

    /* ---- Step 3: auto-detect ROM region ---- */
    Port_DetectRomRegion(gRomData, gRomSize);
    const RomOffsets* R = gRomOffsets;

    if (gRomSize < R->expectedRomSize) {
        char msg[256];
        snprintf(msg, sizeof(msg), "ROM file is truncated or invalid (size %u < expected %u).", gRomSize,
                 R->expectedRomSize);
        Port_FatalRomError("Minish Cap PC Port - ROM invalid", msg);
    }
    fprintf(stderr, "Using offsets for %s (game code: %.4s)\n",
            gRomRegion == ROM_REGION_EU   ? "EU"
            : gRomRegion == ROM_REGION_JP ? "JP"
                                          : "USA",
            R->gameCode);

#ifdef TMC_N64
    { /* Byte-order / alignment probe: compare against known GBA header bytes.
       * Logo@0x04 is the fixed sequence 24 FF AE 51 69 9A A2 21; gamecode@0xAC
       * is 'BZME' = 42 5A 4D 45. Mismatch => cart CPU byte reads are swapped or
       * gRomData is misaligned. */
        const u8* h = gRomData;
        fprintf(stderr,
                "[romdump] hdr[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x  logo[4..11]=%02x %02x %02x %02x %02x "
                "%02x %02x %02x (expect 24 FF AE 51 69 9A A2 21)\n",
                h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11]);
        fprintf(stderr, "[romdump] gamecode[AC..AF]=%02x %02x %02x %02x (expect 42 5A 4D 45 'BZME')\n", h[0xAC],
                h[0xAD], h[0xAE], h[0xAF]);
        const u8* g = &gRomData[R->gfxGroups];
        fprintf(stderr,
                "[romdump] gfxGroups@0x%X [0..11]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                R->gfxGroups, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11]);
    }
#endif

    /* ---- Step 4: resolve ROM symbols using compile-time tables + gRomData ---- */

    /* gGlobalGfxAndPalettes — huge palette/gfx blob (still points into gRomData) */
    gGlobalGfxAndPalettes = &gRomData[R->gfxAndPalettes];

    /* gGfxGroups / gPaletteGroups — resolve the ROM pointer tables (arrays of
     * GBA pointers to GfxItem / PaletteGroup descriptors). On PC these come from
     * the asset pipeline; on N64 (assets stubbed) they MUST be resolved from the
     * ROM here, else LoadGfxGroup/LoadPaletteGroup see NULL and skip every load. */
    memset(gGfxGroups, 0, sizeof(void*) * GFX_GROUPS_COUNT_MAX);
    memset(gPaletteGroups, 0, sizeof(void*) * PALETTE_GROUPS_COUNT_MAX);
    ResolveSubTable(&gRomData[R->gfxGroups], (void**)gGfxGroups, R->gfxGroupsCount);
    ResolveSubTable(&gRomData[R->paletteGroups], (void**)gPaletteGroups, R->paletteGroupsCount);
    fprintf(stderr, "gGfxGroups/gPaletteGroups resolved (%u gfx, %u palette) from ROM.\n", R->gfxGroupsCount,
            R->paletteGroupsCount);

    /* gPalette_549 — Mt Crenel weather-change reads a 26-palette contiguous
     * block starting here. Copy it out of gGlobalGfxAndPalettes (offset 0x44A0
     * — same relative position on USA and EU because the palette ordering in
     * gfx_and_palettes.s is identical). Without this, the +0xD0 indexing in
     * sub_08059894 reads garbage and the mountaintop renders pink/cyan (#34).
     * (Was dropped during the PR #60 merge; restored to fix #108.) */
    {
        extern u16 gPalette_549[0x1A0];
        memcpy(gPalette_549, gGlobalGfxAndPalettes + 0x44A0, sizeof(gPalette_549));
        fprintf(stderr, "gPalette_549 loaded (%zu bytes from gGlobalGfxAndPalettes + 0x44A0).\n", sizeof(gPalette_549));
    }

    /* Resolve the active region's three lilypad rail command lists. */
    if (R->lilypadRails != 0 && R->lilypadRails + 12 <= gRomSize) {
        extern void* gLilypadRails[];
        u8* base = &gRomData[R->lilypadRails];
        if (base != NULL) {
            int i;
            for (i = 0; i < 3; i++) {
                gLilypadRails[i] = Port_UnpackRomDataPtr(base, (u32)i);
            }
            fprintf(stderr, "gLilypadRails loaded (3 rail pointers from ROM 0x%X).\n", R->lilypadRails);
        }
    }

    /* Runtime-rendered sprite data must come from the active ROM.
     * MULTI_REGION binaries are compiled with USA tables, but JP/EU have
     * different table addresses. Copying the compile-time USA blobs here
     * corrupts title/UI sprite rendering after a region switch. */
    if (R->frameObjLists + R->frameObjListsSize <= gRomSize) {
        memcpy(gFrameObjLists, &gRomData[R->frameObjLists], R->frameObjListsSize);
        fprintf(stderr, "gFrameObjLists loaded (%u bytes from ROM 0x%X).\n", R->frameObjListsSize, R->frameObjLists);
    } else {
        memcpy(gFrameObjLists, kFrameObjListsData, R->frameObjListsSize);
        fprintf(stderr, "WARNING: gFrameObjLists ROM range invalid; using compile-time fallback.\n");
    }

    /* gExtraFrameOffsets is region-specific. EU removes one first-level entry
     * and compensates later in the blob, so a USA copy shifts every multipart
     * sprite lookup even when the main sprite tables came from the EU ROM. */
    {
        extern const u8 kExtraFrameOffsetsData[4352];
        extern u8 gExtraFrameOffsets[4352];
        if (R->extraFrameOffsets != 0 && R->extraFrameOffsets + 4352 <= gRomSize) {
            memcpy(gExtraFrameOffsets, &gRomData[R->extraFrameOffsets], 4352);
            fprintf(stderr, "gExtraFrameOffsets loaded (4352 bytes from ROM 0x%X).\n", R->extraFrameOffsets);
        } else {
            memcpy(gExtraFrameOffsets, kExtraFrameOffsetsData, 4352);
            fprintf(stderr, "WARNING: gExtraFrameOffsets unavailable; using compile-time fallback.\n");
        }
    }

    /* OBJ palette offset table — now compile-time const src/data/objPalettes.c */

    if (R->fixedTypeGfx + R->fixedTypeGfxCount * 4 <= gRomSize) {
        memcpy(gFixedTypeGfxData, &gRomData[R->fixedTypeGfx], R->fixedTypeGfxCount * 4);
        fprintf(stderr, "gFixedTypeGfxData loaded (%u entries from ROM 0x%X).\n", R->fixedTypeGfxCount,
                R->fixedTypeGfx);
    } else {
        memcpy(gFixedTypeGfxData, kFixedTypeGfxInitData, R->fixedTypeGfxCount * 4);
        fprintf(stderr, "WARNING: gFixedTypeGfxData ROM range invalid; using compile-time fallback.\n");
    }

    /* gSpritePtrs — resolve every entry from the active ROM table. */
    {
        extern const u32 kMaxSpritePtrs; /* declared in port_linked_stubs.c */
        extern u32 gSpritePtrsLoadedCount;
        u32 loaded = 0;
        memset(sSpritePtrsStable, 0, sizeof(sSpritePtrsStable));
        for (u32 i = 0; i < kMaxSpritePtrs; i++) {
            const u32 entryRomOffset = R->spritePtrs + i * 16;
            if (entryRomOffset + 16 > gRomSize)
                break;
            const u32 animsGba = Port_ReadU32(&gRomData[entryRomOffset + 0]);
            const u32 framesGba = Port_ReadU32(&gRomData[entryRomOffset + 4]);
            const u32 ptrGba = Port_ReadU32(&gRomData[entryRomOffset + 8]);
            const u32 pad = Port_ReadU32(&gRomData[entryRomOffset + 12]);
            const bool inRom = (animsGba >= 0x08000000u) && (animsGba < 0x08000000u + gRomSize);
            if (i >= R->spritePtrsCount && (animsGba == 0 || !inRom))
                break;
            gSpritePtrs[i].animations = inRom ? (void*)Port_ResolveRomData(animsGba) : NULL;
            gSpritePtrs[i].frames = (framesGba >= 0x08000000u && framesGba < 0x08000000u + gRomSize)
                                        ? (SpriteFrame*)Port_ResolveRomData(framesGba)
                                        : NULL;
            gSpritePtrs[i].ptr =
                (ptrGba >= 0x08000000u && ptrGba < 0x08000000u + gRomSize) ? (void*)Port_ResolveRomData(ptrGba) : NULL;
            gSpritePtrs[i].pad = pad;
            sSpritePtrsStable[i] = gSpritePtrs[i];
            loaded++;
        }
        gSpritePtrsLoadedCount = loaded;
        fprintf(stderr, "gSpritePtrs loaded (%u entries from ROM 0x%X).\n", gSpritePtrsLoadedCount, R->spritePtrs);
    }

    /* gMoreSpritePtrs / gSpriteAnimations_322
     * On GBA, these are packed 32-bit pointer tables. On PC/64-bit we materialize
     * native pointers to avoid invalid indexing with sizeof(void*) == 8. */
    {
        extern u32 gSpritePtrsLoadedCount;
        memset(gMoreSpritePtrs, 0, sizeof(gMoreSpritePtrs));
        memset(gSpriteAnimations_322, 0, sizeof(gSpriteAnimations_322));

        const u16 sprite322Index = Port_RemapSpriteIndex(322u);
        if (gSpritePtrsLoadedCount > sprite322Index) {
            const SpritePtr* sp322 = &gSpritePtrs[sprite322Index];
            gMoreSpritePtrs[0] = (u16*)sp322->animations;
            gMoreSpritePtrs[1] = (u16*)sp322->frames;
            gMoreSpritePtrs[2] = (u16*)sp322->ptr;

            if (sp322->animations != NULL) {
                const u8* animTable = (const u8*)sp322->animations;
                u32 resolvedCount = 0;
                for (u32 i = 0; i < SPRITE_ANIM_322_COUNT; i++) {
                    u32 gbaPtr;
                    memcpy(&gbaPtr, animTable + i * 4, 4);
                    if (gbaPtr == 0) {
                        break;
                    }
                    gSpriteAnimations_322[i] = (Frame*)ResolveRomPtr(gbaPtr);
                    if (gSpriteAnimations_322[i] == NULL) {
                        break;
                    }
                    resolvedCount++;
                }
                fprintf(stderr, "gSpriteAnimations_322 resolved (%u entries via SpritePtr[%u]).\n", resolvedCount,
                        (unsigned int)sprite322Index);
            }
        }
    }

    /* Font/text data tables — from active ROM */
    memcpy(gUnk_08109244, &gRomData[R->text09244], 4);
    memcpy(gUnk_0810926C, &gRomData[R->text0926C], 64);
    memcpy(gUnk_081092D4, &gRomData[R->text092D4], 346);
    memcpy(gUnk_0810942E, &gRomData[R->text0942E], 160);
    memcpy(gUnk_081094CE, &gRomData[R->text094CE], 1378);

    /* UI data — always from the active ROM profile. */
    {
        extern u8 gUnk_080C9044[];
        if (R->uiData + 8 <= gRomSize) {
            memcpy(gUnk_080C9044, &gRomData[R->uiData], 8);
        } else {
            memcpy(gUnk_080C9044, kUiInitData, 8);
        }
    }

    /* UI element definitions (native function pointers) */
    {
        extern void Port_InitUIElementDefinitions(void);
        Port_InitUIElementDefinitions();
    }

    /* gFigurines[1..136] — Figurine viewer table; each entry's pal/gfx
     * resolved from gRomData. See port/port_figurines.c (#57). */
    {
        extern void Port_PopulateFigurines(void);
        Port_PopulateFigurines();
    }

    /* gTranslations — resolved from active ROM */
    memset(gTranslations, 0, sizeof(void*) * LANGUAGE_SLOT_COUNT);
    if (REGION_IS_JP) {
        gTranslations[0] = Port_UnpackRomDataPtr(&gRomData[R->translations], 0);
    } else if (REGION_IS_USA) {
        gTranslations[1] = Port_UnpackRomDataPtr(&gRomData[R->translations], 1);
    } else if (REGION_IS_EU) {
        for (int i = 1; i <= EU_LANGUAGE_LAST_SLOT; i++) {
            gTranslations[i] = Port_UnpackRomDataPtr(&gRomData[R->translations], i);
        }
    }
    fprintf(stderr, "gTranslations loaded (selectively by region from active ROM).\n");

    /* gUnk_08109230 — EWRAM variable pointers */
#ifdef PC_PORT
    gTextVariableSources[0] = gUnk_020227DC;
    gTextVariableSources[1] = &gUnk_020227E8[0];
    gTextVariableSources[2] = gUnk_020227F0;
    gTextVariableSources[3] = gUnk_020227F8;
    gTextVariableSources[4] = gUnk_02022800;
#else
    for (int i = 0; i < 5; i++) {
        gTextVariableSources[i] = Port_UnpackRomDataPtr(&gRomData[R->text09230], i);
    }
#endif

    /* gUnk_08109248 — resolved from active ROM */
    for (int i = 0; i < 9; i++) {
        gUnk_08109248[i] = Port_UnpackRomDataPtr(&gRomData[R->text09248], i);
    }
    fprintf(stderr, "gUnk_08109248 font tables loaded (9 entries from active ROM).\n");

    /* gUnk_081092AC — resolved from active ROM */
    for (int i = 0; i < 10; i++) {
        gUnk_081092AC[i] = Port_UnpackRomDataPtr(&gRomData[R->text092AC], i);
    }
    fprintf(stderr, "gUnk_081092AC border tables loaded (10 entries from active ROM).\n");

    /* OBJ size/clipping data also differs by region. */
    if (R->overlaySizeTable + 240 <= gRomSize) {
        Port_LoadOverlayData(gRomData, gRomSize, R->overlaySizeTable);
    } else {
        extern void Port_LoadOverlayDataFromConst(const u8* data, u32 size);
        Port_LoadOverlayDataFromConst(kOverlaySizeData, 240);
    }

    /* gMapData — copy map data blob from ROM into the PC buffer.
     * On GBA, gMapData is a ROM label; on PC it's a large u8 array.
     * Source files compute &gMapData + offset, so we fill the buffer. */
    {
        extern u8 gMapData[];
        u32 mapDataSize = gRomSize - R->mapDataBase;
        if (mapDataSize > 0xE00000u)
            mapDataSize = 0xE00000u;
#ifdef TMC_N64
        if (mapDataSize > 0x100000u)
            mapDataSize = 0x100000u; /* gMapData is a 1 MB placeholder on N64 */
#endif
        memcpy(gMapData, &gRomData[R->mapDataBase], mapDataSize);
        fprintf(stderr, "gMapData loaded (%u bytes from ROM offset 0x%X).\n", mapDataSize, R->mapDataBase);
    }

    /* ---- Area / room data tables (0x90 entries each) ---- */
    {
        /* gAreaRoomHeaders — pointer to RoomHeader array per area.
         * RoomHeader contains only u16 fields (no pointers), so we can
         * point directly into gRomData. */
        /* gAreaRoomHeaders — resolved dynamically from active ROM */
        for (u32 i = 0; i < AREA_COUNT; i++) {
            gAreaRoomHeaders[i] = (RoomHeader*)Port_UnpackRomDataPtr(&gRomData[R->areaRoomHeaders], i);
        }
        fprintf(stderr, "gAreaRoomHeaders loaded (0x%X entries from active ROM).\n", AREA_COUNT);

        /* First pass: resolve first-level pointers from active ROM.
         * NOTE: gAreaTileSets now uses the full R->areaTileSetsCount (0x90) entries,
         *       same as the other tables (AREA_COUNT = 0x90). */
        u32 tsCount = R->areaTileSetsCount < AREA_COUNT ? R->areaTileSetsCount : AREA_COUNT;
        for (u32 i = 0; i < AREA_COUNT; i++) {
            if (i < tsCount) {
                gAreaTileSets[i] = Port_UnpackRomDataPtr(&gRomData[R->areaTileSets], i);
            }
            gAreaRoomMaps[i] = Port_UnpackRomDataPtr(&gRomData[R->areaRoomMaps], i);
            gAreaTable[i] = Port_UnpackRomDataPtr(&gRomData[R->areaTable], i);
            gAreaTiles[i] = Port_UnpackRomDataPtr(&gRomData[R->areaTiles], i);
            /* gExitLists — now compile-time const from src/data/transitions.c, no ROM loading needed */
        }

        /* Second pass: resolve sub-arrays of 32-bit GBA pointers.
         * On GBA, sizeof(void*)==4 and these arrays are read directly.
         * On 64-bit PC, sizeof(void*)==8, so we must pre-resolve into
         * shadow arrays with native-width pointers.
         *
         * Instead of relying on room counts (which can reference garbage
         * tileSet_id values like 0xFFF3), we scan each sub-array to
         * determine its actual length: stop at the first entry that is
         * neither NULL nor a valid ROM pointer. */
        for (u32 i = 0; i < AREA_COUNT; i++) {
/* Helper: scan sub-array of packed 32-bit values at 'base',
 * counting entries that are 0 or valid ROM pointers.
 * Stops at first entry that is neither. Caps at MAX_ROOMS. */
#define SCAN_SUB_ARRAY(base, out_count)                                        \
    do {                                                                       \
        u8* _b = (u8*)(base);                                                  \
        (out_count) = 0;                                                       \
        for (u32 _j = 0; _j < MAX_ROOMS; _j++) {                               \
            u32 _v;                                                            \
            memcpy(&_v, _b + _j * 4, 4);                                       \
            if (_v == 0 || (_v >= 0x08000000u && _v < 0x08000000u + gRomSize)) \
                (out_count) = _j + 1;                                          \
            else                                                               \
                break;                                                         \
        }                                                                      \
    } while (0)

            u32 subCount;

            if (gAreaTileSets[i]) {
                SCAN_SUB_ARRAY(gAreaTileSets[i], subCount);
                if (subCount > 0) {
                    ResolveSubTable(gAreaTileSets[i], sTileSetsResolved[i], subCount);
                    gAreaTileSets[i] = (void*)sTileSetsResolved[i];
                }
            }

            if (gAreaRoomMaps[i]) {
                SCAN_SUB_ARRAY(gAreaRoomMaps[i], subCount);
                if (subCount > 0) {
                    ResolveSubTable(gAreaRoomMaps[i], sRoomMapsResolved[i], subCount);
                    gAreaRoomMaps[i] = (void*)sRoomMapsResolved[i];
                }
            }

            if (gAreaTable[i]) {
                SCAN_SUB_ARRAY(gAreaTable[i], subCount);
                if (subCount > 0) {
                    ResolveSubTable(gAreaTable[i], sAreaTableResolved[i], subCount);
                    gAreaTable[i] = (void*)sAreaTableResolved[i];
                }
            }

            /* gExitLists — compile-time const, no resolution needed */

#undef SCAN_SUB_ARRAY
        }

        fprintf(stderr, "Area data tables loaded (0x%X areas, 2-level pointers resolved).\n", AREA_COUNT);
    }

    Port_LogAssetLoaderStatus();

    /* The extracted assets/ cache is built from the USA ROM (asset baseline is
     * USA). These overrides reseed gTranslations / gSpritePtrs / area tables from
     * that cache, so applying them against a JP ROM clobbers the region-correct
     * data resolved above with USA content (JP gTranslations[0] gets NULLed and
     * English supplied in slot 1 → JP text renders as English, and JP font/area
     * tables go garbage → file-select font crash). Skip the override for a JP ROM;
     * Asset overriding for JP ROMs is now gated inside the loader functions. */
    if (Port_LoadTextsFromAssets()) {
        fprintf(stderr, "gTranslations overridden from extracted assets.\n");
    }

    if (Port_LoadSpritePtrsFromAssets()) {
        fprintf(stderr, "gSpritePtrs overridden from extracted assets.\n");
    }

    if (Port_LoadAreaTablesFromAssets()) {
        fprintf(stderr, "Area data tables overridden from extracted assets.\n");
    }

    fprintf(stderr, "ROM symbols resolved (%s: gGlobalGfxAndPalettes, gFrameObjLists).\n",
            gRomRegion == ROM_REGION_EU   ? "EU"
            : gRomRegion == ROM_REGION_JP ? "JP"
                                          : "USA");

    /* Initialize data stubs with ROM datas */
    {
        extern void Port_InitDataStubs(void);
        Port_InitDataStubs();
    }

    /* ---- Extract all known ROM regions to rom_data/ ---- */
#ifndef TMC_3DS
    EnsureExtractDir();

    /* ROM header (for game code verification) */
    ExtractRegion(0, 0x200);

    /* Brightness/fade tables */
    ExtractRegion(R->fadeData, 0x1200 - R->fadeData);

    /* Pointer tables themselves */
    ExtractRegion(R->gfxGroups, R->gfxGroupsCount * 4);
    ExtractRegion(R->paletteGroups, R->paletteGroupsCount * 4);
    ExtractRegion(R->objPalettes, R->objPalettesCount * 4);
    ExtractRegion(R->frameObjLists, R->frameObjListsSize);

    /* Sprite pointer table + fixed type gfx data */
    ExtractRegion(R->spritePtrs, R->spritePtrsCount * 16);
    ExtractRegion(R->fixedTypeGfx, R->fixedTypeGfxCount * 4);

    /* Gfx+palette blob */
    if (gRomSize > R->gfxAndPalettes)
        ExtractRegion(R->gfxAndPalettes, gRomSize - R->gfxAndPalettes);

    /* Overlay size table */
    ExtractRegion(R->overlaySizeTable, 240);

    /* Area data pointer tables */
    ExtractRegion(R->areaRoomHeaders, AREA_COUNT * 4);
    ExtractRegion(R->areaTileSets, R->areaTileSetsCount * 4);
    ExtractRegion(R->areaRoomMaps, AREA_COUNT * 4);
    ExtractRegion(R->areaTable, AREA_COUNT * 4);
    ExtractRegion(R->areaTiles, AREA_COUNT * 4);
    ExtractRegion(R->exitLists, AREA_COUNT * 4);

    /* Font/text data region */
    {
        u32 textStart = R->translations;
        u32 textEnd = R->text094CE + 1378 + 0x100; /* conservative region */
        ExtractRegion(textStart, textEnd - textStart);
    }

    /* Extract area sub-table data (referenced by pointer tables) */
    for (u32 i = 0; i < AREA_COUNT; i++) {
        u32 tables[] = { R->areaRoomHeaders, R->areaTileSets, R->areaRoomMaps, R->areaTable, R->areaTiles };
        for (u32 t = 0; t < 5; t++) {
            if (t == 1 && i >= R->areaTileSetsCount)
                continue;
            u32 ptr;
            memcpy(&ptr, &gRomData[tables[t] + i * 4], 4);
            if (ptr >= 0x08000000u && ptr < 0x08000000u + gRomSize) {
                ExtractRegion(ptr - 0x08000000u, ROM_PAGE_SIZE);
            }
        }
    }

    /* Extract sprite pointer referenced data */
    {
        const u8* src = &gRomData[R->spritePtrs];
        for (u32 i = 0; i < R->spritePtrsCount; i++) {
            u32 ptrs[3];
            memcpy(&ptrs[0], src + i * 16 + 0, 4); /* animations */
            memcpy(&ptrs[1], src + i * 16 + 4, 4); /* frames */
            memcpy(&ptrs[2], src + i * 16 + 8, 4); /* ptr */
            for (int p = 0; p < 3; p++) {
                if (ptrs[p] >= 0x08000000u && ptrs[p] < 0x08000000u + gRomSize) {
                    ExtractRegion(ptrs[p] - 0x08000000u, ROM_PAGE_SIZE);
                }
            }
        }
    }

#endif
    Port_PrintRomAccessSummary();
}

void Port_ApplyLanguage(void) {
    extern u32* gTranslations[];
    /* Called every frame (port_bios.c Port_UpdateInput). A configured
     * preference must apply when SET or CHANGED (F8 menu calls this right
     * after updating the config) — but NOT re-assert every frame, which
     * overwrote the user's in-game language choice (EU options menu). */
    static int sLastAppliedPref = -2; /* -2 = never applied */
    if (!gSaveHeader)
        return;

    int lang = Port_Config_PreferredLanguage();
    if (lang == sLastAppliedPref && lang >= 0)
        return; /* preference unchanged — leave the in-game choice alone */

    if (lang < 0) {
        sLastAppliedPref = lang;
        const int current = gSaveHeader->language;
        if (current >= 0 && current < (int)RegionLanguageSlotCount() && gTranslations[current] != NULL &&
            RegionSaveLanguageValid((u32)current)) {
            return;
        }
        lang = RegionDefaultLanguage();
    } else {
        sLastAppliedPref = lang;
        lang = RegionPreferredLanguageToSaveSlot(lang);
    }

    if (lang >= 0 && lang < (int)RegionLanguageSlotCount() && gTranslations[lang] != NULL) {
        gSaveHeader->language = (u8)lang;
    }
}
