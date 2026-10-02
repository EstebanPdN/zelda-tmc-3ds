#include "port_asset_pipeline.hpp"

#include "port_asset_log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace PortAssetPipeline {
namespace {

constexpr const char* kPaletteFormat = "tmc_palette_v1";
constexpr const char* kTextFormat = "tmc_text_v1";
constexpr const char* kAnimationFormat = "tmc_animation_v1";
constexpr const char* kSpriteFramesFormat = "tmc_sprite_frames_v1";
constexpr const char* kBuildStateFile = ".asset_build_state.json";
/* kBuildStateVersion lives in port_asset_pipeline.hpp — shared with the
 * warm-launch stamp check in assets_extractor_api.cpp. */

struct RuntimePaletteBuild {
    nlohmann::json jsonEntry;
    std::filesystem::path runtimeFile;
};

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::string JsonStringOrEmpty(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || object[key].is_null() || !object[key].is_string()) {
        return {};
    }
    return object[key].get<std::string>();
}

bool LoadJsonFile(const std::filesystem::path& path, nlohmann::json& outJson, std::string* error = nullptr) {
    std::ifstream input(path);
    if (!input.good()) {
        SetError(error, "Could not open JSON file: " + path.string());
        return false;
    }

    try {
        input >> outJson;
        return true;
    } catch (const std::exception& e) {
        SetError(error, "Failed to parse JSON file " + path.string() + ": " + e.what());
        return false;
    }
}

bool WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& json, std::string* error = nullptr,
                   int indent = 4) {
    PortAssetLog::EnsureDir(path.parent_path());

    std::ofstream output(path);
    if (!output.good()) {
        SetError(error, "Could not write JSON file: " + path.string());
        return false;
    }

    if (indent <= 0) {
        output << json.dump();
    } else {
        output << json.dump(indent);
    }
    return true;
}

bool WriteBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& data,
                     std::string* error = nullptr) {
    PortAssetLog::EnsureDir(path.parent_path());

    std::ofstream output(path, std::ios::binary);
    if (!output.good()) {
        SetError(error, "Could not write file: " + path.string());
        return false;
    }

    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return output.good();
}

bool ReadBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>& data, std::string* error = nullptr) {
    /* Bulk single-read via the shared ReadFileBytes helper (also used by
     * the engine's PortAssetLoader_ReadFileFast). */
    if (!ReadFileBytes(path, data)) {
        SetError(error, "Could not read file: " + path.string());
        return false;
    }
    return true;
}

uint16_t RgbToGba(uint8_t r, uint8_t g, uint8_t b) {
    const uint16_t r5 = static_cast<uint16_t>((static_cast<uint32_t>(r) * 31 + 127) / 255);
    const uint16_t g5 = static_cast<uint16_t>((static_cast<uint32_t>(g) * 31 + 127) / 255);
    const uint16_t b5 = static_cast<uint16_t>((static_cast<uint32_t>(b) * 31 + 127) / 255);
    return static_cast<uint16_t>(r5 | (g5 << 5) | (b5 << 10));
}

std::string GbaToHexColor(uint16_t color) {
    const uint8_t r = static_cast<uint8_t>(((color >> 0) & 0x1F) * 255 / 31);
    const uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x1F) * 255 / 31);
    const uint8_t b = static_cast<uint8_t>(((color >> 10) & 0x1F) * 255 / 31);

    std::ostringstream ss;
    ss << '#';
    ss << "0123456789ABCDEF"[r >> 4] << "0123456789ABCDEF"[r & 0x0F];
    ss << "0123456789ABCDEF"[g >> 4] << "0123456789ABCDEF"[g & 0x0F];
    ss << "0123456789ABCDEF"[b >> 4] << "0123456789ABCDEF"[b & 0x0F];
    return ss.str();
}

bool ParseHexNibble(char c, uint8_t& value) {
    if (c >= '0' && c <= '9') {
        value = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        value = static_cast<uint8_t>(10 + c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        value = static_cast<uint8_t>(10 + c - 'A');
        return true;
    }
    return false;
}

bool ParseHexColor(const std::string& value, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (value.size() != 7 || value[0] != '#') {
        return false;
    }

    uint8_t parts[6];
    for (size_t i = 0; i < 6; ++i) {
        if (!ParseHexNibble(value[i + 1], parts[i])) {
            return false;
        }
    }

    r = static_cast<uint8_t>((parts[0] << 4) | parts[1]);
    g = static_cast<uint8_t>((parts[2] << 4) | parts[3]);
    b = static_cast<uint8_t>((parts[4] << 4) | parts[5]);
    return true;
}

std::filesystem::path ReplaceExtension(const std::filesystem::path& path, const char* extension) {
    std::filesystem::path result = path;
    result.replace_extension(extension);
    return result;
}

bool ParseHexByte(char hi, char lo, uint8_t& value) {
    uint8_t hiNibble = 0;
    uint8_t loNibble = 0;
    if (!ParseHexNibble(hi, hiNibble) || !ParseHexNibble(lo, loNibble)) {
        return false;
    }

    value = static_cast<uint8_t>((hiNibble << 4) | loNibble);
    return true;
}

struct TextCharMapping {
    uint8_t byte;
    const char* utf8;
};

constexpr TextCharMapping kTextCharMappings[] = {
    { 0x0A, "\n" },
    { 0x0D, "\r" },
    { 0x20, " " },
    { 0x21, "!" },
    { 0x22, "\"" },
    { 0x23, "#" },
    { 0x24, "$" },
    { 0x25, "%" },
    { 0x26, "&" },
    { 0x27, "'" },
    { 0x28, "(" },
    { 0x29, ")" },
    { 0x2A, "*" },
    { 0x2B, "+" },
    { 0x2C, "," },
    { 0x2D, "-" },
    { 0x2E, "." },
    { 0x2F, "/" },
    { 0x30, "0" },
    { 0x31, "1" },
    { 0x32, "2" },
    { 0x33, "3" },
    { 0x34, "4" },
    { 0x35, "5" },
    { 0x36, "6" },
    { 0x37, "7" },
    { 0x38, "8" },
    { 0x39, "9" },
    { 0x3A, ":" },
    { 0x3B, ";" },
    { 0x3C, "<" },
    { 0x3D, "=" },
    { 0x3E, ">" },
    { 0x3F, "?" },
    { 0x40, "@" },
    { 0x41, "A" },
    { 0x42, "B" },
    { 0x43, "C" },
    { 0x44, "D" },
    { 0x45, "E" },
    { 0x46, "F" },
    { 0x47, "G" },
    { 0x48, "H" },
    { 0x49, "I" },
    { 0x4A, "J" },
    { 0x4B, "K" },
    { 0x4C, "L" },
    { 0x4D, "M" },
    { 0x4E, "N" },
    { 0x4F, "O" },
    { 0x50, "P" },
    { 0x51, "Q" },
    { 0x52, "R" },
    { 0x53, "S" },
    { 0x54, "T" },
    { 0x55, "U" },
    { 0x56, "V" },
    { 0x57, "W" },
    { 0x58, "X" },
    { 0x59, "Y" },
    { 0x5A, "Z" },
    { 0x5B, "[" },
    { 0x5C, "\\" },
    { 0x5D, "]" },
    { 0x5E, "^" },
    { 0x5F, "_" },
    { 0x60, "`" },
    { 0x61, "a" },
    { 0x62, "b" },
    { 0x63, "c" },
    { 0x64, "d" },
    { 0x65, "e" },
    { 0x66, "f" },
    { 0x67, "g" },
    { 0x68, "h" },
    { 0x69, "i" },
    { 0x6A, "j" },
    { 0x6B, "k" },
    { 0x6C, "l" },
    { 0x6D, "m" },
    { 0x6E, "n" },
    { 0x6F, "o" },
    { 0x70, "p" },
    { 0x71, "q" },
    { 0x72, "r" },
    { 0x73, "s" },
    { 0x74, "t" },
    { 0x75, "u" },
    { 0x76, "v" },
    { 0x77, "w" },
    { 0x78, "x" },
    { 0x79, "y" },
    { 0x7A, "z" },
    { 0x7B, "{" },
    { 0x7C, "|" },
    { 0x7D, "}" },
    { 0x7E, "~" },
    { 0x91, "\xE2\x80\x98" },
    { 0x92, "\xE2\x80\x99" },
    { 0x93, "\xE2\x80\x9C" },
    { 0x94, "\xE2\x80\x9D" },
    { 0x95, "\xC2\xB7" },
    { 0x99, "\xE2\x84\xA2" },
    { 0xA1, "\xC2\xA1" },
    { 0xA3, "\xE2\x99\xAA" },
    { 0xBF, "\xC2\xBF" },
    { 0xC9, "\xC3\x89" },
    { 0xE9, "\xC3\xA9" },
};

constexpr const char* kTextColorNames[] = {
    "White", "Red", "Green", "Blue", "Yellow",
};

constexpr const char* kTextInputNames[] = {
    "A", "B", "Left", "Right", "DUp", "DDown", "DLeft", "DRight", "Dpad", "Select", "Start",
};

std::string HexByteString(uint8_t byte) {
    std::string out = "00";
    out[0] = "0123456789ABCDEF"[byte >> 4];
    out[1] = "0123456789ABCDEF"[byte & 0x0F];
    return out;
}

bool ParseHexByteString(const std::string& token, uint8_t& value) {
    return token.size() == 2 && ParseHexByte(token[0], token[1], value);
}

std::vector<std::string> SplitTextCommand(const std::string& value) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t colon = value.find(':', start);
        if (colon == std::string::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, colon - start));
        start = colon + 1;
    }
    return parts;
}

const char* LookupTextChar(uint8_t byte) {
    for (const TextCharMapping& mapping : kTextCharMappings) {
        if (mapping.byte == byte) {
            return mapping.utf8;
        }
    }
    return nullptr;
}

bool LookupTextByte(const std::string& token, uint8_t& value) {
    const TextCharMapping* bestMatch = nullptr;
    size_t bestLength = 0;
    for (const TextCharMapping& mapping : kTextCharMappings) {
        const size_t length = std::char_traits<char>::length(mapping.utf8);
        if (length < bestLength) {
            continue;
        }
        if (token.size() < length) {
            continue;
        }
        if (token.compare(0, length, mapping.utf8) == 0) {
            bestMatch = &mapping;
            bestLength = length;
        }
    }

    if (bestMatch == nullptr) {
        return false;
    }

    value = bestMatch->byte;
    return true;
}

bool DecodeTextCommand(const uint8_t*& cursor, const uint8_t* end, std::string& text, std::string* error) {
    const uint8_t opcode = *cursor++;
    auto need = [&](size_t bytes) -> bool {
        if (static_cast<size_t>(end - cursor) < bytes) {
            SetError(error, "Truncated text command " + HexByteString(opcode) + ".");
            return false;
        }
        return true;
    };

    switch (opcode) {
        case 0x01: {
            if (!need(1)) {
                return false;
            }
            text += "{01:" + HexByteString(*cursor++) + "}";
            return true;
        }
        case 0x02: {
            if (!need(1)) {
                return false;
            }
            const uint8_t color = *cursor++;
            if (color < std::size(kTextColorNames)) {
                text += "{Color:";
                text += kTextColorNames[color];
                text += "}";
            } else {
                text += "{02:" + HexByteString(color) + "}";
            }
            return true;
        }
        case 0x03: {
            if (!need(2)) {
                return false;
            }
            const uint8_t a = *cursor++;
            const uint8_t b = *cursor++;
            text += "{Sound:" + HexByteString(a) + ":" + HexByteString(b) + "}";
            return true;
        }
        case 0x04: {
            if (!need(1)) {
                return false;
            }
            const uint8_t a = *cursor++;
            if (a == 0x10) {
                if (!need(1)) {
                    return false;
                }
                const uint8_t b = *cursor++;
                text += "{04:" + HexByteString(a) + ":" + HexByteString(b) + "}";
            } else {
                text += "{04:" + HexByteString(a) + "}";
            }
            return true;
        }
        case 0x05: {
            if (!need(1)) {
                return false;
            }
            const uint8_t choice = *cursor++;
            if (choice == 0xFF) {
                text += "{Choice:FF}";
                return true;
            }
            if (!need(1)) {
                return false;
            }
            const uint8_t action = *cursor++;
            text += "{Choice:" + HexByteString(choice) + ":" + HexByteString(action) + "}";
            return true;
        }
        case 0x06: {
            if (!need(1)) {
                return false;
            }
            const uint8_t var = *cursor++;
            if (var == 0x00) {
                text += "{Player}";
            } else {
                text += "{Var:" + HexByteString(var) + "}";
            }
            return true;
        }
        case 0x07: {
            if (!need(2)) {
                return false;
            }
            const uint8_t a = *cursor++;
            const uint8_t b = *cursor++;
            text += "{07:" + HexByteString(a) + ":" + HexByteString(b) + "}";
            return true;
        }
        case 0x08: {
            if (!need(1)) {
                return false;
            }
            text += "{08:" + HexByteString(*cursor++) + "}";
            return true;
        }
        case 0x09: {
            if (!need(1)) {
                return false;
            }
            text += "{09:" + HexByteString(*cursor++) + "}";
            return true;
        }
        case 0x0C: {
            if (!need(1)) {
                return false;
            }
            const uint8_t input = *cursor++;
            if (input < std::size(kTextInputNames)) {
                text += "{Key:";
                text += kTextInputNames[input];
                text += "}";
            } else {
                text += "{0C:" + HexByteString(input) + "}";
            }
            return true;
        }
        case 0x0F: {
            if (!need(1)) {
                return false;
            }
            text += "{Symbol:" + HexByteString(*cursor++) + "}";
            return true;
        }
        default:
            text += "{" + HexByteString(opcode) + "}";
            return true;
    }
}

bool EncodeNamedTextCommand(const std::vector<std::string>& parts, std::vector<uint8_t>& textData, std::string* error) {
    const std::string& command = parts[0];

    if (command == "Player") {
        if (parts.size() != 1) {
            SetError(error, "Player command does not take arguments.");
            return false;
        }
        textData.push_back(0x06);
        textData.push_back(0x00);
        return true;
    }

    if (command == "Color") {
        if (parts.size() != 2) {
            SetError(error, "Color command must have exactly one argument.");
            return false;
        }
        for (size_t i = 0; i < std::size(kTextColorNames); ++i) {
            if (parts[1] == kTextColorNames[i]) {
                textData.push_back(0x02);
                textData.push_back(static_cast<uint8_t>(i));
                return true;
            }
        }
        SetError(error, "Unknown color command argument: " + parts[1]);
        return false;
    }

    if (command == "Sound") {
        if (parts.size() != 3) {
            SetError(error, "Sound command must have two byte arguments.");
            return false;
        }
        uint8_t a = 0;
        uint8_t b = 0;
        if (!ParseHexByteString(parts[1], a) || !ParseHexByteString(parts[2], b)) {
            SetError(error, "Invalid Sound command byte.");
            return false;
        }
        textData.push_back(0x03);
        textData.push_back(a);
        textData.push_back(b);
        return true;
    }

    if (command == "Choice") {
        if (parts.size() != 2 && parts.size() != 3) {
            SetError(error, "Choice command must have one or two byte arguments.");
            return false;
        }
        uint8_t choice = 0;
        if (!ParseHexByteString(parts[1], choice)) {
            SetError(error, "Invalid Choice command byte.");
            return false;
        }
        textData.push_back(0x05);
        textData.push_back(choice);
        if (choice != 0xFF) {
            if (parts.size() != 3) {
                SetError(error, "Choice command missing action byte.");
                return false;
            }
            uint8_t action = 0;
            if (!ParseHexByteString(parts[2], action)) {
                SetError(error, "Invalid Choice action byte.");
                return false;
            }
            textData.push_back(action);
        }
        return true;
    }

    if (command == "Var") {
        if (parts.size() != 2) {
            SetError(error, "Var command must have one byte argument.");
            return false;
        }
        const unsigned long value = std::strtoul(parts[1].c_str(), nullptr, 16);
        if (value > 0xFF) {
            SetError(error, "Var command out of range.");
            return false;
        }
        textData.push_back(0x06);
        textData.push_back(static_cast<uint8_t>(value));
        return true;
    }

    if (command == "Key") {
        if (parts.size() != 2) {
            SetError(error, "Key command must have one argument.");
            return false;
        }
        for (size_t i = 0; i < std::size(kTextInputNames); ++i) {
            if (parts[1] == kTextInputNames[i]) {
                textData.push_back(0x0C);
                textData.push_back(static_cast<uint8_t>(i));
                return true;
            }
        }
        SetError(error, "Unknown key command argument: " + parts[1]);
        return false;
    }

    if (command == "Symbol") {
        if (parts.size() != 2) {
            SetError(error, "Symbol command must have one byte argument.");
            return false;
        }
        uint8_t value = 0;
        if (!ParseHexByteString(parts[1], value)) {
            SetError(error, "Invalid Symbol command byte.");
            return false;
        }
        textData.push_back(0x0F);
        textData.push_back(value);
        return true;
    }

    return false;
}

bool EncodeGenericTextCommand(const std::vector<std::string>& parts, std::vector<uint8_t>& textData,
                              std::string* error) {
    if (parts.empty()) {
        SetError(error, "Empty text command.");
        return false;
    }

    for (const std::string& part : parts) {
        uint8_t byte = 0;
        if (!ParseHexByteString(part, byte)) {
            SetError(error, "Invalid generic text command byte: " + part);
            return false;
        }
        textData.push_back(byte);
    }
    return true;
}

bool ParseLegacyEditableTextString(const std::string& value, std::vector<uint8_t>& textData, std::string* error) {
    textData.clear();

    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '\\') {
            if (i + 1 >= value.size()) {
                SetError(error, "Dangling escape in editable text.");
                return false;
            }

            const char escaped = value[++i];
            switch (escaped) {
                case 'n':
                    textData.push_back('\n');
                    break;
                case 'r':
                    textData.push_back('\r');
                    break;
                case 't':
                    textData.push_back('\t');
                    break;
                case '\\':
                    textData.push_back('\\');
                    break;
                case '<':
                    textData.push_back('<');
                    break;
                default:
                    SetError(error, "Unsupported escape in editable text.");
                    return false;
            }
            continue;
        }

        if (c == '<' && i + 3 < value.size() && value[i + 3] == '>') {
            uint8_t byte = 0;
            if (ParseHexByte(value[i + 1], value[i + 2], byte)) {
                textData.push_back(byte);
                i += 3;
                continue;
            }
        }

        textData.push_back(static_cast<uint8_t>(static_cast<unsigned char>(c)));
    }

    textData.push_back(0);
    return true;
}

std::string FormatEditableText(const std::vector<uint8_t>& textData) {
    std::string decoded;
    std::string error;
    if (PortAssetPipeline::DecodeTmcText(textData.data(), textData.size(), decoded, nullptr, &error)) {
        return decoded;
    }

    std::ostringstream fallback;
    for (uint8_t byte : textData) {
        if (byte == 0) {
            break;
        }
        fallback << '<' << HexByteString(byte) << '>';
    }
    return fallback.str();
}

bool ParseEditableTextString(const std::string& value, std::vector<uint8_t>& textData, std::string* error) {
    if (PortAssetPipeline::EncodeTmcText(value, textData, error)) {
        return true;
    }
    return ParseLegacyEditableTextString(value, textData, error);
}

bool CopyFilePreserveRelative(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                              const std::filesystem::path& relativePath, std::string* error = nullptr) {
    const std::filesystem::path sourcePath = sourceRoot / relativePath;
    const std::filesystem::path outputPath = outputRoot / relativePath;

    PortAssetLog::EnsureDir(outputPath.parent_path());

    std::error_code ec;
    std::filesystem::copy_file(sourcePath, outputPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        SetError(error, "Failed to copy " + sourcePath.string() + " to " + outputPath.string() + ": " + ec.message());
        return false;
    }
    return true;
}

nlohmann::json BuildSourceState(const std::filesystem::path& sourceRoot) {
    nlohmann::json state;
    state["format"] = "tmc_asset_build_state_v1";
    state["builder_version"] = kBuildStateVersion;
    state["files"] = nlohmann::json::array();

    /* Two-phase: enumerate the tree (cheap, single-threaded — recursive
     * directory iterator can't be parallelised cleanly), then stat every
     * file in parallel. The per-file stat() pair (file_size + last_write_time)
     * is what dominates wall time on Windows; parallelism scales it out
     * across the worker pool. */
    struct Entry {
        std::filesystem::path absolute;
        std::string relative;
    };
    std::vector<Entry> entries;
    entries.reserve(8192);
    for (const auto& dirent : std::filesystem::recursive_directory_iterator(sourceRoot)) {
        if (!dirent.is_regular_file()) {
            continue;
        }
        Entry e;
        e.absolute = dirent.path();
        e.relative = std::filesystem::relative(dirent.path(), sourceRoot).generic_string();
        entries.push_back(std::move(e));
    }

    struct StatResult {
        std::string path;
        uint64_t size;
        int64_t mtime;
    };
    std::vector<StatResult> results(entries.size());
    PortAssetLog::ParallelFor<size_t>(0, entries.size(), [&](size_t i) {
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(entries[i].absolute, ec);
        const auto t = std::filesystem::last_write_time(entries[i].absolute, ec);
        results[i].path = std::move(entries[i].relative);
        results[i].size = ec ? 0 : size;
        results[i].mtime = ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
    });

    std::sort(results.begin(), results.end(), [](const StatResult& a, const StatResult& b) { return a.path < b.path; });

    for (auto& r : results) {
        nlohmann::json fileState;
        fileState["path"] = std::move(r.path);
        fileState["size"] = r.size;
        fileState["mtime"] = r.mtime;
        state["files"].push_back(std::move(fileState));
    }

    return state;
}

bool BuildRuntimePaletteFiles(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                              const nlohmann::json& sourcePalettes, nlohmann::json& runtimePalettes,
                              std::unordered_map<std::string, std::string>& paletteFileMap, std::string* error) {
    runtimePalettes = nlohmann::json::array();
    paletteFileMap.clear();

    const size_t total = sourcePalettes.size();
    std::vector<nlohmann::json> entries(total);
    std::vector<std::pair<std::string, std::string>> mappings(total);

    std::mutex error_mu;
    std::string sticky_error;

    PortAssetLog::ParallelFor<size_t>(0, total, [&](size_t i) {
        const auto& sourceEntry = sourcePalettes[i];
        const std::string sourceFile = JsonStringOrEmpty(sourceEntry, "file");
        if (sourceFile.empty()) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (sticky_error.empty())
                sticky_error = "Palette entry missing file path.";
            return;
        }

        std::string local_err;
        std::vector<uint8_t> paletteData;
        if (!ReadPaletteJson(sourceRoot / sourceFile, paletteData, &local_err)) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (sticky_error.empty())
                sticky_error = local_err;
            return;
        }

        if (sourceEntry.contains("size") && sourceEntry["size"].is_number_unsigned()) {
            const size_t expectedSize = sourceEntry["size"].get<size_t>();
            if (paletteData.size() != expectedSize) {
                std::lock_guard<std::mutex> lk(error_mu);
                if (sticky_error.empty())
                    sticky_error = "Palette size mismatch for " + sourceFile;
                return;
            }
        }

        const std::filesystem::path runtimeRelativeFile = ReplaceExtension(std::filesystem::path(sourceFile), ".pal");
        if (!WriteBinaryFile(outputRoot / runtimeRelativeFile, paletteData, &local_err)) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (sticky_error.empty())
                sticky_error = local_err;
            return;
        }

        nlohmann::json runtimeEntry = sourceEntry;
        runtimeEntry["file"] = runtimeRelativeFile.generic_string();
        runtimeEntry["size"] = paletteData.size();
        entries[i] = std::move(runtimeEntry);
        mappings[i] = { sourceFile, runtimeRelativeFile.generic_string() };
    });

    if (!sticky_error.empty()) {
        SetError(error, sticky_error);
        return false;
    }

    for (auto& e : entries) {
        runtimePalettes.push_back(std::move(e));
    }
    for (auto& m : mappings) {
        if (!m.first.empty()) {
            paletteFileMap.emplace(std::move(m.first), std::move(m.second));
        }
    }

    return true;
}

bool BuildRuntimePaletteGroups(const nlohmann::json& sourceGroups,
                               const std::unordered_map<std::string, std::string>& paletteFileMap,
                               nlohmann::json& runtimeGroups) {
    runtimeGroups = nlohmann::json::object();

    for (auto it = sourceGroups.begin(); it != sourceGroups.end(); ++it) {
        nlohmann::json runtimeGroup = it.value();
        if (!runtimeGroup.contains("entries") || !runtimeGroup["entries"].is_array()) {
            continue;
        }

        for (auto& runtimeEntry : runtimeGroup["entries"]) {
            if (!runtimeEntry.contains("palette_files") || !runtimeEntry["palette_files"].is_array()) {
                continue;
            }

            for (auto& paletteRef : runtimeEntry["palette_files"]) {
                const std::string sourceFile = JsonStringOrEmpty(paletteRef, "file");
                if (sourceFile.empty()) {
                    continue;
                }

                auto found = paletteFileMap.find(sourceFile);
                if (found != paletteFileMap.end()) {
                    paletteRef["file"] = found->second;
                } else {
                    paletteRef["file"] = ReplaceExtension(std::filesystem::path(sourceFile), ".pal").generic_string();
                }
            }
        }

        runtimeGroups[it.key()] = runtimeGroup;
    }

    return true;
}

bool BuildRuntimeGfxFiles(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                          const nlohmann::json& sourceGroups, nlohmann::json& runtimeGroups, std::string* error) {
    runtimeGroups = nlohmann::json::object();

    // Flatten (groupKey, elementIndex) pairs so we can process every gfx
    // element in parallel. Decoding the BMP and re-encoding to GBA tiled
    // bytes is the dominant cost of the legacy editable->runtime path.
    struct WorkItem {
        std::string key;
        size_t group_index;
        size_t element_index;
    };

    std::vector<std::string> keys;
    std::vector<const nlohmann::json*> groups;
    keys.reserve(sourceGroups.size());
    groups.reserve(sourceGroups.size());
    for (auto it = sourceGroups.begin(); it != sourceGroups.end(); ++it) {
        keys.push_back(it.key());
        groups.push_back(&it.value());
    }

    std::vector<std::vector<nlohmann::json>> results(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        results[i].resize(groups[i]->size());
    }

    std::vector<WorkItem> work;
    for (size_t i = 0; i < keys.size(); ++i) {
        for (size_t j = 0; j < groups[i]->size(); ++j) {
            work.push_back({ keys[i], i, j });
        }
    }

    std::mutex error_mu;
    std::string sticky_error;

    PortAssetLog::ParallelFor<size_t>(0, work.size(), [&](size_t k) {
        const WorkItem& item = work[k];
        const nlohmann::json& sourceEntry = (*groups[item.group_index])[item.element_index];

        nlohmann::json runtimeEntry;
        runtimeEntry["unknown"] = sourceEntry.value("unknown", 0);
        runtimeEntry["dest"] = sourceEntry.value("dest", 0u);
        runtimeEntry["terminator"] = sourceEntry.value("terminator", false);

        const std::string sourceFile = JsonStringOrEmpty(sourceEntry, "file");
        if (sourceFile.empty()) {
            runtimeEntry["file"] = nullptr;
            results[item.group_index][item.element_index] = std::move(runtimeEntry);
            return;
        }

        const uint16_t width = static_cast<uint16_t>(sourceEntry.value("width", 0));
        const uint16_t height = static_cast<uint16_t>(sourceEntry.value("height", 0));
        const uint8_t bpp = static_cast<uint8_t>(sourceEntry.value("bpp", 4));
        if (width == 0 || height == 0 || (bpp != 4 && bpp != 8)) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (sticky_error.empty())
                sticky_error = "Invalid gfx metadata for " + sourceFile;
            return;
        }

        std::string local_err;
        std::vector<uint8_t> pixels;
        if (!ReadEditableBmp(sourceRoot / sourceFile, pixels, width, height, bpp, &local_err)) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (sticky_error.empty())
                sticky_error = local_err;
            return;
        }

        std::vector<uint8_t> gfxData = EncodeGbaTiledGfx(pixels, width, height, bpp);
        if (gfxData.empty()) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (sticky_error.empty())
                sticky_error = "Failed to encode gfx from " + sourceFile;
            return;
        }

        if (sourceEntry.contains("size") && sourceEntry["size"].is_number_unsigned()) {
            const size_t expectedSize = sourceEntry["size"].get<size_t>();
            if (gfxData.size() < expectedSize) {
                std::lock_guard<std::mutex> lk(error_mu);
                if (sticky_error.empty())
                    sticky_error = "Gfx size mismatch for " + sourceFile;
                return;
            }
            gfxData.resize(expectedSize);
        }

        const std::filesystem::path runtimeRelativeFile = ReplaceExtension(std::filesystem::path(sourceFile), ".bin");
        if (!WriteBinaryFile(outputRoot / runtimeRelativeFile, gfxData, &local_err)) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (sticky_error.empty())
                sticky_error = local_err;
            return;
        }

        runtimeEntry["file"] = runtimeRelativeFile.generic_string();
        runtimeEntry["size"] = gfxData.size();
        results[item.group_index][item.element_index] = std::move(runtimeEntry);
    });

    if (!sticky_error.empty()) {
        SetError(error, sticky_error);
        return false;
    }

    for (size_t i = 0; i < keys.size(); ++i) {
        nlohmann::json group_array = nlohmann::json::array();
        for (auto& e : results[i]) {
            group_array.push_back(std::move(e));
        }
        runtimeGroups[keys[i]] = std::move(group_array);
    }

    return true;
}

bool BuildRuntimeTexts(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                       const nlohmann::json& sourceTexts, nlohmann::json& runtimeTexts, std::string* error) {
    runtimeTexts = nlohmann::json::object();

    std::error_code ec;
    std::filesystem::remove_all(outputRoot / "texts", ec);

    struct CompiledTextFile {
        std::string runtimeFile;
        size_t size;
        std::string preview;
    };

    std::unordered_map<std::string, CompiledTextFile> compiledTexts;

    auto writeLe32 = [](std::vector<uint8_t>& buffer, size_t offset, uint32_t value) {
        if (offset + 4 > buffer.size()) {
            return;
        }
        buffer[offset + 0] = static_cast<uint8_t>(value & 0xFF);
        buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    };

    auto buildPackedTranslationTable = [&](const nlohmann::json& language, std::vector<uint8_t>& outData) -> bool {
        outData.clear();

        const uint32_t categoryCount = language.value("category_count", 0u);
        if (categoryCount == 0 || !language.contains("categories") || !language["categories"].is_object()) {
            return false;
        }

        outData.resize(static_cast<size_t>(categoryCount) * 4, 0);

        for (auto catIt = language["categories"].begin(); catIt != language["categories"].end(); ++catIt) {
            const uint32_t categoryIndex = static_cast<uint32_t>(std::stoul(catIt.key()));
            if (categoryIndex >= categoryCount) {
                continue;
            }

            const nlohmann::json& category = catIt.value();
            const uint32_t messageCount = category.value("message_count", 0u);
            std::vector<uint8_t> categoryData(static_cast<size_t>(messageCount) * 4, 0);
            size_t categoryWritePos = categoryData.size();

            if (category.contains("messages") && category["messages"].is_array()) {
                for (const auto& message : category["messages"]) {
                    const uint32_t messageIndex = message.value("index", 0u);
                    if (messageIndex >= messageCount) {
                        continue;
                    }

                    const std::string runtimeFile = JsonStringOrEmpty(message, "file");
                    if (runtimeFile.empty()) {
                        continue;
                    }

                    std::vector<uint8_t> messageData;
                    if (!ReadBinaryFile(outputRoot / runtimeFile, messageData, error)) {
                        return false;
                    }

                    writeLe32(categoryData, static_cast<size_t>(messageIndex) * 4,
                              static_cast<uint32_t>(categoryWritePos));
                    categoryData.insert(categoryData.end(), messageData.begin(), messageData.end());
                    categoryWritePos += messageData.size();
                }
            }

            while ((categoryData.size() & 0xF) != 0) {
                categoryData.push_back(0xFF);
            }

            writeLe32(outData, static_cast<size_t>(categoryIndex) * 4, static_cast<uint32_t>(outData.size()));
            outData.insert(outData.end(), categoryData.begin(), categoryData.end());
        }

        while ((outData.size() & 0xF) != 0) {
            outData.push_back(0xFF);
        }

        return true;
    };

    for (auto langIt = sourceTexts.begin(); langIt != sourceTexts.end(); ++langIt) {
        const std::string sourceLanguageKey = langIt.key();
        nlohmann::json language = langIt.value();
        if (!language.contains("categories") || !language["categories"].is_object()) {
            continue;
        }

        for (auto catIt = language["categories"].begin(); catIt != language["categories"].end(); ++catIt) {
            nlohmann::json& category = catIt.value();
            if (!category.contains("messages") || !category["messages"].is_array()) {
                continue;
            }

            for (auto& message : category["messages"]) {
                const std::string sourceFile = JsonStringOrEmpty(message, "file");
                if (sourceFile.empty()) {
                    continue;
                }

                auto compiled = compiledTexts.find(sourceFile);
                if (compiled == compiledTexts.end()) {
                    std::vector<uint8_t> textData;
                    if (!ReadEditableText(sourceRoot / sourceFile, textData, error)) {
                        SetError(error,
                                 "Failed to build runtime text from " + sourceFile + ": " + (error ? *error : ""));
                        return false;
                    }

                    const std::filesystem::path runtimeRelativeFile =
                        ReplaceExtension(std::filesystem::path(sourceFile), ".bin");
                    if (!WriteBinaryFile(outputRoot / runtimeRelativeFile, textData, error)) {
                        return false;
                    }

                    CompiledTextFile fileInfo;
                    fileInfo.runtimeFile = runtimeRelativeFile.generic_string();
                    fileInfo.size = textData.size();
                    fileInfo.preview = FormatEditableText(textData);
                    compiled = compiledTexts.emplace(sourceFile, std::move(fileInfo)).first;
                }

                message["file"] = compiled->second.runtimeFile;
                message["size"] = compiled->second.size;
                message["preview"] = compiled->second.preview;
            }
        }

        std::vector<std::string> targetLanguageKeys;
        if (language.contains("engine_slots") && language["engine_slots"].is_array()) {
            for (const auto& slot : language["engine_slots"]) {
                if (slot.is_number_unsigned()) {
                    targetLanguageKeys.push_back(std::to_string(slot.get<uint32_t>()));
                }
            }
        }
        if (targetLanguageKeys.empty()) {
            targetLanguageKeys.push_back(sourceLanguageKey);
        }

        std::vector<uint8_t> packedTable;
        if (!buildPackedTranslationTable(language, packedTable)) {
            SetError(error, "Failed to build packed text table for language " + sourceLanguageKey);
            return false;
        }

        const std::filesystem::path tableRelativeFile =
            std::filesystem::path("texts") / "tables" / ("language_" + sourceLanguageKey + ".bin");
        if (!WriteBinaryFile(outputRoot / tableRelativeFile, packedTable, error)) {
            return false;
        }

        language["table_file"] = tableRelativeFile.generic_string();
        language["table_size"] = packedTable.size();
        language.erase("engine_slots");
        for (const std::string& targetKey : targetLanguageKeys) {
            runtimeTexts[targetKey] = language;
        }
    }

    return true;
}

bool BuildRuntimeSpritePtrs(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                            const nlohmann::json& sourceSpritePtrs, nlohmann::json& runtimeSpritePtrs,
                            std::string* error) {
    runtimeSpritePtrs = sourceSpritePtrs;

    std::mutex anim_mu, frames_mu, ptr_mu, error_mu;
    std::unordered_map<std::string, std::string> compiledAnimations;
    std::unordered_map<std::string, std::string> compiledFrameFiles;
    std::unordered_map<std::string, std::string> compiledPtrFiles;
    std::string sticky_error;

    auto fail = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lk(error_mu);
        if (sticky_error.empty())
            sticky_error = msg;
    };

    PortAssetLog::ParallelFor<size_t>(0, runtimeSpritePtrs.size(), [&](size_t i) {
        nlohmann::json& entry = runtimeSpritePtrs[i];

        if (entry.contains("animations") && entry["animations"].is_array()) {
            for (auto& animRef : entry["animations"]) {
                if (!animRef.is_string())
                    continue;
                const std::string sourceFile = animRef.get<std::string>();
                const std::filesystem::path sourcePath = sourceFile;
                if (sourcePath.extension() != ".json")
                    continue;

                std::string mapped;
                {
                    std::lock_guard<std::mutex> lk(anim_mu);
                    auto found = compiledAnimations.find(sourceFile);
                    if (found != compiledAnimations.end()) {
                        mapped = found->second;
                    }
                }

                if (mapped.empty()) {
                    std::string local_err;
                    std::vector<uint8_t> animationData;
                    if (!ReadEditableAnimation(sourceRoot / sourcePath, animationData, &local_err)) {
                        fail(local_err);
                        return;
                    }
                    const std::string runtimeFile = ReplaceExtension(sourcePath, ".bin").generic_string();
                    if (!WriteBinaryFile(outputRoot / runtimeFile, animationData, &local_err)) {
                        fail(local_err);
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lk(anim_mu);
                        auto inserted = compiledAnimations.emplace(sourceFile, runtimeFile);
                        mapped = inserted.first->second;
                    }
                }
                animRef = mapped;
            }
        }

        const std::string framesFile = JsonStringOrEmpty(entry, "frames_file");
        if (!framesFile.empty()) {
            const std::filesystem::path sourcePath = framesFile;
            if (sourcePath.extension() == ".json") {
                std::string mapped;
                {
                    std::lock_guard<std::mutex> lk(frames_mu);
                    auto found = compiledFrameFiles.find(framesFile);
                    if (found != compiledFrameFiles.end())
                        mapped = found->second;
                }
                if (mapped.empty()) {
                    std::string local_err;
                    std::vector<uint8_t> frameData;
                    if (!ReadEditableSpriteFrames(sourceRoot / sourcePath, frameData, &local_err)) {
                        fail(local_err);
                        return;
                    }
                    const std::string runtimeFile = ReplaceExtension(sourcePath, ".bin").generic_string();
                    if (!WriteBinaryFile(outputRoot / runtimeFile, frameData, &local_err)) {
                        fail(local_err);
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lk(frames_mu);
                        auto inserted = compiledFrameFiles.emplace(framesFile, runtimeFile);
                        mapped = inserted.first->second;
                    }
                }
                entry["frames_file"] = mapped;
            }
        }

        const std::string ptrFile = JsonStringOrEmpty(entry, "ptr_file");
        if (!ptrFile.empty()) {
            const std::filesystem::path sourcePath = ptrFile;
            if (sourcePath.extension() == ".bmp") {
                std::string mapped;
                {
                    std::lock_guard<std::mutex> lk(ptr_mu);
                    auto found = compiledPtrFiles.find(ptrFile);
                    if (found != compiledPtrFiles.end())
                        mapped = found->second;
                }
                if (mapped.empty()) {
                    const uint16_t width = static_cast<uint16_t>(entry.value("ptr_width", 0));
                    const uint16_t height = static_cast<uint16_t>(entry.value("ptr_height", 0));
                    const uint8_t bpp = static_cast<uint8_t>(entry.value("ptr_bpp", 4));
                    const size_t expectedSize = static_cast<size_t>(entry.value("ptr_size", 0u));
                    if (width == 0 || height == 0 || bpp != 4) {
                        fail("Invalid sprite gfx metadata for " + ptrFile);
                        return;
                    }

                    std::string local_err;
                    std::vector<uint8_t> pixels;
                    if (!ReadEditableBmp(sourceRoot / sourcePath, pixels, width, height, bpp, &local_err)) {
                        fail(local_err);
                        return;
                    }

                    std::vector<uint8_t> gfxData = EncodeGbaTiledGfx(pixels, width, height, bpp);
                    if (gfxData.empty()) {
                        fail("Failed to encode sprite gfx from " + ptrFile);
                        return;
                    }

                    if (expectedSize != 0) {
                        if (gfxData.size() < expectedSize) {
                            fail("Sprite gfx size mismatch for " + ptrFile);
                            return;
                        }
                        gfxData.resize(expectedSize);
                    }

                    std::string runtimeFile = JsonStringOrEmpty(entry, "ptr_runtime_file");
                    if (runtimeFile.empty()) {
                        runtimeFile = ReplaceExtension(sourcePath, ".4bpp").generic_string();
                    }

                    if (!WriteBinaryFile(outputRoot / runtimeFile, gfxData, &local_err)) {
                        fail(local_err);
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lk(ptr_mu);
                        auto inserted = compiledPtrFiles.emplace(ptrFile, runtimeFile);
                        mapped = inserted.first->second;
                    }
                }
                entry["ptr_file"] = mapped;
            }
        }

        entry.erase("ptr_runtime_file");
        entry.erase("ptr_width");
        entry.erase("ptr_height");
        entry.erase("ptr_bpp");
        entry.erase("ptr_size");
    });

    if (!sticky_error.empty()) {
        SetError(error, sticky_error);
        return false;
    }

    return true;
}

bool CopyRuntimePassthroughAssets(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                                  std::string* error) {
    static const std::filesystem::path kDirectories[] = {
        "tilemaps",
        "maps",
        "assets",
        "animations",
        "sprites",
        "room_properties",
        "generated",
        /* data_* trees hold raw GBA data referenced indirectly by compiled
         * code (e.g. data_080D5360/gUnk_additional_*.bin is the room-property
         * data that LonLonRanch doors and Minish Forest lily pads index into).
         * Without these, rooms silently misbehave. */
        "data_08000360",
        "data_08000F54",
        "data_080029B4",
        "data_08007DF4",
        "data_080B2A70",
        "data_080B4410",
        "data_080B7B74",
        "data_080D5360",
        "data_080FC8A4",
        "data_08108E6C",
        "data_0811BE38",
        "data_08127280",
    };
    static const std::filesystem::path kFiles[] = {
        "tilemaps.json",       "area_room_headers.json", "area_tile_sets.json",
        "area_room_maps.json", "area_tiles.json",        "area_tables.json",
    };

    // Collect first, then copy in parallel. The recursive iterator is cheap;
    // the per-file CreateFile/CloseFile pair is what dominates wall time on
    // Windows, and that parallelises trivially.
    std::vector<std::filesystem::path> to_copy;
    for (const auto& relativeDir : kDirectories) {
        const std::filesystem::path sourceDir = sourceRoot / relativeDir;
        if (!std::filesystem::exists(sourceDir)) {
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), sourceRoot);
            const std::string relativePathString = relativePath.generic_string();
            if ((relativePathString.rfind("animations/", 0) == 0 || relativePathString.rfind("sprites/", 0) == 0 ||
                 relativePathString.rfind("playerItemGustJar/", 0) == 0) &&
                (relativePath.extension() == ".json" || relativePath.extension() == ".bmp")) {
                continue;
            }
            to_copy.push_back(relativePath);
        }
    }

    for (const auto& relativeFile : kFiles) {
        if (std::filesystem::exists(sourceRoot / relativeFile)) {
            to_copy.push_back(relativeFile);
        }
    }

    std::mutex error_mu;
    std::string sticky_error;
    PortAssetLog::ParallelFor<size_t>(0, to_copy.size(), [&](size_t i) {
        std::string local_err;
        if (!CopyFilePreserveRelative(sourceRoot, outputRoot, to_copy[i], &local_err)) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (sticky_error.empty())
                sticky_error = local_err;
        }
    });

    if (!sticky_error.empty()) {
        SetError(error, sticky_error);
        return false;
    }

    return true;
}

} // namespace

std::vector<uint8_t> DecodeGbaTiledGfx(std::span<const uint8_t> gfxData, uint16_t width, uint16_t height, uint8_t bpp) {
    if (gfxData.empty() || width == 0 || height == 0 || width % 8 != 0 || height % 8 != 0) {
        return {};
    }

    if (bpp != 4 && bpp != 8) {
        return {};
    }

    const size_t tileSize = (bpp == 8) ? 64 : 32;
    const uint32_t tilesWide = width / 8;
    const uint32_t maxTiles = (height / 8) * tilesWide;
    const size_t tileCount = std::min<size_t>(gfxData.size() / tileSize, maxTiles);

    std::vector<uint8_t> pixels(width * height, 0);
    for (size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
        const uint32_t tileX = static_cast<uint32_t>(tileIndex % tilesWide);
        const uint32_t tileY = static_cast<uint32_t>(tileIndex / tilesWide);
        const size_t tileOffset = tileIndex * tileSize;

        for (uint32_t y = 0; y < 8; ++y) {
            const uint32_t destY = tileY * 8 + y;
            const uint32_t rowBase = destY * width + tileX * 8;

            if (bpp == 4) {
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint8_t packed = gfxData[tileOffset + y * 4 + x];
                    pixels[rowBase + x * 2] = packed & 0x0F;
                    pixels[rowBase + x * 2 + 1] = packed >> 4;
                }
            } else {
                for (uint32_t x = 0; x < 8; ++x) {
                    pixels[rowBase + x] = gfxData[tileOffset + y * 8 + x];
                }
            }
        }
    }

    return pixels;
}

std::vector<uint8_t> EncodeGbaTiledGfx(const std::vector<uint8_t>& pixels, uint16_t width, uint16_t height,
                                       uint8_t bpp) {
    if (pixels.size() != static_cast<size_t>(width) * height || width == 0 || height == 0 || width % 8 != 0 ||
        height % 8 != 0) {
        return {};
    }

    if (bpp != 4 && bpp != 8) {
        return {};
    }

    const uint8_t maxIndex = static_cast<uint8_t>((1u << bpp) - 1u);
    const uint32_t tilesWide = width / 8;
    const uint32_t tilesHigh = height / 8;
    const size_t tileSize = (bpp == 8) ? 64 : 32;
    std::vector<uint8_t> gfxData(static_cast<size_t>(tilesWide) * tilesHigh * tileSize, 0);

    for (uint32_t tileY = 0; tileY < tilesHigh; ++tileY) {
        for (uint32_t tileX = 0; tileX < tilesWide; ++tileX) {
            const size_t tileIndex = static_cast<size_t>(tileY) * tilesWide + tileX;
            const size_t tileOffset = tileIndex * tileSize;

            for (uint32_t y = 0; y < 8; ++y) {
                const uint32_t rowBase = (tileY * 8 + y) * width + tileX * 8;
                if (bpp == 4) {
                    for (uint32_t x = 0; x < 4; ++x) {
                        const uint8_t lo = std::min<uint8_t>(pixels[rowBase + x * 2], maxIndex);
                        const uint8_t hi = std::min<uint8_t>(pixels[rowBase + x * 2 + 1], maxIndex);
                        gfxData[tileOffset + y * 4 + x] = static_cast<uint8_t>(lo | (hi << 4));
                    }
                } else {
                    for (uint32_t x = 0; x < 8; ++x) {
                        gfxData[tileOffset + y * 8 + x] = std::min<uint8_t>(pixels[rowBase + x], maxIndex);
                    }
                }
            }
        }
    }

    return gfxData;
}

bool WriteIndexedBmp(const std::filesystem::path& outputPath, std::span<const uint8_t> pixels, uint16_t width,
                     uint16_t height, uint8_t bpp, std::string* error) {
    if (pixels.size() != static_cast<size_t>(width) * height) {
        SetError(error, "Invalid BMP pixel buffer size.");
        return false;
    }

    if (bpp != 4 && bpp != 8) {
        SetError(error, "Only 4bpp and 8bpp indexed BMP export is supported.");
        return false;
    }

    const uint8_t maxIndex = static_cast<uint8_t>((1u << bpp) - 1u);
    for (uint8_t pixel : pixels) {
        if (pixel > maxIndex) {
            SetError(error, "BMP pixel index out of range.");
            return false;
        }
    }

    const uint32_t rowStride = (static_cast<uint32_t>(width) + 3u) & ~3u;
    const uint32_t colorTableSize = 256u * 4u;
    const uint32_t pixelDataOffset = 14u + 40u + colorTableSize;
    const uint32_t imageSize = rowStride * height;
    const uint32_t fileSize = pixelDataOffset + imageSize;

    PortAssetLog::EnsureDir(outputPath.parent_path());
    std::ofstream output(outputPath, std::ios::binary);
    if (!output.good()) {
        SetError(error, "Could not write BMP file: " + outputPath.string());
        return false;
    }

    auto write16 = [&output](uint16_t value) { output.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    auto write32 = [&output](uint32_t value) { output.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    auto writeS32 = [&output](int32_t value) { output.write(reinterpret_cast<const char*>(&value), sizeof(value)); };

    write16(0x4D42);
    write32(fileSize);
    write32(0);
    write32(pixelDataOffset);

    write32(40);
    writeS32(width);
    writeS32(height);
    write16(1);
    write16(8);
    write32(0);
    write32(imageSize);
    writeS32(2835);
    writeS32(2835);
    write32(256);
    write32(0);

    for (uint32_t i = 0; i < 256; ++i) {
        const uint8_t c = static_cast<uint8_t>(i);
        output.put(static_cast<char>(c));
        output.put(static_cast<char>(c));
        output.put(static_cast<char>(c));
        output.put(0);
    }

    std::vector<uint8_t> row(rowStride, 0);
    for (int32_t y = height - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        for (uint16_t x = 0; x < width; ++x) {
            row[x] = pixels[static_cast<size_t>(y) * width + x];
        }
        output.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }

    return output.good();
}

bool ReadEditableBmp(const std::filesystem::path& inputPath, std::vector<uint8_t>& pixels, uint16_t expectedWidth,
                     uint16_t expectedHeight, uint8_t bpp, std::string* error) {
    std::vector<uint8_t> data;
    if (!ReadBinaryFile(inputPath, data, error)) {
        return false;
    }

    if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') {
        SetError(error, "Invalid BMP header: " + inputPath.string());
        return false;
    }

    const auto read16 = [&data](size_t offset) -> uint16_t {
        return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
    };
    const auto read32 = [&data](size_t offset) -> uint32_t {
        return static_cast<uint32_t>(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) |
                                     (data[offset + 3] << 24));
    };
    const auto readS32 = [&read32](size_t offset) -> int32_t { return static_cast<int32_t>(read32(offset)); };

    const uint32_t pixelDataOffset = read32(10);
    const uint32_t dibSize = read32(14);
    const int32_t width = readS32(18);
    const int32_t height = readS32(22);
    const uint16_t planes = read16(26);
    const uint16_t bitsPerPixel = read16(28);
    const uint32_t compression = read32(30);

    if (dibSize < 40 || planes != 1 || compression != 0 || width <= 0 || height == 0) {
        SetError(error, "Unsupported BMP format: " + inputPath.string());
        return false;
    }

    const uint32_t absHeight = static_cast<uint32_t>(height < 0 ? -height : height);
    if (width != expectedWidth || absHeight != expectedHeight) {
        SetError(error, "BMP dimensions mismatch: " + inputPath.string());
        return false;
    }

    const uint8_t maxIndex = static_cast<uint8_t>((1u << bpp) - 1u);
    const uint32_t rowStride = ((static_cast<uint32_t>(width) * bitsPerPixel + 31u) / 32u) * 4u;
    if (pixelDataOffset + rowStride * absHeight > data.size()) {
        SetError(error, "BMP pixel data truncated: " + inputPath.string());
        return false;
    }

    pixels.assign(static_cast<size_t>(width) * absHeight, 0);

    for (uint32_t y = 0; y < absHeight; ++y) {
        const uint32_t srcY = (height > 0) ? (absHeight - 1 - y) : y;
        const uint8_t* row = data.data() + pixelDataOffset + rowStride * srcY;
        for (uint32_t x = 0; x < static_cast<uint32_t>(width); ++x) {
            uint8_t index = 0;

            if (bitsPerPixel == 8) {
                index = row[x];
                if (index > maxIndex) {
                    SetError(error, "BMP pixel index out of range in " + inputPath.string());
                    return false;
                }
            } else if (bitsPerPixel == 24) {
                const uint8_t b = row[x * 3 + 0];
                const uint8_t g = row[x * 3 + 1];
                const uint8_t r = row[x * 3 + 2];
                const uint32_t gray = (static_cast<uint32_t>(r) + g + b) / 3u;
                index = static_cast<uint8_t>((gray * maxIndex + 127u) / 255u);
            } else if (bitsPerPixel == 32) {
                const uint8_t b = row[x * 4 + 0];
                const uint8_t g = row[x * 4 + 1];
                const uint8_t r = row[x * 4 + 2];
                const uint32_t gray = (static_cast<uint32_t>(r) + g + b) / 3u;
                index = static_cast<uint8_t>((gray * maxIndex + 127u) / 255u);
            } else {
                SetError(error, "Unsupported BMP bit depth in " + inputPath.string());
                return false;
            }

            pixels[static_cast<size_t>(y) * width + x] = index;
        }
    }

    return true;
}

bool WritePaletteJson(const std::filesystem::path& outputPath, std::span<const uint8_t> paletteData,
                      std::string* error) {
    if (paletteData.size() % 32 != 0) {
        SetError(error, "Palette data size must be a multiple of 32 bytes.");
        return false;
    }

    nlohmann::json root;
    root["format"] = kPaletteFormat;
    root["num_palettes"] = paletteData.size() / 32;
    root["palettes"] = nlohmann::json::array();

    for (size_t paletteOffset = 0; paletteOffset < paletteData.size(); paletteOffset += 32) {
        nlohmann::json palette = nlohmann::json::array();
        for (size_t colorOffset = paletteOffset; colorOffset < paletteOffset + 32; colorOffset += 2) {
            const uint16_t color =
                static_cast<uint16_t>(paletteData[colorOffset] | (paletteData[colorOffset + 1] << 8));
            palette.push_back(GbaToHexColor(color));
        }
        root["palettes"].push_back(palette);
    }

    return WriteJsonFile(outputPath, root, error);
}

bool ReadPaletteJson(const std::filesystem::path& inputPath, std::vector<uint8_t>& paletteData, std::string* error) {
    nlohmann::json root;
    if (!LoadJsonFile(inputPath, root, error)) {
        return false;
    }

    if (!root.contains("palettes") || !root["palettes"].is_array()) {
        SetError(error, "Palette JSON missing palettes array: " + inputPath.string());
        return false;
    }

    paletteData.clear();
    for (const auto& palette : root["palettes"]) {
        if (!palette.is_array() || palette.size() != 16) {
            SetError(error, "Each palette must contain 16 colors: " + inputPath.string());
            return false;
        }

        for (const auto& colorValue : palette) {
            if (!colorValue.is_string()) {
                SetError(error, "Palette color must be a string: " + inputPath.string());
                return false;
            }

            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (!ParseHexColor(colorValue.get<std::string>(), r, g, b)) {
                SetError(error, "Invalid palette color in " + inputPath.string());
                return false;
            }

            const uint16_t gbaColor = RgbToGba(r, g, b);
            paletteData.push_back(static_cast<uint8_t>(gbaColor & 0xFF));
            paletteData.push_back(static_cast<uint8_t>(gbaColor >> 8));
        }
    }

    return true;
}

bool DecodeTmcText(const uint8_t* textData, size_t maxBytes, std::string& text, size_t* consumedBytes,
                   std::string* error) {
    text.clear();
    if (textData == nullptr) {
        SetError(error, "Text buffer is null.");
        return false;
    }

    const uint8_t* cursor = textData;
    const uint8_t* end = textData + maxBytes;
    while (cursor < end) {
        const uint8_t byte = *cursor;
        if (byte == 0) {
            ++cursor;
            if (consumedBytes != nullptr) {
                *consumedBytes = static_cast<size_t>(cursor - textData);
            }
            return true;
        }

        if (const char* mapped = LookupTextChar(byte); mapped != nullptr) {
            text += mapped;
            ++cursor;
            continue;
        }

        if (byte < 0x20) {
            if (!DecodeTextCommand(cursor, end, text, error)) {
                return false;
            }
            continue;
        }

        text += "{";
        text += HexByteString(byte);
        text += "}";
        ++cursor;
    }

    SetError(error, "Text is missing a terminating null byte.");
    return false;
}

bool EncodeTmcText(const std::string& text, std::vector<uint8_t>& textData, std::string* error) {
    textData.clear();

    for (size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i + 1);
            if (close == std::string::npos) {
                SetError(error, "Unclosed text command.");
                return false;
            }

            const std::string content = text.substr(i + 1, close - (i + 1));
            const std::vector<std::string> parts = SplitTextCommand(content);
            if (!EncodeNamedTextCommand(parts, textData, error) && !EncodeGenericTextCommand(parts, textData, error)) {
                return false;
            }
            i = close + 1;
            continue;
        }

        uint8_t byte = 0;
        if (!LookupTextByte(text.substr(i), byte)) {
            SetError(error,
                     "Unsupported text character near: " + text.substr(i, std::min<size_t>(text.size() - i, 16)));
            return false;
        }

        const char* mapped = LookupTextChar(byte);
        const size_t consumed = std::char_traits<char>::length(mapped);
        textData.push_back(byte);
        i += consumed;
    }

    textData.push_back(0);
    return true;
}

bool WriteEditableText(const std::filesystem::path& outputPath, const std::vector<uint8_t>& textData,
                       std::string* error) {
    nlohmann::json root;
    root["format"] = kTextFormat;
    root["text"] = FormatEditableText(textData);
    return WriteJsonFile(outputPath, root, error);
}

bool ReadEditableText(const std::filesystem::path& inputPath, std::vector<uint8_t>& textData, std::string* error) {
    nlohmann::json root;
    if (!LoadJsonFile(inputPath, root, error)) {
        return false;
    }

    if (!root.contains("text") || !root["text"].is_string()) {
        SetError(error, "Editable text JSON missing text string: " + inputPath.string());
        return false;
    }

    if (!ParseEditableTextString(root["text"].get<std::string>(), textData, error)) {
        if (error != nullptr && !error->empty()) {
            *error += " in " + inputPath.string();
        }
        return false;
    }

    return true;
}

bool WriteEditableAnimation(const std::filesystem::path& outputPath, std::span<const uint8_t> animationData,
                            std::string* error) {
    nlohmann::json root;
    root["format"] = kAnimationFormat;
    root["frames"] = nlohmann::json::array();

    size_t offset = 0;
    while (offset + 4 <= animationData.size()) {
        nlohmann::json frame;
        frame["frame_index"] = animationData[offset + 0];
        frame["duration"] = animationData[offset + 1];
        frame["sprite_settings"] = animationData[offset + 2];
        frame["frame"] = animationData[offset + 3] & 0x7F;

        offset += 4;
        if ((animationData[offset - 1] & 0x80) != 0) {
            if (offset >= animationData.size()) {
                SetError(error, "Animation loop byte missing.");
                return false;
            }
            frame["loop"] = true;
            frame["loop_back"] = animationData[offset];
            ++offset;
        }

        root["frames"].push_back(frame);
    }

    if (offset != animationData.size()) {
        SetError(error, "Animation data has trailing bytes.");
        return false;
    }

    return WriteJsonFile(outputPath, root, error);
}

bool ReadEditableAnimation(const std::filesystem::path& inputPath, std::vector<uint8_t>& animationData,
                           std::string* error) {
    nlohmann::json root;
    if (!LoadJsonFile(inputPath, root, error)) {
        return false;
    }

    if (!root.contains("frames") || !root["frames"].is_array()) {
        SetError(error, "Animation JSON missing frames array: " + inputPath.string());
        return false;
    }

    animationData.clear();
    for (const auto& frame : root["frames"]) {
        if (!frame.is_object()) {
            SetError(error, "Animation frame must be an object: " + inputPath.string());
            return false;
        }

        const uint8_t frameIndex = static_cast<uint8_t>(frame.value("frame_index", 0));
        const uint8_t duration = static_cast<uint8_t>(frame.value("duration", 0));
        const uint8_t spriteSettings = static_cast<uint8_t>(frame.value("sprite_settings", 0));
        uint8_t frameValue = static_cast<uint8_t>(frame.value("frame", 0));
        if (frame.value("loop", false)) {
            frameValue = static_cast<uint8_t>(frameValue | 0x80);
        }

        animationData.push_back(frameIndex);
        animationData.push_back(duration);
        animationData.push_back(spriteSettings);
        animationData.push_back(frameValue);

        if ((frameValue & 0x80) != 0) {
            animationData.push_back(static_cast<uint8_t>(frame.value("loop_back", 0)));
        }
    }

    return true;
}

bool WriteEditableSpriteFrames(const std::filesystem::path& outputPath, std::span<const uint8_t> frameData,
                               std::string* error) {
    if (frameData.size() % 4 != 0) {
        SetError(error, "Sprite frame data size must be a multiple of 4 bytes.");
        return false;
    }

    nlohmann::json root;
    root["format"] = kSpriteFramesFormat;
    root["frames"] = nlohmann::json::array();

    for (size_t offset = 0; offset < frameData.size(); offset += 4) {
        nlohmann::json frame;
        frame["num_tiles"] = frameData[offset + 0];
        frame["unk_1"] = frameData[offset + 1];
        frame["first_tile_index"] = static_cast<uint16_t>(frameData[offset + 2] | (frameData[offset + 3] << 8));
        root["frames"].push_back(frame);
    }

    return WriteJsonFile(outputPath, root, error);
}

bool ReadEditableSpriteFrames(const std::filesystem::path& inputPath, std::vector<uint8_t>& frameData,
                              std::string* error) {
    nlohmann::json root;
    if (!LoadJsonFile(inputPath, root, error)) {
        return false;
    }

    if (!root.contains("frames") || !root["frames"].is_array()) {
        SetError(error, "Sprite frames JSON missing frames array: " + inputPath.string());
        return false;
    }

    frameData.clear();
    for (const auto& frame : root["frames"]) {
        if (!frame.is_object()) {
            SetError(error, "Sprite frame entry must be an object: " + inputPath.string());
            return false;
        }

        const uint8_t numTiles = static_cast<uint8_t>(frame.value("num_tiles", 0));
        const uint8_t unk1 = static_cast<uint8_t>(frame.value("unk_1", 0));
        const uint16_t firstTileIndex = static_cast<uint16_t>(frame.value("first_tile_index", 0));
        frameData.push_back(numTiles);
        frameData.push_back(unk1);
        frameData.push_back(static_cast<uint8_t>(firstTileIndex & 0xFF));
        frameData.push_back(static_cast<uint8_t>(firstTileIndex >> 8));
    }

    return true;
}

bool RuntimeAssetsNeedRebuild(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                              std::string* reason) {
    if (std::filesystem::exists(outputRoot) && !std::filesystem::is_directory(outputRoot)) {
        SetError(reason, "runtime asset output path is not a directory");
        return true;
    }

    if (!std::filesystem::exists(outputRoot) || !std::filesystem::is_directory(outputRoot)) {
        SetError(reason, "runtime asset directory is missing");
        return true;
    }

    const std::filesystem::path statePath = outputRoot / kBuildStateFile;
    if (!std::filesystem::exists(statePath)) {
        SetError(reason, "runtime build state is missing");
        return true;
    }

    nlohmann::json state;
    if (!LoadJsonFile(statePath, state, nullptr)) {
        SetError(reason, "runtime build state could not be read");
        return true;
    }

    if (!state.contains("builder_version") || !state["builder_version"].is_number_unsigned()) {
        SetError(reason, "runtime build state is invalid");
        return true;
    }

    if (state["builder_version"].get<uint32_t>() != kBuildStateVersion) {
        SetError(reason, "runtime assets were built by an older pipeline version");
        return true;
    }

    /* If the runtime tree was built in pak mode but BuildRuntimeAssets
     * (which always emits loose files) is being asked to refresh it,
     * force a rebuild so the engine sees a consistent loose tree. The
     * extractor's own up-to-date check handles the inverse case. */
    if (state.contains("pack_format") && state["pack_format"].is_string()) {
        const std::string recorded = state["pack_format"].get<std::string>();
        if (recorded != "loose") {
            SetError(reason, "runtime tree is in pak mode but pipeline only writes loose files");
            return true;
        }
    }

    /* Compare the recorded source-state against the current one. If they
     * match exactly (path/size/mtime), the runtime assets are still valid and
     * we can skip the entire rebuild. This is what makes re-runs near-instant. */
    if (!std::filesystem::exists(sourceRoot) || !std::filesystem::is_directory(sourceRoot)) {
        /* No editable source tree to compare against — trust the state file. */
        return false;
    }

    nlohmann::json current;
    try {
        current = BuildSourceState(sourceRoot);
    } catch (const std::exception& e) {
        SetError(reason, std::string("failed to enumerate source assets: ") + e.what());
        return true;
    }

    if (!state.contains("files") || !current.contains("files")) {
        SetError(reason, "runtime build state lacks file manifest");
        return true;
    }

    if (state["files"] != current["files"]) {
        SetError(reason, "editable assets have changed since the last build");
        return true;
    }

    return false;
}

bool BuildRuntimeAssets(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                        std::string* error) {
    nlohmann::json sourceGfxGroups;
    nlohmann::json sourcePalettes;
    nlohmann::json sourcePaletteGroups;
    nlohmann::json sourceTexts;
    nlohmann::json sourceSpritePtrs;

    if (!LoadJsonFile(sourceRoot / "gfx_groups.json", sourceGfxGroups, error) ||
        !LoadJsonFile(sourceRoot / "palettes.json", sourcePalettes, error) ||
        !LoadJsonFile(sourceRoot / "palette_groups.json", sourcePaletteGroups, error) ||
        !LoadJsonFile(sourceRoot / "texts.json", sourceTexts, error) ||
        !LoadJsonFile(sourceRoot / "sprite_ptrs.json", sourceSpritePtrs, error)) {
        return false;
    }

    nlohmann::json runtimePalettes;
    nlohmann::json runtimePaletteGroups;
    nlohmann::json runtimeGfxGroups;
    nlohmann::json runtimeTexts;
    nlohmann::json runtimeSpritePtrs;
    std::unordered_map<std::string, std::string> paletteFileMap;

    if (!BuildRuntimePaletteFiles(sourceRoot, outputRoot, sourcePalettes, runtimePalettes, paletteFileMap, error)) {
        return false;
    }
    if (!BuildRuntimePaletteGroups(sourcePaletteGroups, paletteFileMap, runtimePaletteGroups)) {
        SetError(error, "Failed to build palette group metadata.");
        return false;
    }
    if (!BuildRuntimeGfxFiles(sourceRoot, outputRoot, sourceGfxGroups, runtimeGfxGroups, error)) {
        return false;
    }
    if (!BuildRuntimeTexts(sourceRoot, outputRoot, sourceTexts, runtimeTexts, error)) {
        return false;
    }
    if (!BuildRuntimeSpritePtrs(sourceRoot, outputRoot, sourceSpritePtrs, runtimeSpritePtrs, error)) {
        return false;
    }
    if (!CopyRuntimePassthroughAssets(sourceRoot, outputRoot, error)) {
        return false;
    }

    if (!WriteJsonFile(outputRoot / "palettes.json", runtimePalettes, error) ||
        !WriteJsonFile(outputRoot / "palette_groups.json", runtimePaletteGroups, error) ||
        !WriteJsonFile(outputRoot / "gfx_groups.json", runtimeGfxGroups, error) ||
        !WriteJsonFile(outputRoot / "texts.json", runtimeTexts, error) ||
        !WriteJsonFile(outputRoot / "sprite_ptrs.json", runtimeSpritePtrs, error)) {
        return false;
    }

    const nlohmann::json sourceState = BuildSourceState(sourceRoot);
    if (!WriteJsonFile(outputRoot / kBuildStateFile, sourceState, error)) {
        return false;
    }

    return true;
}

bool WriteBuildStateFile(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                         std::string* error) {
    if (!std::filesystem::exists(sourceRoot) || !std::filesystem::is_directory(sourceRoot)) {
        SetError(error, "source asset root does not exist");
        return false;
    }
    nlohmann::json state;
    try {
        state = BuildSourceState(sourceRoot);
    } catch (const std::exception& e) {
        SetError(error, std::string("failed to enumerate source assets: ") + e.what());
        return false;
    }
    return WriteJsonFile(outputRoot / kBuildStateFile, state, error);
}

bool EnsureRuntimeAssetsBuilt(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                              std::string* reasonOrError) {
    std::string reason;
    if (!RuntimeAssetsNeedRebuild(sourceRoot, outputRoot, &reason)) {
        return true;
    }

    if (!BuildRuntimeAssets(sourceRoot, outputRoot, reasonOrError)) {
        if (reasonOrError != nullptr && reasonOrError->empty()) {
            *reasonOrError = reason;
        }
        return false;
    }

    if (reasonOrError != nullptr) {
        *reasonOrError = reason;
    }
    return true;
}

} // namespace PortAssetPipeline
