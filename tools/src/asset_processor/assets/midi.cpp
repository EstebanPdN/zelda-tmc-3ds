#include "midi.h"
#include "reader.h"
#include "simple_format.h"
#include "util.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <util/file.h>

extern std::string gBaseromPath;

std::filesystem::path MidiAsset::generateAssetPath() {
    std::filesystem::path asset_path = path;
    return asset_path.replace_extension(".mid");
}

std::filesystem::path MidiAsset::generateBuildPath() {
    std::filesystem::path build_path = path;
    return build_path.replace_extension(".s");
}

void MidiAsset::extractBinary(const std::vector<char>& baserom) {
    // Custom extraction as we need a label in the middle.
    std::string label = path.stem().string();

    std::filesystem::path tracksPath = path;
    tracksPath.replace_filename(label + "_tracks.bin");
    std::filesystem::path headerPath = path;
    headerPath.replace_filename(label + "_header.bin");

    int headerOffset = asset["options"]["headerOffset"];

    // Extract tracks
    {
        auto file = util::open_file(tracksPath.string(), "wb");
        std::fwrite(baserom.data() + start, 1, static_cast<size_t>(headerOffset), file.get());
    }
    // Extract header
    {
        auto file = util::open_file(headerPath.string(), "wb");
        std::fwrite(baserom.data() + start + headerOffset, 1, static_cast<size_t>(size - headerOffset), file.get());
    }

    // Create dummy .s file.
    auto file = util::open_file(buildPath.string(), "wb");
    assetfmt::Print(file.get(), "\t.incbin \"{}\"\n", tracksPath.generic_string());
    assetfmt::Print(file.get(), "{}::\n", label);
    assetfmt::Print(file.get(), "\t.incbin \"{}\"\n", headerPath.generic_string());
}

void MidiAsset::parseOptions(std::vector<std::string>& commonParams, std::vector<std::string>& agb2midParams) {
    bool exactGateTime = true;

    for (const auto& it : asset["options"].items()) {
        const std::string& key = it.key();
        if (key == "group" || key == "G") {
            commonParams.emplace_back("-G");
            commonParams.push_back(to_string(it.value()));
        } else if (key == "priority" || key == "P") {
            commonParams.emplace_back("-P");
            commonParams.push_back(to_string(it.value()));
        } else if (key == "reverb" || key == "R") {
            commonParams.emplace_back("-R");
            commonParams.push_back(to_string(it.value()));
        } else if (key == "nominator") {
            agb2midParams.emplace_back("-n");
            agb2midParams.push_back(to_string(it.value()));
        } else if (key == "denominator") {
            agb2midParams.emplace_back("-d");
            agb2midParams.push_back(to_string(it.value()));
        } else if (key == "timeChanges") {
            const nlohmann::json& value = it.value();
            if (value.is_array()) {
                // Multiple time changes
                for (const auto& change : value) {
                    agb2midParams.emplace_back("-t");
                    agb2midParams.push_back(to_string(change["nominator"]));
                    agb2midParams.push_back(to_string(change["denominator"]));
                    agb2midParams.push_back(to_string(change["time"]));
                }
            } else {
                agb2midParams.emplace_back("-t");
                agb2midParams.push_back(to_string(value["nominator"]));
                agb2midParams.push_back(to_string(value["denominator"]));
                agb2midParams.push_back(to_string(value["time"]));
            }
        } else if (key == "exactGateTime") {
            if (it.value().get<int>() == 1) {
                exactGateTime = true;
            } else {
                exactGateTime = false;
            }
        } else if (key == "headerOffset") {
            // ignore here
        } else {
            commonParams.push_back("-" + key);
            commonParams.push_back(to_string(it.value()));
        }
    }

    if (exactGateTime) {
        commonParams.emplace_back("-E");
    }
}

void MidiAsset::convertToHumanReadable(const std::vector<char>& baserom) {
    (void)baserom;

    // Convert the options
    std::vector<std::string> commonParams;
    std::vector<std::string> agb2midParams;
    parseOptions(commonParams, agb2midParams);

    int headerOffset = asset["options"]["headerOffset"];

    std::filesystem::path toolPath = "tools";
    std::vector<std::string> cmd;
    cmd.push_back((toolPath / "bin" / "agb2mid").string());
    cmd.push_back(gBaseromPath);
    cmd.push_back(hex_u32(static_cast<uint32_t>(start + headerOffset)));
    cmd.push_back(gBaseromPath); // TODO deduplicate?
    cmd.push_back(assetPath.string());
    cmd.insert(cmd.end(), commonParams.begin(), commonParams.end());
    cmd.insert(cmd.end(), agb2midParams.begin(), agb2midParams.end());
    check_call(cmd);

    // We also need to build the mid to an s file here, so we get shiftability after converting.
    cmd.clear();
    cmd.push_back((toolPath / "bin" / "mid2agb").string());
    cmd.push_back(assetPath.string());
    cmd.push_back(buildPath.string());
    cmd.insert(cmd.end(), commonParams.begin(), commonParams.end());
    check_call(cmd);
}

void MidiAsset::buildToBinary() {
    // Convert the options
    std::vector<std::string> commonParams;
    std::vector<std::string> agb2midParams;
    parseOptions(commonParams, agb2midParams);
    std::filesystem::path toolPath = "tools";
    std::vector<std::string> cmd;
    cmd.push_back((toolPath / "bin" / "mid2agb").string());
    cmd.push_back(assetPath.string());
    cmd.push_back(buildPath.string());
    cmd.insert(cmd.end(), commonParams.begin(), commonParams.end());
    check_call(cmd);
}
