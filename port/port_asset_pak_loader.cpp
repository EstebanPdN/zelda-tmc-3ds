#include "port_asset_pak_loader.hpp"

#include "port_asset_log.hpp"
#include "port_asset_pak.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace PortAssetPak {

namespace {

inline uint32_t ReadU32LE(const uint8_t* src) {
    return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

inline uint64_t ReadU64LE(const uint8_t* src) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(src[i]) << (i * 8);
    }
    return v;
}

inline void SetError(std::string* out, std::string msg) {
    if (out != nullptr) {
        *out = std::move(msg);
    }
}

} // namespace

PakArchive::PakArchive() = default;

PakArchive::~PakArchive() {
    Close();
}

PakArchive::PakArchive(PakArchive&& other) noexcept {
    *this = std::move(other);
}

PakArchive& PakArchive::operator=(PakArchive&& other) noexcept {
    if (this != &other) {
        Close();
        path_ = std::move(other.path_);
#ifdef _WIN32
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        other.file_handle_ = nullptr;
        other.mapping_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        base_ = other.base_;
        size_ = other.size_;
        entry_count_ = other.entry_count_;
        name_table_offset_ = other.name_table_offset_;
        entry_offset_ = other.entry_offset_;
        other.base_ = nullptr;
        other.size_ = 0;
        other.entry_count_ = 0;
    }
    return *this;
}

void PakArchive::Close() {
#ifdef _WIN32
    if (base_ != nullptr) {
        UnmapViewOfFile(base_);
        base_ = nullptr;
    }
    if (mapping_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mapping_handle_));
        mapping_handle_ = nullptr;
    }
    if (file_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(file_handle_));
        file_handle_ = nullptr;
    }
#else
    if (base_ != nullptr) {
        munmap(const_cast<uint8_t*>(base_), size_);
        base_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    size_ = 0;
    entry_count_ = 0;
    name_table_offset_ = 0;
    entry_offset_ = 0;
    path_.clear();
}

bool PakArchive::Open(const std::filesystem::path& path, std::string* error_out) {
    Close();
    path_ = path;

#ifdef _WIN32
    HANDLE fh = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        SetError(error_out, "CreateFileW failed for " + path.string());
        return false;
    }
    LARGE_INTEGER size_li{};
    if (!GetFileSizeEx(fh, &size_li) || size_li.QuadPart < kHeaderSize) {
        SetError(error_out, "pak file too small or stat failed: " + path.string());
        CloseHandle(fh);
        return false;
    }
    HANDLE mh = CreateFileMappingW(fh, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mh == nullptr) {
        SetError(error_out, "CreateFileMappingW failed for " + path.string());
        CloseHandle(fh);
        return false;
    }
    void* view = MapViewOfFile(mh, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        SetError(error_out, "MapViewOfFile failed for " + path.string());
        CloseHandle(mh);
        CloseHandle(fh);
        return false;
    }
    file_handle_ = fh;
    mapping_handle_ = mh;
    base_ = static_cast<const uint8_t*>(view);
    size_ = static_cast<std::size_t>(size_li.QuadPart);
#else
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        SetError(error_out, std::string("open failed for ") + path.string() + ": " + std::strerror(errno));
        return false;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(kHeaderSize)) {
        SetError(error_out, "pak file too small or stat failed: " + path.string());
        ::close(fd);
        return false;
    }
    void* mapping =
        ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        SetError(error_out, std::string("mmap failed for ") + path.string() + ": " + std::strerror(errno));
        ::close(fd);
        return false;
    }
    fd_ = fd;
    base_ = static_cast<const uint8_t*>(mapping);
    size_ = static_cast<std::size_t>(st.st_size);
#endif

    /* Validate header. */
    const uint32_t magic = ReadU32LE(base_ + 0);
    const uint32_t version = ReadU32LE(base_ + 4);
    if (magic != kMagic || version != kVersion) {
        SetError(error_out,
                 "pak file magic/version mismatch: " + path.string());
        Close();
        return false;
    }
    entry_count_ = ReadU32LE(base_ + 8);
    name_table_offset_ = ReadU32LE(base_ + 12);
    const uint32_t name_table_size = ReadU32LE(base_ + 16);
    const uint32_t data_offset = ReadU32LE(base_ + 20);
    const uint64_t data_size = ReadU64LE(base_ + 24);
    entry_offset_ = kHeaderSize;

    /* Bounds-check header. */
    const uint64_t end_of_entries = static_cast<uint64_t>(entry_offset_) + static_cast<uint64_t>(entry_count_) * kEntrySize;
    if (end_of_entries > size_ || name_table_offset_ < end_of_entries ||
        static_cast<uint64_t>(name_table_offset_) + name_table_size > size_ ||
        static_cast<uint64_t>(data_offset) + data_size > size_) {
        SetError(error_out, "pak file header out of range: " + path.string());
        Close();
        return false;
    }

    /* Per-entry name-span bounds check. The header check above validates the
       name-table extent as a whole, but not each entry's (name_offset,
       name_length). A corrupt or truncated *.pak with an out-of-range name span
       would make the (static) CompareEntry read past the mmap region on every
       binary-search probe -> OOB read / mis-routed lookup. Validate once here at
       mount so a damaged archive fails to mount with a clear error rather than
       silently faulting at first use. */
    for (uint32_t i = 0; i < entry_count_; ++i) {
        const uint8_t* entry = base_ + kHeaderSize + static_cast<uint64_t>(i) * kEntrySize;
        const uint32_t name_offset = ReadU32LE(entry + 0);
        const uint32_t name_length = ReadU32LE(entry + 4);
        if (static_cast<uint64_t>(name_offset) + name_length > name_table_size) {
            SetError(error_out, "pak file entry name out of range: " + path.string());
            Close();
            return false;
        }
    }
    return true;
}

int PakArchive::CompareEntry(const uint8_t* base, uint32_t name_table_offset, uint32_t entry_index,
                             std::string_view key) {
    const uint8_t* entry = base + kHeaderSize + entry_index * kEntrySize;
    const uint32_t name_offset = ReadU32LE(entry + 0);
    const uint32_t name_length = ReadU32LE(entry + 4);
    const char* name_ptr = reinterpret_cast<const char*>(base + name_table_offset + name_offset);
    return std::string_view(name_ptr, name_length).compare(key);
}

std::optional<std::span<const uint8_t>> PakArchive::Lookup(std::string_view relativePath) const {
    if (base_ == nullptr || entry_count_ == 0) {
        return std::nullopt;
    }
    /* Binary search across the sorted entry table. */
    uint32_t lo = 0;
    uint32_t hi = entry_count_;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const int cmp = CompareEntry(base_, name_table_offset_, mid, relativePath);
        if (cmp < 0) {
            lo = mid + 1;
        } else if (cmp > 0) {
            hi = mid;
        } else {
            const uint8_t* entry = base_ + kHeaderSize + mid * kEntrySize;
            const uint64_t data_offset = ReadU64LE(entry + 8);
            const uint32_t data_size = ReadU32LE(entry + 16);
            if (data_offset + data_size > size_) {
                return std::nullopt;
            }
            return std::span<const uint8_t>(base_ + data_offset, data_size);
        }
    }
    return std::nullopt;
}

std::size_t PakSet::Mount(const std::filesystem::path& assetsRoot) {
    Clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(assetsRoot, ec)) {
        return 0;
    }
    /* Sort files by name so the mount order is deterministic. With
     * unique-per-archive paths this only affects diagnostic logging,
     * but determinism makes troubleshooting easier. */
    std::vector<std::filesystem::path> pak_paths;
    for (const auto& entry : std::filesystem::directory_iterator(assetsRoot, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (entry.path().extension() == ".pak") {
            pak_paths.push_back(entry.path());
        }
    }
    std::sort(pak_paths.begin(), pak_paths.end());

    auto& reporter = PortAssetLog::Reporter::Instance();
    for (const auto& p : pak_paths) {
        auto archive = std::make_unique<PakArchive>();
        std::string err;
        if (!archive->Open(p, &err)) {
            reporter.Warn("failed to mount pak " + p.string() + ": " + err);
            continue;
        }
        archives_.push_back(std::move(archive));
    }
    return archives_.size();
}

std::size_t PakSet::TotalEntries() const noexcept {
    std::size_t total = 0;
    for (const auto& a : archives_) {
        total += a->EntryCount();
    }
    return total;
}

std::optional<std::span<const uint8_t>> PakSet::Lookup(std::string_view relativePath) const {
    for (const auto& a : archives_) {
        if (auto found = a->Lookup(relativePath); found.has_value()) {
            return found;
        }
    }
    return std::nullopt;
}

void PakSet::Clear() {
    archives_.clear();
}

} // namespace PortAssetPak
