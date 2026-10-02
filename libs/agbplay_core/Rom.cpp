#include "Rom.hpp"

#include <algorithm>

std::unique_ptr<Rom> Rom::globalInstance;

Rom Rom::LoadFromBufferCopy(std::span<uint8_t> buffer) {
    Rom rom;

    rom.romContainer.assign(buffer.begin(), buffer.end());
    rom.romData = std::span<uint8_t>(rom.romContainer.data(), rom.romContainer.size());
    return rom;
}

Rom Rom::LoadFromBufferRef(std::span<uint8_t> buffer) {
    Rom rom;

    rom.romData = buffer;
    return rom;
}

void Rom::CreateInstance(const std::filesystem::path& filePath) {
    (void)filePath;
}

std::string Rom::ReadString(size_t pos, size_t limit) const {
    std::string value;

    for (size_t i = 0; i < limit && pos + i < romData.size(); i++) {
        const uint8_t ch = romData[pos + i];
        if (ch == 0) {
            break;
        }
        value.push_back(static_cast<char>(ch));
    }

    return value;
}

std::string Rom::GetROMCode() const {
    if (romData.size() < 0xB0) {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(&romData[0xAC]), 4);
}

bool Rom::IsGsf() const {
    return false;
}

void Rom::Verify() {
}

void Rom::LoadFile(const std::filesystem::path& filePath) {
    (void)filePath;
}

bool Rom::LoadZip(const std::filesystem::path& filePath) {
    (void)filePath;
    return false;
}

bool Rom::LoadGsflib(const std::filesystem::path& filePath) {
    (void)filePath;
    return false;
}

bool Rom::LoadRaw(const std::filesystem::path& filePath) {
    (void)filePath;
    return false;
}
