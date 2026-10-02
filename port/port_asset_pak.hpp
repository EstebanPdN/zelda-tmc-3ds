#pragma once

/* TMC Pak v1 — write side.
 *
 * One pak archive per asset category (gfx, palettes, animations, …)
 * concatenates many small runtime files into a single mmap-friendly
 * blob. The format is little-endian only and uses no compression — the
 * source data is already optimised for the GBA hardware and we want
 * zero-copy access from the engine.
 *
 * On-disk layout (little-endian; 40-byte header):
 *
 *   offset  size  field
 *   ------  ----  -------------------------------
 *      0     4    magic "TMCP"
 *      4     4    version (u32, currently 1)
 *      8     4    entry_count (u32)
 *     12     4    name_table_offset (u32)
 *     16     4    name_table_size (u32)
 *     20     4    data_offset (u32, page-aligned to 4 KiB)
 *     24     8    data_size (u64)
 *     32     8    reserved (zero padding up to kHeaderSize = 40)
 *     40    24*N  entries[entry_count] (24 bytes each, sorted by name)
 *
 * Entry layout (24 bytes, little-endian):
 *
 *      0     4    name_offset (u32, relative to name_table_offset)
 *      4     4    name_length (u32, bytes, no null terminator)
 *      8     8    data_offset (u64, absolute file offset)
 *     16     4    data_size (u32, single 32-bit size; no asset > 4 GiB)
 *     20     4    reserved (u32, must be 0)
 *
 * Names are stored as concatenated, length-prefixed-by-entry strings
 * with no null terminators.
 *
 * Entries are sorted lexicographically by name so the loader can do a
 * std::lower_bound binary search without an auxiliary index. */

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace PortAssetPak {

constexpr uint32_t kMagic = 0x50434D54; // "TMCP" little-endian
constexpr uint32_t kVersion = 1;
constexpr uint32_t kHeaderSize = 40;
constexpr uint32_t kEntrySize = 24;
constexpr uint32_t kDataAlignment = 4096;

#pragma pack(push, 1)
struct Entry {
    uint32_t name_offset;
    uint32_t name_length;
    uint64_t data_offset;
    uint32_t data_size;
    uint32_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(Entry) == kEntrySize, "TMC Pak entry layout mismatch");

/* Thread-safe builder. Add() may be called from any thread; Write()
 * sorts the staging entries by name and serialises them to disk. */
class PakBuilder {
  public:
    PakBuilder() = default;
    PakBuilder(const PakBuilder&) = delete;
    PakBuilder& operator=(const PakBuilder&) = delete;
    PakBuilder(PakBuilder&&) = delete;
    PakBuilder& operator=(PakBuilder&&) = delete;

    /* Stage an entry. relativePath is the asset's path relative to
     * assets/ (e.g. "gfx/gfx_12345_64x64_4bpp_uncompressed.bin"). data
     * is copied into an owning staging buffer so the caller may release
     * its memory immediately. */
    void Add(std::string relativePath, const uint8_t* data, std::size_t size);

    /* Number of entries currently staged. */
    std::size_t Size() const;

    /* Serialise to outputPath. Sorts entries by name in-place, then
     * writes header + entries + name table + data blob. Returns false
     * on I/O failure with a description in *error (if non-null). */
    bool Write(const std::filesystem::path& outputPath, std::string* error);

  private:
    struct StagedEntry {
        std::string path;
        std::vector<uint8_t> data;
    };

    mutable std::mutex mu_;
    std::vector<StagedEntry> entries_;
};

} // namespace PortAssetPak
