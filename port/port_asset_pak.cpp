#include "port_asset_pak.hpp"

#include "port_asset_log.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>

namespace PortAssetPak {

namespace {

inline void WriteU32LE(uint8_t* dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

inline void WriteU64LE(uint8_t* dst, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        dst[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

inline uint64_t AlignUp(uint64_t v, uint64_t alignment) {
    return (v + alignment - 1) & ~(alignment - 1);
}

inline void SetError(std::string* error, std::string msg) {
    if (error != nullptr) {
        *error = std::move(msg);
    }
}

} // namespace

void PakBuilder::Add(std::string relativePath, const uint8_t* data, std::size_t size) {
    StagedEntry entry;
    entry.path = std::move(relativePath);
    entry.data.assign(data, data + size);
    std::lock_guard<std::mutex> lk(mu_);
    entries_.push_back(std::move(entry));
}

std::size_t PakBuilder::Size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return entries_.size();
}

bool PakBuilder::Write(const std::filesystem::path& outputPath, std::string* error) {
    std::vector<StagedEntry> entries;
    {
        std::lock_guard<std::mutex> lk(mu_);
        entries = std::move(entries_);
        entries_.clear();
    }

    /* Sort by (path, descending size) so that std::unique below
     * collapses duplicate path entries while keeping the LARGEST
     * payload. The asset-index occasionally lists the same runtime
     * path under two different sizes (e.g. assets/gGfx_9_0_JP.bin is
     * indexed as both 0x500 and 0x1900 bytes — a slice and the full
     * blob). In loose mode the larger one tends to win because it's
     * written second by the sweep; we mirror that here so pak and
     * loose modes agree. Stable sort makes ties (same path AND same
     * size) deterministic regardless of thread scheduling. */
    std::stable_sort(entries.begin(), entries.end(),
                     [](const StagedEntry& a, const StagedEntry& b) {
                         if (a.path != b.path) {
                             return a.path < b.path;
                         }
                         return a.data.size() > b.data.size();
                     });

    auto last = std::unique(entries.begin(), entries.end(),
                            [](const StagedEntry& a, const StagedEntry& b) { return a.path == b.path; });
    entries.erase(last, entries.end());

    const uint32_t entry_count = static_cast<uint32_t>(entries.size());
    const uint32_t name_table_offset = kHeaderSize + entry_count * kEntrySize;

    /* Build the name table by concatenating all paths back-to-back. */
    uint32_t name_table_size = 0;
    for (const auto& e : entries) {
        name_table_size += static_cast<uint32_t>(e.path.size());
    }

    /* Page-align the data blob so the loader's mmap of the data
     * portion lands on a fresh page (helps debuggers + makes it
     * possible to mprotect the blob read-only without affecting
     * the metadata pages). */
    const uint64_t data_offset =
        AlignUp(static_cast<uint64_t>(name_table_offset) + name_table_size, kDataAlignment);

    /* Compute per-entry data offsets (4-byte aligned within the blob). */
    std::vector<uint64_t> entry_offsets(entries.size());
    uint64_t cursor = data_offset;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        cursor = AlignUp(cursor, 4);
        entry_offsets[i] = cursor;
        cursor += entries[i].data.size();
    }
    const uint64_t data_size = cursor - data_offset;

    PortAssetLog::EnsureDir(outputPath.parent_path());

    /* Pre-allocate a single output buffer and write it in one shot.
     * Even the largest pak (gfx) is well under 100 MB so this fits
     * comfortably in RAM, and a single ofstream::write call is a lot
     * cheaper than streaming many small writes. */
    std::vector<uint8_t> out(static_cast<std::size_t>(cursor), 0);

    /* Serialise the 40-byte header field-by-field; offsets match the
     * layout doc in port_asset_pak.hpp. Offsets 32-39 stay zero (from
     * the zero-filled buffer) — the loader never reads them. */
    WriteU32LE(out.data() + 0, kMagic);
    WriteU32LE(out.data() + 4, kVersion);
    WriteU32LE(out.data() + 8, entry_count);
    WriteU32LE(out.data() + 12, name_table_offset);
    WriteU32LE(out.data() + 16, name_table_size);
    WriteU32LE(out.data() + 20, static_cast<uint32_t>(data_offset));
    WriteU64LE(out.data() + 24, data_size);

    uint32_t name_cursor = 0;
    for (uint32_t i = 0; i < entry_count; ++i) {
        const auto& e = entries[i];
        const uint32_t entry_pos = kHeaderSize + i * kEntrySize;
        WriteU32LE(out.data() + entry_pos + 0, name_cursor);
        WriteU32LE(out.data() + entry_pos + 4, static_cast<uint32_t>(e.path.size()));
        WriteU64LE(out.data() + entry_pos + 8, entry_offsets[i]);
        WriteU32LE(out.data() + entry_pos + 16, static_cast<uint32_t>(e.data.size()));
        WriteU32LE(out.data() + entry_pos + 20, 0); // reserved
        std::memcpy(out.data() + name_table_offset + name_cursor, e.path.data(), e.path.size());
        name_cursor += static_cast<uint32_t>(e.path.size());
    }

    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].data.empty()) {
            std::memcpy(out.data() + entry_offsets[i], entries[i].data.data(), entries[i].data.size());
        }
    }

    /* Buffered fwrite-style output; one syscall flush at the end. */
    static constexpr std::streamsize kBuf = 1024 * 1024;
    std::vector<char> file_buf(static_cast<std::size_t>(kBuf));
    std::ofstream f;
    f.rdbuf()->pubsetbuf(file_buf.data(), kBuf);
    f.open(outputPath, std::ios::binary | std::ios::trunc);
    if (!f) {
        SetError(error, "failed to open " + outputPath.string() + " for writing");
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    f.flush();
    if (!f) {
        SetError(error, "failed to write " + outputPath.string());
        return false;
    }
    return true;
}

} // namespace PortAssetPak
