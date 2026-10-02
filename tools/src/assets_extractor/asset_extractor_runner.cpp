#include "asset_extractor_runner.h"

#include <assets_extractor.hpp>

extern "C" {
extern u8* gRomData;
extern u32 gRomSize;
}

namespace {

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

Config MakeConfig(const std::filesystem::path& editableAssetsFolder) {
    Config config;
#ifdef EU
    config.gfxGroupsTableOffset = 0x100A94;
    config.gfxGroupsTableLength = 133;
    config.paletteGroupsTableOffset = 0x0FF83C;
    config.paletteGroupsTableLength = 208;
    config.globalGfxAndPalettesOffset = 0x5A3898;
    config.mapDataOffset = 0x325504;
    config.areaRoomHeadersTableOffset = 0x11E1F8;
    config.areaTileSetsTableOffset = 0x102458;
    config.areaRoomMapsTableOffset = 0x107974;
    config.areaTableTableOffset = 0x0D50E8;
    config.areaTilesTableOffset = 0x103088;
    config.spritePtrsTableOffset = 0x0029B4;
    config.spritePtrsCount = 329;
    config.translationsTableOffset = 0x109200;
    config.language = 1;
    config.variant = "EU";
#else
    config.gfxGroupsTableOffset = 0x100AA8;
    config.gfxGroupsTableLength = 133;
    config.paletteGroupsTableOffset = 0x0FF850;
    config.paletteGroupsTableLength = 208;
    config.globalGfxAndPalettesOffset = 0x5A2E80;
    config.mapDataOffset = 0x324AE4;
    config.areaRoomHeadersTableOffset = 0x11E214;
    config.areaTileSetsTableOffset = 0x10246C;
    config.areaRoomMapsTableOffset = 0x107988;
    config.areaTableTableOffset = 0x0D50FC;
    config.areaTilesTableOffset = 0x10309C;
    config.spritePtrsTableOffset = 0x0029B4;
    config.spritePtrsCount = 329;
    config.translationsTableOffset = 0x109214;
    config.language = 1;
    config.variant = "USA";
#endif
    config.outputRoot = editableAssetsFolder;
    return config;
}

bool CopySoundsJson(const std::filesystem::path& root, std::string* error) {
    const std::filesystem::path target = root / "sounds.json";
    if (std::filesystem::exists(target)) {
        return true;
    }

    for (std::filesystem::path probe = root; !probe.empty(); probe = probe.parent_path()) {
        const std::filesystem::path source = probe / "assets" / "sounds.json";
        if (std::filesystem::exists(source)) {
            std::error_code ec;
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                SetError(error, "failed to copy sounds.json: " + ec.message());
                return false;
            }
            return true;
        }

        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
    }

    SetError(error, "sounds.json was not found");
    return false;
}

} // namespace

bool RunEmbeddedAssetExtractor(const std::filesystem::path& root, std::string* error) {
    const std::filesystem::path editableAssetsFolder = root / "assets_src";
    const std::filesystem::path runtimeAssetsFolder = root / "assets";
    const std::filesystem::path romPath = root / "baserom.gba";

    if (!load_rom(romPath)) {
        SetError(error, "failed to load ROM from " + romPath.string());
        return false;
    }

    gRomData = Rom.data();
    gRomSize = static_cast<u32>(Rom.size());

    std::error_code ec;
    std::filesystem::create_directories(editableAssetsFolder, ec);
    if (ec) {
        SetError(error, "failed to create assets_src: " + ec.message());
        return false;
    }

    extract_assets(MakeConfig(editableAssetsFolder));

    std::string buildError;
    if (!PortAssetPipeline::BuildRuntimeAssets(editableAssetsFolder, runtimeAssetsFolder, &buildError)) {
        SetError(error, buildError.empty() ? "failed to build runtime assets" : buildError);
        return false;
    }

    return CopySoundsJson(root, error);
}
