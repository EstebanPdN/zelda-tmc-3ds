#pragma once

/* TMC Pak v1 — read side.
 *
 * mmap-backed reader that exposes asset bytes as zero-copy spans into
 * the OS page cache. Callers take a `std::span<const uint8_t>` and
 * must not retain it past PakArchive destruction (which unmaps the
 * file).
 *
 * Multi-pak case: the engine constructs one PakSet that owns N
 * archives (one per category) and routes Lookup() to the first
 * archive whose name table contains the requested key. */

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PortAssetPak {

class PakArchive {
  public:
    PakArchive();
    ~PakArchive();
    PakArchive(const PakArchive&) = delete;
    PakArchive& operator=(const PakArchive&) = delete;
    PakArchive(PakArchive&&) noexcept;
    PakArchive& operator=(PakArchive&&) noexcept;

    /* Open a pak file. Returns false if the file is missing,
     * unreadable, or the magic/version doesn't match. error_out (if
     * non-null) is set to a human-readable description on failure. */
    bool Open(const std::filesystem::path& path, std::string* error_out);

    /* Return the bytes for `relativePath` if present in this archive,
     * or std::nullopt if not. The returned span points into the mmap
     * and lives as long as this archive. */
    std::optional<std::span<const uint8_t>> Lookup(std::string_view relativePath) const;

    bool IsOpen() const noexcept { return base_ != nullptr; }
    std::size_t EntryCount() const noexcept { return entry_count_; }
    const std::filesystem::path& Path() const noexcept { return path_; }

  private:
    void Close();
    static int CompareEntry(const uint8_t* base, uint32_t name_table_offset, uint32_t entry_index,
                            std::string_view key);

    std::filesystem::path path_;
    /* Platform-specific mapping handles. */
#ifdef _WIN32
    void* file_handle_{nullptr};   // HANDLE
    void* mapping_handle_{nullptr}; // HANDLE
#else
    int fd_{-1};
#endif
    const uint8_t* base_{nullptr};
    std::size_t size_{0};

    /* Cached header values for hot lookup. */
    uint32_t entry_count_{0};
    uint32_t name_table_offset_{0};
    uint32_t entry_offset_{0};
};

/* Convenience holder: open a directory worth of pak files (one per
 * category) and route Lookup to the first archive that has the path. */
class PakSet {
  public:
    /* Try to open every "*.pak" in `assetsRoot`. Returns the number
     * of successfully opened archives. Failures are logged to the
     * extractor reporter (warning) but do not abort the load. */
    std::size_t Mount(const std::filesystem::path& assetsRoot);

    /* Returns true if at least one archive has been mounted. */
    bool Empty() const noexcept { return archives_.empty(); }

    /* Sum of EntryCount across all archives. */
    std::size_t TotalEntries() const noexcept;

    std::optional<std::span<const uint8_t>> Lookup(std::string_view relativePath) const;

    void Clear();

    const std::vector<std::unique_ptr<PakArchive>>& Archives() const noexcept { return archives_; }

  private:
    std::vector<std::unique_ptr<PakArchive>> archives_;
};

} // namespace PortAssetPak
