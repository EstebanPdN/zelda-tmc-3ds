#ifndef PORT_EXE_PATH_HPP
#define PORT_EXE_PATH_HPP

/* Single source of truth for "directory containing the running executable".
 *
 * Implemented once in port_exe_path.cpp so the platform query — and its
 * <windows.h> include — lives in exactly one place instead of the copies
 * that had drifted into three subtly different variants across the C++ port
 * TUs (port_asset_loader.cpp, port_asset_bootstrap.cpp, port_m4a_backend.cpp,
 * port_mods.cpp). The C callers (port_rom.c, port_rom_picker.c) keep their own
 * char-buffer versions: a different ABI not worth bridging across the C/C++
 * boundary.
 *
 * Returns std::nullopt on platform-query failure; callers layer their own cwd
 * fallback on top where they want one. */

#include <filesystem>
#include <optional>

namespace port {
std::optional<std::filesystem::path> ExecutableDir();
}  // namespace port

#endif  // PORT_EXE_PATH_HPP
