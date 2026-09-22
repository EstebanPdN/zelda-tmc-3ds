#include "port_asset_loader.h"
#include "port_asset_pipeline.hpp"
#include "port_asset_pak_loader.hpp"
#include "port_debug_verbose.h" /* Port_DebugVerbose flag */
#include "port_exe_path.hpp"

extern "C" {
#define this this_
#include "common.h"
#include "port_gba_mem.h"
#include "port_rom.h"
#include "port_config.h" /* gRomRegion / ROM_REGION_JP for the JP gfx-group gate */
#include "region.h"      /* RegionAssetSubdir() — per-region cache folder */
#include "port_asset_index.h"
#include "structures.h"
#include "area.h"
#undef this

extern RoomHeader* gAreaRoomHeaders[];
extern void* gAreaRoomMaps[];
extern void* gAreaTable[];
extern void* gAreaTileSets[];
extern void* gAreaTiles[];
extern u32* gTranslations[];
extern SpritePtr gSpritePtrs[];
extern u16* gMoreSpritePtrs[];
extern Frame* gSpriteAnimations_322[];
}

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <nlohmann/json.hpp>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

/* Thin wrapper over PortAssetPipeline::ReadFileBytes (the bulk-read
 * helper shared with the extractor's ReadBinaryFile) that hands back an
 * owning buffer. Returns nullptr on any I/O failure; an empty file
 * yields a non-null empty vector. Caller owns the unique_ptr. */
static std::unique_ptr<std::vector<u8>> PortAssetLoader_ReadFileFast(const std::filesystem::path& path) {
    auto buf = std::make_unique<std::vector<u8>>();
    if (!PortAssetPipeline::ReadFileBytes(path, *buf)) {
        return nullptr;
    }
    return buf;
}

namespace {

struct SaveHeaderLite {
    int signature;
    u8 saveFileId;
    u8 msgSpeed;
    u8 brightness;
    u8 language;
};

struct GfxGroupEntryData {
    u8 unknown;
    u32 dest;
    std::string file;
    bool terminator;
};

struct PaletteFileRefData {
    std::string file;
    u32 byteOffset;
    u32 size;
    u32 numPalettes;
};

struct PaletteGroupEntryData {
    u8 destPaletteNum;
    u8 numPalettes;
    bool terminator;
    std::vector<PaletteFileRefData> paletteFiles;
};

struct MapDefinitionRefData {
    bool multiple = false;
    bool compressed = false;
    bool isPaletteGroup = false;
    u16 paletteGroup = 0;
    u32 dest = 0;
    u32 size = 0;
    std::string file;
};

struct AreaPropertyEntryData {
    std::vector<std::string> files;
};

struct SpritePtrEntryData {
    std::vector<std::string> animations;
    std::string framesFile;
    std::string ptrFile;
    u32 pad = 0;
};

constexpr size_t kAreaCount = 0x90;
constexpr size_t kSpritePtrMax = 512;
constexpr size_t kSpriteAnim322Count = 128;

struct AssetGroupCache {
    bool initAttempted = false;
    bool ready = false;
    bool spritePtrsLoaded = false;
    bool areaTablesLoaded = false;
    bool textsLoaded = false;
    bool hasSpritePtrData = false;
    bool hasAreaData = false;
    bool hasTextData = false;
    std::filesystem::path assetsRoot;
    std::unordered_map<u32, std::vector<GfxGroupEntryData>> gfxGroups;
    std::unordered_map<u32, std::vector<PaletteGroupEntryData>> paletteGroups;
    std::array<std::vector<RoomHeader>, kAreaCount> areaRoomHeaders;
    std::array<std::vector<std::vector<MapDefinitionRefData>>, kAreaCount> areaTileSets;
    std::array<std::vector<std::vector<MapDefinitionRefData>>, kAreaCount> areaRoomMaps;
    std::array<std::vector<MapDefinitionRefData>, kAreaCount> areaTiles;
    std::array<std::vector<AreaPropertyEntryData>, kAreaCount> areaTables;
    std::vector<SpritePtrEntryData> spritePtrs;
    std::unordered_map<std::string, std::unique_ptr<std::vector<u8>>> binaryFiles;
    std::unordered_map<std::string, u32> mapAssetFileToIndex;
    std::vector<std::string> mapAssetFiles;
    std::array<std::vector<MapDataDefinition*>, kAreaCount> areaTileSetPtrs;
    std::array<std::vector<MapDataDefinition*>, kAreaCount> areaRoomMapPtrs;
    std::array<std::vector<void**>, kAreaCount> areaTablePtrs;
    std::array<MapDataDefinition*, kAreaCount> areaTilesPtrs = {};
    std::array<std::vector<std::unique_ptr<void*[]>>, kAreaCount> areaPropertyStorage;
    std::array<std::vector<std::unique_ptr<MapDataDefinition[]>>, kAreaCount> mapDefStorage;
    std::vector<std::vector<const u8*>> spriteAnimationPtrs;
    std::array<std::vector<u8>, 7> translationBuffers;
    std::array<std::unordered_map<u32, std::string>, 7> textFilesById;
    bool modsScanned = false;
    std::unordered_map<std::string, std::filesystem::path> modReplacements;

    /* When non-empty, LoadBinaryFileCached will look up runtime
     * binary files in these mmap'd pak archives before falling back
     * to loose-file ifstream. Mounted at startup by
     * port_asset_bootstrap if any *.pak is present in assetsRoot
     * and the user hasn't passed --loose-assets. */
    PortAssetPak::PakSet paks;
    bool paksEnabled = false;

    /* Mod overlays: directory roots searched (first-wins) before the
     * pak archives and the loose assets tree. Populated by
     * Port_Mods_Init() at startup from the TMC_MODS env var or by
     * auto-discovery in <assets-root>/mods.
     *
     * modExplicitSelection is true only when TMC_MODS was set; in that
     * mode auto-discovery is disabled so unlisted mods cannot affect a run. */
    bool modExplicitSelection = false;
    std::vector<std::filesystem::path> modRoots;
};

AssetGroupCache gAssetGroupCache;
std::unordered_set<std::string> gAssetLogOnceKeys;

std::string PathForLog(const std::filesystem::path& path) {
    return path.generic_string();
}

void AssetLogOnce(const std::string& key, const char* fmt, ...) {
    if (!gAssetLogOnceKeys.insert(key).second) {
        return;
    }

    /* On low-end hardware (Pi 4 / Steam Deck / old laptop) the per-
     * asset [ASSET] line spam is a real CPU cost during room init
     * — 20k+ formatted lines per boot. Default OFF; set TMC_VERBOSE=1
     * to re-enable. The dedup set is still maintained so re-enabling
     * mid-session doesn't replay everything. */
    if (!Port_DebugVerbose)
        return;

    std::va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[ASSET] ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

// Returns the directory containing the running executable, or — only as a
// last resort — std::filesystem::current_path(). The release tarball ships
// `tmc_pc` and `assets[_src]/` as siblings, so the exe directory is the
// authoritative answer; cwd just happens to coincide with it when launched
// from a terminal in the same dir.
std::optional<std::filesystem::path> GetExecutableDirectory() {
    if (auto dir = port::ExecutableDir()) {
        return dir;
    }
#ifdef _WIN32
    return std::nullopt;
#else
    std::error_code ec;
    return std::filesystem::current_path(ec);
#endif
}

/* Build the search list once: exe-dir first (typical install layout), then
 * cwd (works around users who launch via a custom dynamic loader, e.g.
 * `$HOME/glibc/ld-linux.so.2 ./tmc_pc`, in which case /proc/self/exe points
 * at the loader rather than tmc_pc — issue #2). Caller filters by which
 * candidate actually contains the expected JSON manifest. */
static std::vector<std::filesystem::path> AssetSearchRoots() {
    std::vector<std::filesystem::path> roots;
    const auto exeDir = GetExecutableDirectory();
    if (exeDir.has_value()) {
        roots.push_back(*exeDir);
    }
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec && (!exeDir.has_value() || *exeDir != cwd)) {
        roots.push_back(cwd);
    }
    return roots;
}

/* The asset cache is keyed per-region: assets/<region>/ and assets_src/<region>/.
 * gActiveRegion is set at ROM load, so RegionAssetSubdir() is valid here. For USA
 * we also accept the legacy FLAT assets/ tree (older installs predating per-region
 * folders) so they aren't forced to re-extract. The flat fallback is USA-only on
 * purpose: a JP/EU run must never silently pick up a flat USA tree — that is the
 * exact cross-region corruption this scheme exists to prevent. */
static bool RegionAllowsLegacyFlat() {
    return std::string(RegionAssetSubdir()) == "usa";
}

/* Shared search for an asset tree: probe <root>/<subdir>/<region> for
 * every manifest in `manifests`, then (USA only) the legacy flat
 * <root>/<subdir>. Editable trees live under "assets_src" and require
 * palettes.json too; runtime trees live under "assets". */
std::optional<std::filesystem::path> FindAssetsRoot(const char* subdir, std::initializer_list<const char*> manifests) {
    const char* sub = RegionAssetSubdir();
    const auto hasManifests = [&](const std::filesystem::path& dir) {
        for (const char* m : manifests) {
            if (!std::filesystem::exists(dir / m)) {
                return false;
            }
        }
        return true;
    };
    for (const auto& root : AssetSearchRoots()) {
        const std::filesystem::path regioned = root / subdir / sub;
        if (hasManifests(regioned)) {
            return regioned;
        }
        if (RegionAllowsLegacyFlat()) {
            const std::filesystem::path legacy = root / subdir;
            if (hasManifests(legacy)) {
                return legacy;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> FindEditableAssetsRoot() {
    return FindAssetsRoot("assets_src", { "gfx_groups.json", "palette_groups.json", "palettes.json" });
}

std::optional<std::filesystem::path> FindRuntimeAssetsRoot() {
    return FindAssetsRoot("assets", { "gfx_groups.json", "palette_groups.json" });
}

/* Map an editable root to its runtime sibling, preserving the region subdir:
 *   <root>/assets_src/<region>  ->  <root>/assets/<region>
 *   <root>/assets_src           ->  <root>/assets           (legacy flat) */
std::filesystem::path RuntimeRootForEditableRoot(const std::filesystem::path& editableRoot) {
    if (editableRoot.filename() == "assets_src") {
        return editableRoot.parent_path() / "assets";
    }
    const std::filesystem::path base = editableRoot.parent_path(); // expected: .../assets_src
    if (base.filename() == "assets_src") {
        return base.parent_path() / "assets" / editableRoot.filename();
    }
    return editableRoot.parent_path() / "assets";
}

bool LoadJsonFile(const std::filesystem::path& path, nlohmann::json& outJson);

std::string NormalizeAssetPath(std::string path) {
    for (char& ch : path) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    return path;
}

#include "port_asset_loader_mods.inc"

bool LoadJsonFile(const std::filesystem::path& path, nlohmann::json& outJson) {
    std::ifstream input(path);
    if (!input.good()) {
        return false;
    }

    input >> outJson;
    return true;
}

std::string JsonStringOrEmpty(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || object[key].is_null() || !object[key].is_string()) {
        return {};
    }
    return object[key].get<std::string>();
}

bool IsRomPointer(const void* ptr, size_t size = 1) {
    if (ptr == nullptr || gRomData == nullptr || gRomSize < size) {
        return false;
    }

    const uintptr_t start = reinterpret_cast<uintptr_t>(gRomData);
    const uintptr_t end = start + static_cast<uintptr_t>(gRomSize);
    const uintptr_t at = reinterpret_cast<uintptr_t>(ptr);
    return at >= start && at <= end - size;
}

bool LoadOptionalJson(const std::filesystem::path& path, nlohmann::json& json) {
    if (!std::filesystem::exists(path)) {
        json = nlohmann::json();
        return true;
    }
    return LoadJsonFile(path, json);
}

const std::vector<u8>* LoadBinaryFileCached(const std::string& relativePath);

/*
 * Append the ROM bytes that follow an animation so the engine can read
 * past the end of the extracted .bin exactly as it would on GBA.  We
 * scan forward through the trailing ROM data for the first loop frame
 * (bit-7 set on byte [3] of a 4-byte record) and include everything
 * up to and including its loop_back byte.  Once the engine hits that
 * loop it stays within the padded buffer.
 *
 * Returns the number of bytes appended (0 if ROM lookup fails).
 */
size_t AppendRomTrailingBytes(const char* assetPath, size_t fileSize, std::vector<u8>& buf) {
    if (gRomData == nullptr || gRomSize == 0)
        return 0;
    const EmbeddedAssetEntry* index = EmbeddedAssetIndex_Get();
    u32 indexCount = EmbeddedAssetIndex_Count();

    u32 trailStart = 0;
    bool found = false;
    for (u32 idx = 0; idx < indexCount; ++idx) {
        if (std::strcmp(assetPath, index[idx].path) == 0) {
            trailStart = index[idx].offset + static_cast<u32>(fileSize);
            found = true;
            break;
        }
    }
    if (!found || trailStart >= gRomSize)
        return 0;

    const u8* trail = gRomData + trailStart;
    size_t available = static_cast<size_t>(gRomSize - trailStart);

    /* Walk 4-byte frame records until we find one whose frame byte
     * (byte [3]) has bit-7 set — that's a loop/done terminator.
     * Include that frame (4 bytes) plus the loop_back byte (1). */
    size_t needed = 0;
    while (needed + 4 <= available) {
        bool isLoop = (trail[needed + 3] & 0x80u) != 0u;
        needed += 4;
        if (isLoop) {
            if (needed < available)
                needed += 1; /* loop_back byte */
            break;
        }
    }

    if (needed == 0)
        return 0;

    buf.insert(buf.end(), trail, trail + needed);
    return needed;
}

void ParseGfxGroups(const nlohmann::json& root) {
    gAssetGroupCache.gfxGroups.clear();

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 group = static_cast<u32>(std::stoul(it.key()));
        std::vector<GfxGroupEntryData> entries;

        for (const auto& jsonEntry : it.value()) {
            GfxGroupEntryData entry = {};
            entry.unknown = static_cast<u8>(jsonEntry.value("unknown", 0));
            entry.dest = jsonEntry.value("dest", 0u);
            entry.file = JsonStringOrEmpty(jsonEntry, "file");
            entry.terminator = jsonEntry.value("terminator", false);
            entries.push_back(std::move(entry));
        }

        gAssetGroupCache.gfxGroups.emplace(group, std::move(entries));
    }
}

void ParsePaletteGroups(const nlohmann::json& root) {
    gAssetGroupCache.paletteGroups.clear();

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 group = static_cast<u32>(std::stoul(it.key()));
        std::vector<PaletteGroupEntryData> entries;

        if (!it.value().contains("entries") || !it.value()["entries"].is_array()) {
            continue;
        }

        for (const auto& jsonEntry : it.value()["entries"]) {
            PaletteGroupEntryData entry = {};
            entry.destPaletteNum = static_cast<u8>(jsonEntry.value("dest_palette_num", 0));
            entry.numPalettes = static_cast<u8>(jsonEntry.value("num_palettes", 0));
            entry.terminator = jsonEntry.value("terminator", false);

            if (jsonEntry.contains("palette_files") && jsonEntry["palette_files"].is_array()) {
                for (const auto& jsonRef : jsonEntry["palette_files"]) {
                    PaletteFileRefData ref = {};
                    ref.file = JsonStringOrEmpty(jsonRef, "file");
                    ref.byteOffset = jsonRef.value("byte_offset", 0u);
                    ref.size = jsonRef.value("size", 0u);
                    ref.numPalettes = jsonRef.value("num_palettes", 0u);
                    entry.paletteFiles.push_back(std::move(ref));
                }
            }

            entries.push_back(std::move(entry));
        }

        gAssetGroupCache.paletteGroups.emplace(group, std::move(entries));
    }
}

std::vector<MapDefinitionRefData> ParseMapDefinitionList(const nlohmann::json& root) {
    std::vector<MapDefinitionRefData> refs;

    if (!root.is_array()) {
        return refs;
    }

    for (const auto& jsonEntry : root) {
        MapDefinitionRefData ref = {};
        ref.multiple = jsonEntry.value("multiple", false);
        ref.compressed = jsonEntry.value("compressed", false);

        if (jsonEntry.contains("palette_group") && jsonEntry["palette_group"].is_number_unsigned()) {
            ref.isPaletteGroup = true;
            ref.paletteGroup = static_cast<u16>(jsonEntry["palette_group"].get<u32>());
        } else {
            ref.dest = jsonEntry.value("dest", 0u);
            ref.size = jsonEntry.value("size", 0u);
            ref.file = JsonStringOrEmpty(jsonEntry, "file");
        }

        refs.push_back(std::move(ref));
    }

    return refs;
}

void ParseAreaRoomHeaders(const nlohmann::json& root) {
    for (auto& areaRooms : gAssetGroupCache.areaRoomHeaders) {
        areaRooms.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 area = static_cast<u32>(std::stoul(it.key()));
        if (area >= kAreaCount || !it.value().is_array()) {
            continue;
        }

        auto& out = gAssetGroupCache.areaRoomHeaders[area];
        for (const auto& jsonRoom : it.value()) {
            RoomHeader header = {};
            header.map_x = static_cast<u16>(jsonRoom.value("map_x", 0));
            header.map_y = static_cast<u16>(jsonRoom.value("map_y", 0));
            header.pixel_width = static_cast<u16>(jsonRoom.value("pixel_width", 0));
            header.pixel_height = static_cast<u16>(jsonRoom.value("pixel_height", 0));
            header.tileSet_id = static_cast<u16>(jsonRoom.value("tile_set_id", 0xFFFF));
            out.push_back(header);
        }

        RoomHeader terminator = {};
        terminator.map_x = 0xFFFF;
        out.push_back(terminator);
    }
}

void ParseAreaMapTable(const nlohmann::json& root,
                       std::array<std::vector<std::vector<MapDefinitionRefData>>, kAreaCount>& outTable) {
    for (auto& areaEntries : outTable) {
        areaEntries.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 area = static_cast<u32>(std::stoul(it.key()));
        if (area >= kAreaCount || !it.value().is_array()) {
            continue;
        }

        auto& out = outTable[area];
        for (const auto& jsonSequence : it.value()) {
            out.push_back(ParseMapDefinitionList(jsonSequence));
        }
    }
}

void ParseAreaTiles(const nlohmann::json& root) {
    for (auto& areaEntries : gAssetGroupCache.areaTiles) {
        areaEntries.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 area = static_cast<u32>(std::stoul(it.key()));
        if (area >= kAreaCount) {
            continue;
        }

        gAssetGroupCache.areaTiles[area] = ParseMapDefinitionList(it.value());
    }
}

void ParseAreaTables(const nlohmann::json& root) {
    for (auto& areaEntries : gAssetGroupCache.areaTables) {
        areaEntries.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 area = static_cast<u32>(std::stoul(it.key()));
        if (area >= kAreaCount || !it.value().is_array()) {
            continue;
        }

        auto& out = gAssetGroupCache.areaTables[area];
        for (const auto& jsonRoom : it.value()) {
            AreaPropertyEntryData roomEntry;
            const nlohmann::json* filesJson = nullptr;

            if (jsonRoom.is_array()) {
                filesJson = &jsonRoom;
            } else if (jsonRoom.is_object() && jsonRoom.contains("files") && jsonRoom["files"].is_array()) {
                filesJson = &jsonRoom["files"];
            }

            if (filesJson != nullptr) {
                for (const auto& jsonFile : *filesJson) {
                    if (jsonFile.is_string()) {
                        roomEntry.files.push_back(jsonFile.get<std::string>());
                    } else {
                        roomEntry.files.emplace_back();
                    }
                }
            }

            out.push_back(std::move(roomEntry));
        }
    }
}

void ParseSpritePtrs(const nlohmann::json& root) {
    gAssetGroupCache.spritePtrs.clear();

    if (root.is_array()) {
        gAssetGroupCache.spritePtrs.resize(root.size());
        for (size_t i = 0; i < root.size(); ++i) {
            const auto& jsonEntry = root[i];
            if (!jsonEntry.is_object()) {
                continue;
            }

            SpritePtrEntryData entry = {};
            if (jsonEntry.contains("animations") && jsonEntry["animations"].is_array()) {
                for (const auto& jsonAnim : jsonEntry["animations"]) {
                    if (jsonAnim.is_string()) {
                        entry.animations.push_back(jsonAnim.get<std::string>());
                    } else {
                        entry.animations.emplace_back();
                    }
                }
            }
            entry.framesFile = JsonStringOrEmpty(jsonEntry, "frames_file");
            entry.ptrFile = JsonStringOrEmpty(jsonEntry, "ptr_file");
            entry.pad = jsonEntry.value("pad", 0u);
            gAssetGroupCache.spritePtrs[i] = std::move(entry);
        }
        return;
    }

    if (!root.is_object()) {
        return;
    }

    size_t maxIndex = 0;
    for (auto it = root.begin(); it != root.end(); ++it) {
        maxIndex = std::max(maxIndex, static_cast<size_t>(std::stoul(it.key())));
    }

    gAssetGroupCache.spritePtrs.resize(maxIndex + 1);
    for (auto it = root.begin(); it != root.end(); ++it) {
        const size_t index = static_cast<size_t>(std::stoul(it.key()));
        if (!it.value().is_object()) {
            continue;
        }

        SpritePtrEntryData entry = {};
        if (it.value().contains("animations") && it.value()["animations"].is_array()) {
            for (const auto& jsonAnim : it.value()["animations"]) {
                if (jsonAnim.is_string()) {
                    entry.animations.push_back(jsonAnim.get<std::string>());
                } else {
                    entry.animations.emplace_back();
                }
            }
        }
        entry.framesFile = JsonStringOrEmpty(it.value(), "frames_file");
        entry.ptrFile = JsonStringOrEmpty(it.value(), "ptr_file");
        entry.pad = it.value().value("pad", 0u);
        gAssetGroupCache.spritePtrs[index] = std::move(entry);
    }
}

void WriteLe32(std::vector<u8>& buffer, size_t offset, u32 value) {
    if (offset + 4 > buffer.size()) {
        return;
    }
    buffer[offset + 0] = static_cast<u8>(value & 0xFF);
    buffer[offset + 1] = static_cast<u8>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<u8>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<u8>((value >> 24) & 0xFF);
}

bool BuildTranslationBufferFromJson(const nlohmann::json& languageJson, std::vector<u8>& outBuffer) {
    outBuffer.clear();

    const u32 categoryCount = languageJson.value("category_count", 0u);
    if (categoryCount == 0 || !languageJson.contains("categories") || !languageJson["categories"].is_object()) {
        return false;
    }

    outBuffer.resize(static_cast<size_t>(categoryCount) * 4, 0);

    for (auto catIt = languageJson["categories"].begin(); catIt != languageJson["categories"].end(); ++catIt) {
        const u32 categoryIndex = static_cast<u32>(std::stoul(catIt.key()));
        if (categoryIndex >= categoryCount) {
            continue;
        }

        const nlohmann::json& categoryJson = catIt.value();
        const u32 messageCount = categoryJson.value("message_count", 0u);
        std::vector<u8> categoryBuffer(static_cast<size_t>(messageCount) * 4, 0);
        size_t categoryWritePos = categoryBuffer.size();

        if (categoryJson.contains("messages") && categoryJson["messages"].is_array()) {
            for (const auto& messageJson : categoryJson["messages"]) {
                const u32 messageIndex = messageJson.value("index", 0u);
                if (messageIndex >= messageCount) {
                    continue;
                }

                const std::string file = JsonStringOrEmpty(messageJson, "file");
                if (file.empty()) {
                    continue;
                }

                const std::vector<u8>* fileData = LoadBinaryFileCached(file);
                if (fileData == nullptr) {
                    return false;
                }

                WriteLe32(categoryBuffer, static_cast<size_t>(messageIndex) * 4, static_cast<u32>(categoryWritePos));
                categoryBuffer.insert(categoryBuffer.end(), fileData->begin(), fileData->end());
                categoryWritePos += fileData->size();
            }
        }

        while ((categoryBuffer.size() & 0xF) != 0) {
            categoryBuffer.push_back(0xFF);
        }

        const u32 categoryOffset = static_cast<u32>(outBuffer.size());
        WriteLe32(outBuffer, static_cast<size_t>(categoryIndex) * 4, categoryOffset);
        outBuffer.insert(outBuffer.end(), categoryBuffer.begin(), categoryBuffer.end());
    }

    while ((outBuffer.size() & 0xF) != 0) {
        outBuffer.push_back(0xFF);
    }

    return true;
}

void ParseTexts(const nlohmann::json& root) {
    for (std::vector<u8>& buffer : gAssetGroupCache.translationBuffers) {
        buffer.clear();
    }
    for (auto& files : gAssetGroupCache.textFilesById) {
        files.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto langIt = root.begin(); langIt != root.end(); ++langIt) {
        const u32 languageIndex = static_cast<u32>(std::stoul(langIt.key()));
        if (languageIndex >= gAssetGroupCache.translationBuffers.size()) {
            continue;
        }

        const nlohmann::json& languageJson = langIt.value();
        if (!languageJson.value("valid", false)) {
            continue;
        }

        const std::string tableFile = JsonStringOrEmpty(languageJson, "table_file");
        if (!tableFile.empty()) {
            const std::vector<u8>* tableData = LoadBinaryFileCached(tableFile);
            if (tableData != nullptr) {
                gAssetGroupCache.translationBuffers[languageIndex] = *tableData;
            }
        }
        if (gAssetGroupCache.translationBuffers[languageIndex].empty()) {
            BuildTranslationBufferFromJson(languageJson, gAssetGroupCache.translationBuffers[languageIndex]);
        }

        if (languageJson.contains("categories") && languageJson["categories"].is_object()) {
            for (auto catIt = languageJson["categories"].begin(); catIt != languageJson["categories"].end(); ++catIt) {
                const u32 categoryIndex = static_cast<u32>(std::stoul(catIt.key()));
                const nlohmann::json& categoryJson = catIt.value();
                if (!categoryJson.contains("messages") || !categoryJson["messages"].is_array()) {
                    continue;
                }

                for (const auto& messageJson : categoryJson["messages"]) {
                    const u32 messageIndex = messageJson.value("index", 0u);
                    const u32 textId = messageJson.value("text_id", (categoryIndex << 8) | messageIndex);
                    const std::string file = JsonStringOrEmpty(messageJson, "file");
                    if (!file.empty()) {
                        gAssetGroupCache.textFilesById[languageIndex][textId] = file;
                    }
                }
            }
        }
    }
}

const std::vector<u8>* LoadBinaryFileCached(const std::string& relativePath) {
    auto it = gAssetGroupCache.binaryFiles.find(relativePath);
    if (it != gAssetGroupCache.binaryFiles.end()) {
        return it->second.get();
    }

    /* Mod overrides — matheo's hash-lookup approach (O(1) per lookup
     * via the pre-scanned modReplacements map) wins over our linear
     * modRoots scan. Combined with our fast-path reader so we don't
     * regress on cold-cache misses on slow storage. */
    ScanMods();
    const std::string normalizedPath = NormalizeAssetPath(relativePath);
    auto modIt = gAssetGroupCache.modReplacements.find(normalizedPath);
    if (modIt != gAssetGroupCache.modReplacements.end()) {
        auto data = PortAssetLoader_ReadFileFast(modIt->second);
        if (data) {
            const std::vector<u8>* result = data.get();
            gAssetGroupCache.binaryFiles.emplace(relativePath, std::move(data));
            AssetLogOnce("mod-file:" + normalizedPath, "mod override %s <- %s", normalizedPath.c_str(),
                         PathForLog(modIt->second).c_str());
            return result;
        }
        std::fprintf(stderr, "[MOD] Failed to open replacement for %s: %s\n", normalizedPath.c_str(),
                     PathForLog(modIt->second).c_str());
    }

    /* Pak first when mounted: an mmap lookup beats opening a small
     * file on every cold cache-miss. We still wrap the bytes in an
     * owning std::vector to keep the existing six callers (which
     * expect const std::vector<u8>* and store the pointer for the
     * lifetime of the engine) source-compatible. A future change can
     * thread std::span all the way through. */
    if (gAssetGroupCache.paksEnabled) {
        if (auto bytes = gAssetGroupCache.paks.Lookup(relativePath); bytes.has_value()) {
            auto data = std::make_unique<std::vector<u8>>(bytes->begin(), bytes->end());
            const std::vector<u8>* result = data.get();
            gAssetGroupCache.binaryFiles.emplace(relativePath, std::move(data));
            return result;
        }
    }

    const std::filesystem::path fullPath = gAssetGroupCache.assetsRoot / std::filesystem::path(relativePath);
    auto data = PortAssetLoader_ReadFileFast(fullPath);
    if (!data) {
        return nullptr;
    }
    const std::vector<u8>* result = data.get();
    gAssetGroupCache.binaryFiles.emplace(relativePath, std::move(data));
    return result;
}

u32 RegisterMapAssetFile(const std::string& relativePath) {
    auto found = gAssetGroupCache.mapAssetFileToIndex.find(relativePath);
    if (found != gAssetGroupCache.mapAssetFileToIndex.end()) {
        return found->second;
    }

    const u32 index = static_cast<u32>(gAssetGroupCache.mapAssetFiles.size());
    gAssetGroupCache.mapAssetFiles.push_back(relativePath);
    gAssetGroupCache.mapAssetFileToIndex.emplace(relativePath, index);
    return index;
}

MapDataDefinition* BuildMapDefinitionSequence(const std::vector<MapDefinitionRefData>& refs, u32 area) {
    if (refs.empty()) {
        return nullptr;
    }

    auto defs = std::make_unique<MapDataDefinition[]>(refs.size());
    for (size_t i = 0; i < refs.size(); ++i) {
        const MapDefinitionRefData& ref = refs[i];
        defs[i].dest = reinterpret_cast<void*>(static_cast<uintptr_t>(ref.dest));

        if (ref.isPaletteGroup) {
            defs[i].src = (ref.multiple ? MAP_MULTIPLE : 0u) | ref.paletteGroup;
            defs[i].dest = nullptr;
            defs[i].size = 0;
            continue;
        }

        const u32 assetIndex = RegisterMapAssetFile(ref.file);
        defs[i].src = (ref.multiple ? MAP_MULTIPLE : 0u) | MAP_SRC_FILE | assetIndex;
        defs[i].size = ref.size | (ref.compressed ? MAP_COMPRESSED : 0u);
    }

    /* The engine's LoadMapData walk (beanstalkSubtask.c) continues while the
     * PREVIOUS def carries MAP_MULTIPLE — the terminator lives inside the
     * data. A truncated/modded JSON whose last entry still says
     * "multiple":true would walk off this exact-size array; force the last
     * def to terminate the chain. */
    defs[refs.size() - 1].src &= ~static_cast<u32>(MAP_MULTIPLE);

    MapDataDefinition* result = defs.get();
    gAssetGroupCache.mapDefStorage[area].push_back(std::move(defs));
    return result;
}

bool BuildAreaFromAssets(u32 area) {
    if (area >= kAreaCount || !gAssetGroupCache.hasAreaData) {
        return false;
    }

    gAssetGroupCache.areaTileSetPtrs[area].clear();
    gAssetGroupCache.areaRoomMapPtrs[area].clear();
    gAssetGroupCache.areaTablePtrs[area].clear();
    gAssetGroupCache.areaPropertyStorage[area].clear();
    gAssetGroupCache.mapDefStorage[area].clear();
    gAssetGroupCache.areaTilesPtrs[area] = nullptr;

    if (!gAssetGroupCache.areaRoomHeaders[area].empty()) {
        gAreaRoomHeaders[area] = gAssetGroupCache.areaRoomHeaders[area].data();
    }
    /* If the asset cache has no extracted headers for this area, leave
     * gAreaRoomHeaders[area] alone — the startup ROM-pointer pass already
     * populated it from kAreaRoomHeaderOffsets. Nulling here would clobber
     * the valid pointer for any area whose header table wasn't extracted
     * to JSON, and crash callers that read it directly (e.g. the world-map
     * windcrest pin loop in subtaskFastTravel.c, #53). */

    /* Same gotcha as the gAreaRoomHeaders fix in #53 (ac1081b2): the
     * cache vectors below get sized to max(N,64) and filled with nullptr,
     * then the first N slots are populated from extracted data. For areas
     * with NO extracted tile/map data (e.g. Lake Hylia), N=0 and the
     * vector ends up all-nullptr but non-empty — so the `.empty() ? nullptr
     * : data()` checks always picked the data branch and clobbered the
     * ROM-backed pointer that port_rom.c populated at startup. The renderer
     * then reads tiles via NULL → all-black BG layers after fast-travel
     * to an unextracted area. Only commit the cache when the SOURCE data
     * (not the padded slot vector) was non-empty. */
    const size_t tileSetSlots = std::max<size_t>(gAssetGroupCache.areaTileSets[area].size(), 64);
    gAssetGroupCache.areaTileSetPtrs[area].assign(tileSetSlots, nullptr);
    for (size_t i = 0; i < gAssetGroupCache.areaTileSets[area].size(); ++i) {
        gAssetGroupCache.areaTileSetPtrs[area][i] =
            BuildMapDefinitionSequence(gAssetGroupCache.areaTileSets[area][i], area);
    }
    if (!gAssetGroupCache.areaTileSets[area].empty()) {
        gAreaTileSets[area] = gAssetGroupCache.areaTileSetPtrs[area].data();
    }

    const size_t roomMapSlots = std::max<size_t>(gAssetGroupCache.areaRoomMaps[area].size(), 64);
    gAssetGroupCache.areaRoomMapPtrs[area].assign(roomMapSlots, nullptr);
    for (size_t i = 0; i < gAssetGroupCache.areaRoomMaps[area].size(); ++i) {
        gAssetGroupCache.areaRoomMapPtrs[area][i] =
            BuildMapDefinitionSequence(gAssetGroupCache.areaRoomMaps[area][i], area);
    }
    if (!gAssetGroupCache.areaRoomMaps[area].empty()) {
        gAreaRoomMaps[area] = gAssetGroupCache.areaRoomMapPtrs[area].data();
    }

    /* areaTiles[area] is a single pointer (not a sub-array). If the
     * source was empty BuildMapDefinitionSequence returns nullptr; same
     * preservation rule. */
    if (!gAssetGroupCache.areaTiles[area].empty()) {
        gAssetGroupCache.areaTilesPtrs[area] = BuildMapDefinitionSequence(gAssetGroupCache.areaTiles[area], area);
        gAreaTiles[area] = gAssetGroupCache.areaTilesPtrs[area];
    }

    const auto& jsonTables = gAssetGroupCache.areaTables[area];
    const size_t areaTableSlots = std::max<size_t>(jsonTables.size(), 64);
    gAssetGroupCache.areaPropertyStorage[area].clear();
    gAssetGroupCache.areaPropertyStorage[area].resize(areaTableSlots);
    gAssetGroupCache.areaTablePtrs[area].assign(areaTableSlots, nullptr);
    for (size_t room = 0; room < jsonTables.size(); ++room) {
        const AreaPropertyEntryData& roomEntry = jsonTables[room];
        const size_t propertySlots = std::max<size_t>(roomEntry.files.size(), 64);
        auto props = std::make_unique<void*[]>(propertySlots);
        for (size_t i = 0; i < propertySlots; ++i) {
            props[i] = nullptr;
            if (i >= roomEntry.files.size()) {
                continue;
            }
            if (roomEntry.files[i].empty()) {
                continue;
            }

            /* #36: Room-property files are thin wrappers — they carry only
             * the leading bytes of a multi-chunk `gUnk_additional_*` table
             * (e.g. the 4-byte `.4byte` rail pointer that prefixes a 16-
             * byte-per-entry lava-platform table) and omit the rest, which
             * lives in adjacent `.incbin` files. Iterating off the
             * truncated file runs straight off the end into heap garbage
             * so LavaPlatform_SpawnPlatforms never finds the real 0xff
             * terminator and no moving platforms spawn.
             *
             * Two paths reach gRomData:
             *   (a) `room_properties/offset_<hex>.bin` — Rollobite repro
             *       has its leading rail-pointer chunk under this name.
             *   (b) Files registered in port_asset_index.c — the
             *       BossDoor repro has its leading chunk under
             *       `data_080D5360/gUnk_additional_8_CaveOfFlames_BossDoor.bin`
             *       and 8 follow-on chunks at adjacent ROM offsets that
             *       the asset extractor wrote into separate files.
             *
             * Both routes return gRomData + rom-offset so the consumer
             * sees the GBA-original contiguous layout. (Both were
             * dropped during the PR #60 merge; restored.) */
            u8* romPtr = nullptr;
            if (gRomData != nullptr) {
                const std::string& path = roomEntry.files[i];

                /* Path (a): filename-encoded hex offset. */
                static constexpr const char kPropPrefix[] = "room_properties/offset_";
                static constexpr const char kPropSuffix[] = ".bin";
                constexpr size_t kPrefixLen = sizeof(kPropPrefix) - 1;
                constexpr size_t kSuffixLen = sizeof(kPropSuffix) - 1;
                if (path.size() > kPrefixLen + kSuffixLen && path.compare(0, kPrefixLen, kPropPrefix) == 0 &&
                    path.compare(path.size() - kSuffixLen, kSuffixLen, kPropSuffix) == 0) {
                    const std::string hex = path.substr(kPrefixLen, path.size() - kPrefixLen - kSuffixLen);
                    char* end = nullptr;
                    const unsigned long offset = std::strtoul(hex.c_str(), &end, 16);
                    if (end != hex.c_str() && *end == '\0' && offset < gRomSize) {
                        romPtr = gRomData + offset;
                    }
                }

                /* Path (b): asset-index lookup. Built once on first use. */
                if (romPtr == nullptr) {
                    static const std::unordered_map<std::string, u32> kFileToRomOffset = []() {
                        std::unordered_map<std::string, u32> m;
                        const EmbeddedAssetEntry* entries = EmbeddedAssetIndex_Get();
                        const u32 count = EmbeddedAssetIndex_Count();
                        m.reserve(count);
                        for (u32 k = 0; k < count; ++k) {
                            m.emplace(entries[k].path, entries[k].offset);
                        }
                        return m;
                    }();
                    auto it = kFileToRomOffset.find(path);
                    if (it != kFileToRomOffset.end() && it->second < gRomSize) {
                        romPtr = gRomData + it->second;
                    }
                }
            }

            if (romPtr != nullptr) {
                props[i] = romPtr;
                AssetLogOnce("rom-prop:" + std::to_string(area) + ":" + std::to_string(room) + ":" + std::to_string(i) +
                                 ":" + roomEntry.files[i],
                             "room property area=0x%x room=%zu slot=%zu -> gRomData (%s)", area, room, i,
                             roomEntry.files[i].c_str());
            } else {
                const std::vector<u8>* fileData = LoadBinaryFileCached(roomEntry.files[i]);
                if (fileData != nullptr && !fileData->empty()) {
                    props[i] = const_cast<u8*>(fileData->data());
                }
            }
        }

        gAssetGroupCache.areaTablePtrs[area][room] = props.get();
        gAssetGroupCache.areaPropertyStorage[area][room] = std::move(props);
    }
    /* Same preservation rule as the tile/map tables above: jsonTables
     * is the SOURCE — when empty (no extracted area-tables JSON for this
     * area, e.g. Lake Hylia) the padded all-nullptr slot vector must NOT
     * clobber the ROM-backed pointer. */
    if (!jsonTables.empty()) {
        gAreaTable[area] = gAssetGroupCache.areaTablePtrs[area].data();
    }

    return true;
}

void RefreshSprite322DerivedTables() {
    memset(gMoreSpritePtrs, 0, sizeof(u16*) * 16);
    memset(gSpriteAnimations_322, 0, sizeof(Frame*) * kSpriteAnim322Count);

    const u16 sprite322Index = Port_RemapSpriteIndex(322u);
    if (gAssetGroupCache.spriteAnimationPtrs.size() <= sprite322Index) {
        return;
    }

    const SpritePtr& sp322 = gSpritePtrs[sprite322Index];
    gMoreSpritePtrs[0] = reinterpret_cast<u16*>(sp322.animations);
    gMoreSpritePtrs[1] = reinterpret_cast<u16*>(sp322.frames);
    gMoreSpritePtrs[2] = reinterpret_cast<u16*>(sp322.ptr);

    const auto& anims = gAssetGroupCache.spriteAnimationPtrs[sprite322Index];
    const size_t count = std::min(anims.size(), static_cast<size_t>(kSpriteAnim322Count));
    for (size_t i = 0; i < count; ++i) {
        gSpriteAnimations_322[i] = reinterpret_cast<Frame*>(const_cast<u8*>(anims[i]));
    }
}

bool EnsureAssetGroupCache() {
    if (gAssetGroupCache.initAttempted) {
        return gAssetGroupCache.ready;
    }

    gAssetGroupCache.initAttempted = true;

    const std::optional<std::filesystem::path> editableRoot = FindEditableAssetsRoot();
    std::optional<std::filesystem::path> assetsRoot;

    if (editableRoot.has_value()) {
        const std::filesystem::path runtimeRoot = RuntimeRootForEditableRoot(*editableRoot);
        std::string buildInfo;
        if (!PortAssetPipeline::EnsureRuntimeAssetsBuilt(*editableRoot, runtimeRoot, &buildInfo)) {
            std::fprintf(stderr, "[ASSET] Failed to build runtime assets from %s: %s\n", editableRoot->string().c_str(),
                         buildInfo.c_str());
            return false;
        }

        if (!buildInfo.empty()) {
            std::fprintf(stderr, "[ASSET] Rebuilt runtime assets from %s (%s)\n", editableRoot->string().c_str(),
                         buildInfo.c_str());
        }

        assetsRoot = runtimeRoot;
    } else {
        assetsRoot = FindRuntimeAssetsRoot();
    }

    if (!assetsRoot.has_value()) {
        return false;
    }

    nlohmann::json gfxGroupsJson;
    nlohmann::json paletteGroupsJson;
    nlohmann::json areaRoomHeadersJson;
    nlohmann::json areaTileSetsJson;
    nlohmann::json areaRoomMapsJson;
    nlohmann::json areaTablesJson;
    nlohmann::json areaTilesJson;
    nlohmann::json spritePtrsJson;
    nlohmann::json textsJson;

    if (!LoadJsonFile(*assetsRoot / "gfx_groups.json", gfxGroupsJson) ||
        !LoadJsonFile(*assetsRoot / "palette_groups.json", paletteGroupsJson) ||
        !LoadOptionalJson(*assetsRoot / "area_room_headers.json", areaRoomHeadersJson) ||
        !LoadOptionalJson(*assetsRoot / "area_tile_sets.json", areaTileSetsJson) ||
        !LoadOptionalJson(*assetsRoot / "area_room_maps.json", areaRoomMapsJson) ||
        !LoadOptionalJson(*assetsRoot / "area_tables.json", areaTablesJson) ||
        !LoadOptionalJson(*assetsRoot / "area_tiles.json", areaTilesJson) ||
        !LoadOptionalJson(*assetsRoot / "sprite_ptrs.json", spritePtrsJson) ||
        !LoadOptionalJson(*assetsRoot / "texts.json", textsJson)) {
        return false;
    }

    gAssetGroupCache.assetsRoot = *assetsRoot;
    gAssetGroupCache.hasAreaData = !areaRoomHeadersJson.is_null() && !areaTileSetsJson.is_null() &&
                                   !areaRoomMapsJson.is_null() && !areaTablesJson.is_null() && !areaTilesJson.is_null();
    gAssetGroupCache.hasSpritePtrData = !spritePtrsJson.is_null();
    gAssetGroupCache.hasTextData = !textsJson.is_null();

#ifdef TMC_ANDROID_PORT
    gAssetGroupCache.hasAreaData = false;
    gAssetGroupCache.hasSpritePtrData = false;
    gAssetGroupCache.hasTextData = false;
#endif

    try {
        ParseGfxGroups(gfxGroupsJson);
        ParsePaletteGroups(paletteGroupsJson);
        if (gAssetGroupCache.hasAreaData) {
            ParseAreaRoomHeaders(areaRoomHeadersJson);
            ParseAreaMapTable(areaTileSetsJson, gAssetGroupCache.areaTileSets);
            ParseAreaMapTable(areaRoomMapsJson, gAssetGroupCache.areaRoomMaps);
            ParseAreaTiles(areaTilesJson);
            ParseAreaTables(areaTablesJson);
        }
        if (gAssetGroupCache.hasSpritePtrData) {
            ParseSpritePtrs(spritePtrsJson);
        } else {
            gAssetGroupCache.spritePtrs.clear();
        }
        if (gAssetGroupCache.hasTextData) {
            ParseTexts(textsJson);
        } else {
            for (std::vector<u8>& buffer : gAssetGroupCache.translationBuffers) {
                buffer.clear();
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[ASSET] JSON parse failed in %s: %s\n", gAssetGroupCache.assetsRoot.string().c_str(),
                     e.what());
        return false;
    }

    gAssetGroupCache.ready = true;
    return true;
}

extern "C" void Port_LogAssetLoaderStatus(void) {
    const std::optional<std::filesystem::path> editableRoot = FindEditableAssetsRoot();
    const std::optional<std::filesystem::path> runtimeRoot = FindRuntimeAssetsRoot();

    AssetLogOnce("startup-banner", "startup asset scan:");
    AssetLogOnce("startup-editable", "editable root: %s",
                 editableRoot.has_value() ? PathForLog(*editableRoot).c_str() : "<none>");
    AssetLogOnce("startup-runtime", "runtime root candidate: %s",
                 runtimeRoot.has_value() ? PathForLog(*runtimeRoot).c_str() : "<none>");

    if (!EnsureAssetGroupCache()) {
        AssetLogOnce("startup-disabled", "asset loader inactive; ROM will be used for tables and animations.");
        return;
    }

    AssetLogOnce("startup-root", "selected asset root: %s", PathForLog(gAssetGroupCache.assetsRoot).c_str());
    AssetLogOnce("startup-gfx", "gfx groups: enabled (%zu groups)", gAssetGroupCache.gfxGroups.size());
    AssetLogOnce("startup-pal", "palette groups: enabled (%zu groups)", gAssetGroupCache.paletteGroups.size());
    AssetLogOnce("startup-sprite", "sprite_ptrs: %s",
                 gAssetGroupCache.hasSpritePtrData ? "enabled via sprite_ptrs.json" : "disabled, ROM fallback");
    AssetLogOnce("startup-text", "texts: %s",
                 gAssetGroupCache.hasTextData ? "enabled via texts.json" : "disabled, ROM fallback");
    AssetLogOnce("startup-area", "area tables: %s",
                 gAssetGroupCache.hasAreaData ? "enabled via area_*.json" : "disabled, ROM fallback");
    AssetLogOnce("startup-map-assets", "registered map asset files: %zu", gAssetGroupCache.mapAssetFiles.size());
}

/* #110: unconditional asset-environment dump for FATAL paths.
 * Common_AbortMissingAssetGroup calls this to surface enough state
 * that a triager can tell, from the bug-report stderr alone, whether
 * the user's problem is "extractor never ran", "assets/ is in the
 * wrong place", "palette_groups.json missing palette N", or
 * "EnsureAssetGroupCache failed altogether". */
extern "C" void Port_DumpAssetEnvironment(FILE* out, const char* kind, unsigned int group) {
    std::fprintf(out, "  --- asset environment ---\n");
    std::fprintf(out, "  active region: %s (cache subdir: assets/%s)\n",
                 gRomRegion == ROM_REGION_EU    ? "EU"
                 : gRomRegion == ROM_REGION_JP  ? "JP"
                 : gRomRegion == ROM_REGION_USA ? "USA"
                                                : "UNKNOWN",
                 RegionAssetSubdir());

    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    std::fprintf(out, "  cwd: %s\n", ec ? "<failed>" : PathForLog(cwd).c_str());
    const auto exeDir = GetExecutableDirectory();
    std::fprintf(out, "  exe dir: %s\n", exeDir.has_value() ? PathForLog(*exeDir).c_str() : "<unknown>");

    /* Probe for baserom.gba in the same candidate roots the asset
     * loader uses. If it's missing or the wrong size the auto-extractor
     * cannot have produced a valid assets/ tree. */
    {
        bool romFound = false;
        for (const auto& root : AssetSearchRoots()) {
            const std::filesystem::path rom = root / "baserom.gba";
            std::error_code rom_ec;
            if (std::filesystem::exists(rom, rom_ec)) {
                const auto sz = std::filesystem::file_size(rom, rom_ec);
                std::fprintf(out, "  baserom.gba: %s (%llu bytes)%s\n", PathForLog(rom).c_str(),
                             static_cast<unsigned long long>(rom_ec ? 0 : sz),
                             (sz == 16ULL * 1024 * 1024) ? "" : "  <-- WRONG SIZE, expected 16777216");
                romFound = true;
                break;
            }
        }
        if (!romFound) {
            std::fprintf(out, "  baserom.gba: NOT FOUND in cwd or exe dir\n");
        }
    }

    const auto editableRoot = FindEditableAssetsRoot();
    const auto runtimeRoot = FindRuntimeAssetsRoot();
    std::fprintf(out, "  editable assets root: %s\n",
                 editableRoot.has_value() ? PathForLog(*editableRoot).c_str() : "<not found>");
    std::fprintf(out, "  runtime assets root:  %s\n",
                 runtimeRoot.has_value() ? PathForLog(*runtimeRoot).c_str() : "<not found>");

    if (!EnsureAssetGroupCache()) {
        std::fprintf(out, "  EnsureAssetGroupCache: FAILED — no assets/ found at all.\n");
        std::fprintf(out, "  --> Extractor likely never ran. Either:\n");
        std::fprintf(out, "      1) Place baserom.gba next to tmc_pc and run ./asset_extractor manually, or\n");
        std::fprintf(out, "      2) Move baserom.gba to the same directory as tmc_pc and re-launch tmc_pc\n");
        std::fprintf(out, "         (the binary auto-extracts on first launch if baserom is alongside).\n");
        return;
    }

    std::fprintf(out, "  selected asset root: %s\n", PathForLog(gAssetGroupCache.assetsRoot).c_str());
    std::fprintf(out, "  gfx groups loaded:     %zu\n", gAssetGroupCache.gfxGroups.size());
    std::fprintf(out, "  palette groups loaded: %zu\n", gAssetGroupCache.paletteGroups.size());
    std::fprintf(out, "  paks mounted: %s\n", gAssetGroupCache.paksEnabled ? "yes" : "no");

    const std::filesystem::path palJson = gAssetGroupCache.assetsRoot / "palette_groups.json";
    const std::filesystem::path gfxJson = gAssetGroupCache.assetsRoot / "gfx_groups.json";
    std::fprintf(out, "  palette_groups.json: %s\n", std::filesystem::exists(palJson, ec) ? "exists" : "MISSING");
    std::fprintf(out, "  gfx_groups.json:     %s\n", std::filesystem::exists(gfxJson, ec) ? "exists" : "MISSING");

    if (gAssetGroupCache.paletteGroups.size() < 10) {
        std::fprintf(out, "  WARNING: palette_groups appears truncated\n");
    }

    const bool isPalette = (kind && std::strcmp(kind, "palette") == 0);
    const bool isGfx = (kind && std::strcmp(kind, "gfx") == 0);

    /* List the first few known indices for the failing kind so the
     * triager can see whether group N is missing specifically vs all
     * of them. */
    if (isPalette) {
        std::fprintf(out, "  palette groups indices present: ");
        std::size_t shown = 0;
        for (const auto& [idx, _] : gAssetGroupCache.paletteGroups) {
            if (shown++ < 32) {
                std::fprintf(out, "%u ", idx);
            }
        }
        if (gAssetGroupCache.paletteGroups.size() > 32) {
            std::fprintf(out, "... (%zu more)", gAssetGroupCache.paletteGroups.size() - 32);
        }
        std::fprintf(out, "\n");

        /* If the group IS in the map, the failure is in a referenced
         * palette file — enumerate them and flag the missing one. */
        const auto it = gAssetGroupCache.paletteGroups.find(group);
        if (it != gAssetGroupCache.paletteGroups.end()) {
            std::fprintf(out, "  palette group %u IS described in palette_groups.json — checking files:\n", group);
            for (const auto& entry : it->second) {
                for (const auto& ref : entry.paletteFiles) {
                    const std::filesystem::path p = gAssetGroupCache.assetsRoot / ref.file;
                    const bool exists = std::filesystem::exists(p, ec);
                    std::uintmax_t sz = 0;
                    if (exists)
                        sz = std::filesystem::file_size(p, ec);
                    const bool tooSmall = exists && (ref.byteOffset + ref.size > sz);
                    std::fprintf(out, "    %s %s (%llu bytes)%s\n",
                                 exists ? (tooSmall ? "TRUNCATED" : "ok       ") : "MISSING  ", ref.file.c_str(),
                                 static_cast<unsigned long long>(sz),
                                 tooSmall ? "  <-- file too small for byteOffset+size" : "");
                }
            }
        }
    } else if (isGfx) {
        std::fprintf(out, "  gfx groups indices present: ");
        std::size_t shown = 0;
        for (const auto& [idx, _] : gAssetGroupCache.gfxGroups) {
            if (shown++ < 32) {
                std::fprintf(out, "%u ", idx);
            }
        }
        if (gAssetGroupCache.gfxGroups.size() > 32) {
            std::fprintf(out, "... (%zu more)", gAssetGroupCache.gfxGroups.size() - 32);
        }
        std::fprintf(out, "\n");

        const auto it = gAssetGroupCache.gfxGroups.find(group);
        if (it != gAssetGroupCache.gfxGroups.end()) {
            std::fprintf(out, "  gfx group %u IS described in gfx_groups.json — checking files:\n", group);
            for (const auto& entry : it->second) {
                const std::filesystem::path p = gAssetGroupCache.assetsRoot / entry.file;
                const bool exists = std::filesystem::exists(p, ec);
                std::uintmax_t sz = 0;
                if (exists)
                    sz = std::filesystem::file_size(p, ec);
                std::fprintf(out, "    %s %s (%llu bytes)\n", exists ? "ok     " : "MISSING", entry.file.c_str(),
                             static_cast<unsigned long long>(sz));
            }
        }
    }
}

enum GfxLoadDecision {
    GFX_SKIP = 0,
    GFX_LOAD = 1,
    GFX_STOP = 2,
};

GfxLoadDecision EvaluateGfxControl(u8 unknown) {
    const SaveHeaderLite* saveHeader = static_cast<const SaveHeaderLite*>(gba_MemPtr(0x02000000u));
    const u8 language = (saveHeader != nullptr) ? saveHeader->language : 0;
    const u32 ctrl = unknown & 0xF;

    switch (ctrl) {
        case 0x7:
            return GFX_LOAD;
        case 0xD:
            return GFX_STOP;
        case 0xE:
            return (language != 0 && language != 1) ? GFX_LOAD : GFX_SKIP;
        case 0xF:
            return (language != 0) ? GFX_LOAD : GFX_SKIP;
        default:
            return (ctrl == language) ? GFX_LOAD : GFX_SKIP;
    }
}

} // namespace

/* gPaletteBuffer lives in port_linked_stubs.c; declare it locally rather
 * than pulling all of main.h into this translation unit. */
extern "C" u16 gPaletteBuffer[];

extern "C" bool32 Port_LoadPaletteGroupFromAssets(u32 group) {
    /* EU: never serve palette GROUPS from the extracted cache. The EU palette
     * area drops two entries (gPalette_2432/2433) relative to USA, so the
     * extractor's per-NAME file offsets sit +0x10/-0x40 off the id*32
     * arithmetic the game's group table actually uses — group loads for
     * palette ids >= ~2434 come out half-a-palette shifted (magenta/green
     * garbled EU title BG, M6 bug). The loaded ROM resolves groups exactly
     * (gGlobalGfxAndPalettes[paletteId * 32]); let LoadPaletteGroup fall
     * through to it. Same shape as the JP gfx-group gate below. */
    if (gRomRegion == ROM_REGION_EU) {
        return FALSE;
    }
    if (!EnsureAssetGroupCache()) {
        return FALSE;
    }

    const auto it = gAssetGroupCache.paletteGroups.find(group);
    if (it == gAssetGroupCache.paletteGroups.end()) {
        return FALSE;
    }

    AssetLogOnce("palette-group-json:" + std::to_string(group), "palette group %u described by %s/palette_groups.json",
                 group, PathForLog(gAssetGroupCache.assetsRoot).c_str());

    bool loadedAny = false;
    for (const PaletteGroupEntryData& entry : it->second) {
        u32 copiedPalettes = 0;

        for (const PaletteFileRefData& ref : entry.paletteFiles) {
            const std::vector<u8>* fileData = LoadBinaryFileCached(ref.file);
            if (fileData == nullptr || ref.byteOffset + ref.size > fileData->size()) {
                return FALSE;
            }

            AssetLogOnce("palette-file:" + std::to_string(group) + ":" +
                             std::to_string(entry.destPaletteNum + copiedPalettes) + ":" + ref.file,
                         "palette group %u slot %u <- %s", group, entry.destPaletteNum + copiedPalettes,
                         ref.file.c_str());
            LoadPalettes(fileData->data() + ref.byteOffset, static_cast<s32>(entry.destPaletteNum + copiedPalettes),
                         static_cast<s32>(ref.numPalettes));
            copiedPalettes += ref.numPalettes;
            loadedAny = true;
        }

        if (copiedPalettes != entry.numPalettes) {
            return FALSE;
        }

        if (entry.terminator) {
            break;
        }
    }

    return loadedAny ? TRUE : FALSE;
}

extern "C" bool32 Port_LoadGfxGroupFromAssets(u32 group) {
    /* Extracted asset caches use the build-time baseline. A universal build is
     * USA-based, so non-USA ROMs must retain their runtime-resolved ROM tables. */
    if (gRomRegion != ROM_REGION_USA) {
        return FALSE;
    }
    if (!EnsureAssetGroupCache()) {
        return FALSE;
    }

    const auto it = gAssetGroupCache.gfxGroups.find(group);
    if (it == gAssetGroupCache.gfxGroups.end()) {
        return FALSE;
    }

    AssetLogOnce("gfx-group-json:" + std::to_string(group), "gfx group %u described by %s/gfx_groups.json", group,
                 PathForLog(gAssetGroupCache.assetsRoot).c_str());

    bool loadedAny = false;
    for (const GfxGroupEntryData& entry : it->second) {
        const GfxLoadDecision decision = EvaluateGfxControl(entry.unknown);

        if (decision == GFX_STOP) {
            return TRUE;
        }

        if (decision == GFX_LOAD && !entry.file.empty()) {
            const std::vector<u8>* fileData = LoadBinaryFileCached(entry.file);
            if (fileData == nullptr) {
                return FALSE;
            }

            AssetLogOnce("gfx-file:" + std::to_string(group) + ":" + entry.file + ":" + std::to_string(entry.dest),
                         "gfx group %u -> %s (dest=0x%08X, %u bytes)", group, entry.file.c_str(), entry.dest,
                         static_cast<u32>(fileData->size()));
            /* Resolve the destination ourselves and std::memcpy directly so
             * the source pointer (a heap-allocated std::vector<u8> from
             * LoadBinaryFileCached) is NEVER passed through port_resolve_addr.
             *
             * On Windows MinGW, malloc can return addresses inside the
             * GBA address window [0x02000000, 0x0A000000). MemCopy and
             * DmaCopy32 both call port_resolve_addr on their source — when
             * a heap pointer happens to fall inside that window, the resolver
             * remaps it to gEwram[]/gVram[]/gRomData[] and the copy reads
             * whatever the game has stored there (usually zeros) instead
             * of the actual gfx bytes. Result on the user's machine:
             * Deepwood Shrine barrels invisible (#61), title-screen palette
             * stalls, etc. Linux glibc never allocates in that window so
             * the bug never fires there.
             *
             * EWRAM still needs Port_ResolveEwramPtr because the port has
             * heap-allocated stand-in arrays for gMapDataBottomSpecial /
             * gMapDataTopSpecial / gMapTop etc. that live OUTSIDE gEwram[]. */
            void* resolvedDest = nullptr;
            if (entry.dest >= 0x02000000u && entry.dest < 0x02040000u) {
                resolvedDest = Port_ResolveEwramPtr(entry.dest);
            }

            if (resolvedDest != nullptr) {
                std::memcpy(resolvedDest, fileData->data(), fileData->size());
            } else {
                MemCopy(fileData->data(), reinterpret_cast<void*>(static_cast<uintptr_t>(entry.dest)),
                        static_cast<u32>(fileData->size()));
            }
            loadedAny = true;
        }

        if (entry.terminator) {
            break;
        }
    }

    return loadedAny ? TRUE : FALSE;
}

extern "C" bool32 Port_LoadAreaTablesFromAssets(void) {
    if (!Port_ShouldUseAreaAssetCacheForRegion(gRomRegion))
        return FALSE;
    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasAreaData) {
        return FALSE;
    }

    AssetLogOnce("area-data-json",
                 "area tables enabled from "
                 "%s/{area_room_headers.json,area_tile_sets.json,area_room_maps.json,area_tables.json,area_tiles.json}",
                 PathForLog(gAssetGroupCache.assetsRoot).c_str());

    for (u32 area = 0; area < kAreaCount; ++area) {
        BuildAreaFromAssets(area);
    }

    gAssetGroupCache.areaTablesLoaded = true;
    return TRUE;
}

extern "C" bool32 Port_LoadSpritePtrsFromAssets(void) {
    /* Never reseed non-USA sprite pointers from the USA-baseline cache. Doing so
     * overwrites every gSpritePtrs[i].animations with a *native* heap pointer, which
     * is fine on USA, but breaks other regions: Port_GetSpriteAnimationData uses its ROM
     * branch there and validates spr->animations with
     * IsRomPointer() — a native pointer fails, resolves to NULL, and the entity gets
     * no animation. With a NULL animPtr FrameZero never runs, so frameIndex stays
     * 0xFF (sprite invisible — intro Zelda/Smith), animations freeze (file-select
     * preview sprite 325), and ANIM_DONE is never set so cutscene WaitForAnimDone
     * blocks forever and never returns control (Link frozen at the intro handover).
     * port_rom.c already skips its own non-USA override call, but the asset
     * bootstrap (port_asset_bootstrap.cpp) calls this ungated — gate it here too so
     * each region keeps its ROM-resolved gSpritePtrs regardless of caller. */
    if (gRomRegion != ROM_REGION_USA) {
        return FALSE;
    }
    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasSpritePtrData || gAssetGroupCache.spritePtrs.empty()) {
        return FALSE;
    }

    AssetLogOnce("sprite-ptrs-json", "sprite pointer table enabled from %s/sprite_ptrs.json",
                 PathForLog(gAssetGroupCache.assetsRoot).c_str());

    std::vector<std::vector<const u8*>> newAnimationPtrs;
    newAnimationPtrs.resize(std::max(kSpritePtrMax, gAssetGroupCache.spritePtrs.size()));
    std::vector<SpritePtr> newSpritePtrs(kSpritePtrMax);
    for (size_t i = 0; i < kSpritePtrMax; ++i) {
        newSpritePtrs[i] = gSpritePtrs[i];
    }

    for (size_t i = 0; i < gAssetGroupCache.spritePtrs.size() && i < kSpritePtrMax; ++i) {
        const SpritePtrEntryData& entry = gAssetGroupCache.spritePtrs[i];

        /* Seed from the ROM-resolved compile-time table. The
         * asset-pipeline-rewrite extractor emits per-sprite tile
         * (.ptr) and frame-table (.frames) bin files, but it slices
         * each one at the GBA-ROM offset of the next sprite kind —
         * which is the wrong size for sprites whose frames address
         * tiles beyond that boundary. Example: sprite 42 (Npc5/Zelda)
         * extracts to a 1600-byte tile buffer (50 tiles) but
         * gSpriteFrames_Npc5's firstTileIndex values reach 0x88 (136
         * tiles, 4352 bytes) — the engine reads heap garbage past end
         * of the extracted buffer and the visible regression is
         * scrambled NPC tiles in the Smith / Zelda / Picori scenes.
         *
         * In slim mode the ROM is mapped and `kSpritePtrEntries[i]`
         * already gave us a correctly-sized in-ROM pointer, so we
         * keep that and only refresh .animations / .pad below. The
         * extracted ptr/frames bins remain available via the JSON if
         * a future ROM-less path needs them, but we deliberately do
         * not consume them here. */
        SpritePtr sprite = gSpritePtrs[i];
        sprite.animations = nullptr;

        auto& animPtrs = newAnimationPtrs[i];
        animPtrs.clear();
        animPtrs.reserve(entry.animations.size());

        /* Two-pass walk so we can read the next animation's leading
         * byte (the loop-back distance the GBA would have read off the
         * end of this animation's bytes when bit-7 of the trailing
         * frame is set). The asset extractor sizes each .bin by ROM
         * offset diff and therefore truncates the loop-back byte that
         * lives at the start of the adjacent ROM region. Without
         * synthesising it back, FrameZero / UpdateAnimationVariableFrames
         * read past end-of-buffer and the engine logs
         *
         *   FrameZero: loop byte out of ROM at <padded-end-pointer>
         *
         * Visible regression: garbled Zelda sprite during the file /
         * intro screens, plus eventual segfault when a downstream
         * reader keeps walking. Mirrors the logic that was previously
         * in this function on origin/sync-matheo-release. */
        std::vector<const std::vector<u8>*> rawAnims;
        rawAnims.reserve(entry.animations.size());
        for (const std::string& animFile : entry.animations) {
            if (animFile.empty()) {
                rawAnims.push_back(nullptr);
                continue;
            }
            const std::vector<u8>* animData = LoadBinaryFileCached(animFile);
            if (animData == nullptr) {
                return FALSE;
            }
            rawAnims.push_back(animData);
        }

        for (size_t a = 0; a < rawAnims.size(); ++a) {
            const std::vector<u8>* animData = rawAnims[a];
            if (animData == nullptr) {
                animPtrs.push_back(nullptr);
                continue;
            }
            const u8* dataPtr = animData->data();
            const size_t dataSize = animData->size();
            if (dataSize < 4 || (dataSize % 4u) != 0u) {
                animPtrs.push_back(dataPtr);
                continue;
            }

            /* On GBA, animations are packed contiguously in ROM. The
             * animation engine reads bytes sequentially from animPtr;
             * when it walks past one animation it reads from the next.
             * The asset extractor sizes each .bin by ROM-offset diff,
             * truncating those trailing bytes.
             *
             * For looping animations (last frame byte has bit-7 set),
             * the missing byte is the loop_back distance.  We
             * reconstruct it from the next animation's first byte.
             *
             * For non-looping animations (last frame byte lacks bit-7),
             * the GBA would read adjacent ROM bytes as the next frame.
             * We append those actual ROM bytes (looked up via
             * EmbeddedAssetIndex) so frame signaling values that
             * gameplay state-machines depend on are preserved.  A
             * previous approach of OR-ing 0x80 into the last byte
             * corrupted these values (e.g. 0x41 -> 0xC1 broke the
             * item-get checks, phase-marker 3 -> 0x83 broke the
             * portal-shrink switch). */
            const size_t numFrames = dataSize / 4u;
            const bool lastFrameLoops = (dataPtr[dataSize - 1] & 0x80u) != 0u;

            const std::string& sourceKey = entry.animations[a];
            std::string paddedKey;
            paddedKey.reserve(sourceKey.size() + 32);
            paddedKey.append("__padded__/");
            paddedKey.append(std::to_string(i));
            paddedKey.push_back('/');
            paddedKey.append(std::to_string(a));
            paddedKey.push_back('/');
            paddedKey.append(sourceKey);

            auto buf = std::make_unique<std::vector<u8>>();
            buf->assign(dataPtr, dataPtr + dataSize);

            if (lastFrameLoops) {
                /* Loop-terminated: just append the missing loop_back
                 * byte, preferring the next animation's first byte
                 * (what the GBA would read). */
                u8 loopBack = static_cast<u8>(std::min<size_t>(numFrames, 0xFFu));
                if (a + 1 < rawAnims.size() && rawAnims[a + 1] != nullptr && !rawAnims[a + 1]->empty()) {
                    u8 nextByte = (*rawAnims[a + 1])[0];
                    if (nextByte > 0 && nextByte <= numFrames) {
                        loopBack = nextByte;
                    }
                }
                buf->push_back(loopBack);
            } else {
                /* Non-looping: append the actual ROM bytes that follow
                 * this animation so the engine reads the same data the
                 * GBA would.  We scan forward until the first loop
                 * frame so the engine stays within the buffer. */
                size_t appended = AppendRomTrailingBytes(sourceKey.c_str(), dataSize, *buf);
                if (appended == 0) {
                    /* ROM unavailable — append a safe sentinel frame:
                     * invisible tile (0xFF), 1-tick, ANIM_DONE, self-loop. */
                    const u8 sentinel[] = { 0xFF, 0x01, 0x00, 0x80, 0x01 };
                    buf->insert(buf->end(), sentinel, sentinel + sizeof(sentinel));
                }
            }

            const u8* paddedPtr = buf->data();
            gAssetGroupCache.binaryFiles.emplace(std::move(paddedKey), std::move(buf));
            animPtrs.push_back(paddedPtr);
        }

        sprite.animations = animPtrs.empty() ? nullptr : (void*)animPtrs.data();
        sprite.pad = entry.pad;
        newSpritePtrs[i] = sprite;
    }

    gAssetGroupCache.spriteAnimationPtrs = std::move(newAnimationPtrs);
    /* Don't memset(gSpritePtrs, 0, ...) — sprite entries that the JSON
     * doesn't override (which is all of them: ptr_file/frames_file are
     * null in the extractor output) need to keep their ROM-resolved
     * .ptr / .frames pointers from `gSpritePtrs loaded (... pointers
     * resolved)` startup. We seed each `sprite` from the live entry
     * above and only replace .animations / explicit .ptr / .frames /
     * .pad here. */
    for (size_t i = 0; i < newSpritePtrs.size() && i < kSpritePtrMax; ++i) {
        gSpritePtrs[i] = newSpritePtrs[i];
    }

    RefreshSprite322DerivedTables();
    gAssetGroupCache.spritePtrsLoaded = true;
    return TRUE;
}

extern "C" bool32 Port_LoadTextsFromAssets(void) {
    if (gRomRegion != ROM_REGION_USA)
        return FALSE;
    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasTextData) {
        return FALSE;
    }

    bool anyLoaded = false;
    for (const std::vector<u8>& buffer : gAssetGroupCache.translationBuffers) {
        if (!buffer.empty()) {
            anyLoaded = true;
            break;
        }
    }
    if (!anyLoaded) {
        return FALSE;
    }

    for (size_t i = 0; i < gAssetGroupCache.translationBuffers.size(); ++i) {
        std::vector<u8>& buffer = gAssetGroupCache.translationBuffers[i];
        gTranslations[i] = buffer.empty() ? nullptr : reinterpret_cast<u32*>(buffer.data());
    }

    if (anyLoaded) {
        gAssetGroupCache.textsLoaded = true;
        AssetLogOnce("texts-root", "translations loaded from %s",
                     PathForLog(gAssetGroupCache.assetsRoot / "texts.json").c_str());
    }

    return anyLoaded ? TRUE : FALSE;
}

extern "C" void Port_LogTextLookup(u32 langIndex, u32 textIndex) {
    const std::string key = "text-lookup:" + std::to_string(langIndex) + ":" + std::to_string(textIndex);

    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasTextData ||
        langIndex >= gAssetGroupCache.textFilesById.size()) {
        AssetLogOnce(key, "text 0x%04X lang %u <- ROM", textIndex & 0xFFFF, langIndex);
        return;
    }

    const auto it = gAssetGroupCache.textFilesById[langIndex].find(textIndex & 0xFFFF);
    if (it != gAssetGroupCache.textFilesById[langIndex].end()) {
        AssetLogOnce(key, "text 0x%04X lang %u <- %s", textIndex & 0xFFFF, langIndex, it->second.c_str());
    } else {
        AssetLogOnce(key, "text 0x%04X lang %u <- extracted table (file unknown)", textIndex & 0xFFFF, langIndex);
    }
}

extern "C" bool32 Port_AreSpritePtrsLoadedFromAssets(void) {
    return gAssetGroupCache.spritePtrsLoaded ? TRUE : FALSE;
}

extern "C" bool32 Port_RefreshAreaDataFromAssets(u32 area) {
    if (!Port_ShouldUseAreaAssetCacheForRegion(gRomRegion))
        return FALSE;
    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasAreaData || area >= kAreaCount) {
        return FALSE;
    }

    AssetLogOnce("area-refresh:" + std::to_string(area), "area %u refreshed from extracted area tables", area);
    return BuildAreaFromAssets(area) ? TRUE : FALSE;
}

extern "C" bool32 Port_IsAreaTablePtrFromAssets(u32 area, const void* ptr) {
    if (ptr == nullptr || !EnsureAssetGroupCache() || area >= kAreaCount) {
        return FALSE;
    }

    const auto& table = gAssetGroupCache.areaTablePtrs[area];
    return !table.empty() && ptr == table.data() ? TRUE : FALSE;
}

extern "C" bool32 Port_IsRoomHeaderPtrReadable(const void* ptr) {
    if (ptr == nullptr) {
        return FALSE;
    }

    if (IsRomPointer(ptr, sizeof(RoomHeader))) {
        return TRUE;
    }

    const RoomHeader* roomPtr = static_cast<const RoomHeader*>(ptr);

    for (const auto& roomHeaders : gAssetGroupCache.areaRoomHeaders) {
        if (roomHeaders.empty()) {
            continue;
        }

        const RoomHeader* begin = roomHeaders.data();
        const RoomHeader* end = begin + roomHeaders.size();
        if (roomPtr >= begin && roomPtr < end) {
            return TRUE;
        }
    }

    return FALSE;
}

extern "C" bool32 Port_IsLoadedAssetBytes(const void* ptr, u32 size) {
    if (ptr == nullptr) {
        return FALSE;
    }

    for (const auto& [_, dataPtr] : gAssetGroupCache.binaryFiles) {
        if (dataPtr == nullptr || dataPtr->empty()) {
            continue;
        }

        const u8* begin = dataPtr->data();
        const u8* end = begin + dataPtr->size();
        const u8* at = static_cast<const u8*>(ptr);
        if (at >= begin && at <= end && size <= static_cast<u32>(end - at)) {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * If `ptr` lands inside a cached asset blob (e.g. a compressed tileset handed
 * to LZ77UnCompVram), return one-past-the-end of that blob; else NULL. Lets the
 * BIOS decompressor bound its source read on heap blobs, which — unlike GBA ROM
 * sources — have no readable trailing bytes, so a normally-benign trailing
 * over-read runs off the allocation (heap-buffer-overflow in lz77_decomp).
 */
extern "C" const u8* Port_LoadedAssetBytesEnd(const void* ptr) {
    if (ptr == nullptr) {
        return nullptr;
    }
    const u8* at = static_cast<const u8*>(ptr);
    for (const auto& [_, dataPtr] : gAssetGroupCache.binaryFiles) {
        if (dataPtr == nullptr || dataPtr->empty()) {
            continue;
        }
        const u8* begin = dataPtr->data();
        const u8* end = begin + dataPtr->size();
        if (at >= begin && at < end) {
            return end;
        }
    }
    return nullptr;
}

extern "C" const u8* Port_GetMapAssetDataByIndex(u32 assetIndex, u32* size) {
    if (!EnsureAssetGroupCache() || assetIndex >= gAssetGroupCache.mapAssetFiles.size()) {
        return nullptr;
    }

    const std::vector<u8>* fileData = LoadBinaryFileCached(gAssetGroupCache.mapAssetFiles[assetIndex]);
    if (fileData == nullptr) {
        return nullptr;
    }

    if (size != nullptr) {
        *size = static_cast<u32>(fileData->size());
    }
    AssetLogOnce("map-asset:" + std::to_string(assetIndex), "map asset %u <- %s", assetIndex,
                 gAssetGroupCache.mapAssetFiles[assetIndex].c_str());
    return fileData->data();
}

extern "C" const u8* Port_GetSpriteAnimationData(u16 spriteIndex, u32 animIndex) {
    /* Non-USA ROMs: the cache's sprite-animation buffers use the USA baseline,
     * which made region-specific entity
     * animPtrs fail FrameZero's bounds check (hundreds of "animPtr outside/overruns
     * ROM" warnings → blank/garbled NPC sprites). Port_LoadRom already keeps them on
     * region-correct ROM-resolved gSpritePtrs, so resolve animations from the ROM,
     * that decision, instead of the asset cache. */
    if (gRomRegion == ROM_REGION_USA && EnsureAssetGroupCache()) {
        if (!gAssetGroupCache.spritePtrsLoaded) {
            Port_LoadSpritePtrsFromAssets();
        }

        if (gAssetGroupCache.spritePtrsLoaded && spriteIndex < gAssetGroupCache.spriteAnimationPtrs.size()) {
            const auto& anims = gAssetGroupCache.spriteAnimationPtrs[spriteIndex];
            if (animIndex < anims.size()) {
                if (spriteIndex < gAssetGroupCache.spritePtrs.size() &&
                    animIndex < gAssetGroupCache.spritePtrs[spriteIndex].animations.size()) {
                    AssetLogOnce("sprite-anim:" + std::to_string(spriteIndex) + ":" + std::to_string(animIndex),
                                 "sprite %u anim %u <- %s", spriteIndex, animIndex,
                                 gAssetGroupCache.spritePtrs[spriteIndex].animations[animIndex].c_str());
                }
                return anims[animIndex];
            }
        }
    }

    const SpritePtr* spr = Port_GetSpritePtr(spriteIndex);
    if (spr == nullptr || spr->animations == nullptr) {
        return nullptr;
    }

    const u8* animTable = static_cast<const u8*>(spr->animations);
    const size_t tableBytes = (static_cast<size_t>(animIndex) + 1u) * sizeof(u32);
    if (!IsRomPointer(animTable, tableBytes)) {
        return nullptr;
    }

    const u32 animGbaAddr = Port_ReadU32(animTable + static_cast<size_t>(animIndex) * sizeof(u32));
    if (animGbaAddr == 0) {
        return nullptr;
    }

    return static_cast<const u8*>(Port_ResolveRomData(animGbaAddr));
}

extern "C" int Port_MountAssetPaks(const char* assetsRoot) {
    if (assetsRoot == nullptr || *assetsRoot == '\0') {
        return 0;
    }
    const std::filesystem::path root(assetsRoot);
    const std::size_t mounted = gAssetGroupCache.paks.Mount(root);
    gAssetGroupCache.paksEnabled = mounted > 0;
    return static_cast<int>(mounted);
}

extern "C" void Port_UnmountAssetPaks(void) {
    gAssetGroupCache.paks.Clear();
    gAssetGroupCache.paksEnabled = false;
}

extern "C" bool32 Port_PaksMounted(void) {
    return gAssetGroupCache.paksEnabled ? TRUE : FALSE;
}

extern "C" int Port_PakEntryCount(void) {
    return static_cast<int>(gAssetGroupCache.paks.TotalEntries());
}

extern "C" void Port_AssetLoader_Reload(void) {
    /* Reset everything except the binary file cache (cheap to refill)
     * and the pak set (managed independently by Port_MountAssetPaks).
     * Subsequent EnsureAssetGroupCache calls will re-scan and pick up
     * assets that were extracted after the first probe. */
    gAssetGroupCache.initAttempted = false;
    gAssetGroupCache.ready = false;
    gAssetGroupCache.spritePtrsLoaded = false;
    gAssetGroupCache.areaTablesLoaded = false;
    gAssetGroupCache.textsLoaded = false;
    gAssetGroupCache.hasSpritePtrData = false;
    gAssetGroupCache.hasAreaData = false;
    gAssetGroupCache.hasTextData = false;
    gAssetGroupCache.assetsRoot.clear();
    gAssetGroupCache.gfxGroups.clear();
    gAssetGroupCache.paletteGroups.clear();
    gAssetGroupCache.spritePtrs.clear();
    /* The remaining caches (mapAssetFiles, areaRoomHeaders,
     * areaTileSets, areaRoomMaps, areaTables, areaTiles, etc.) are
     * rebuilt lazily inside EnsureAssetGroupCache; clearing the
     * scalar flags above is sufficient to trigger that. */
}

void Port_SetModsExplicitSelection(bool explicitSelection) {
    gAssetGroupCache.modExplicitSelection = explicitSelection;
    gAssetGroupCache.modsScanned = false;
}

void Port_AddModRoot(const std::filesystem::path& modRoot) {
    /* Registers one explicit mod directory. Called from port_mods.cpp
     * during startup for TMC_MODS. ScanMods can also walk
     * <assets-root>/mods when explicit selection is off; canonical-path
     * dedupe prevents double-loading the same physical directory. */
    if (modRoot.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(modRoot, ec);
    if (ec) {
        canonical = modRoot;
    }
    /* Dedupe so repeated env-var parses don't grow the vector. */
    for (const auto& existing : gAssetGroupCache.modRoots) {
        if (existing == canonical) {
            return;
        }
    }
    gAssetGroupCache.modRoots.push_back(std::move(canonical));
    /* Invalidate so the next asset request re-scans and picks up
     * mod overrides from the newly-added root. */
    gAssetGroupCache.modsScanned = false;
}
