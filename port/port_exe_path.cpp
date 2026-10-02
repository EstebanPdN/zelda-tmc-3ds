#include "port_exe_path.hpp"

#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace port {

std::optional<std::filesystem::path> ExecutableDir() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len == 0) {
        return std::nullopt;
    }
    while (len >= buffer.size() - 1) {
        buffer.resize(buffer.size() * 2);
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return std::nullopt;
        }
    }
    buffer.resize(len);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    /* macOS has no /proc filesystem; readlink("/proc/self/exe") fails.
     * _NSGetExecutablePath returns the launch path which may include
     * symlinks/relative segments, so weakly_canonical() it for a canonical form. */
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(buffer.c_str(), ec);
        if (!ec) {
            return canonical.parent_path();
        }
    }
    return std::nullopt;
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (len > 0 && static_cast<size_t>(len) < sizeof(buffer)) {
        return std::filesystem::path(std::string(buffer, static_cast<size_t>(len))).parent_path();
    }
    return std::nullopt;
#endif
}

}  // namespace port
