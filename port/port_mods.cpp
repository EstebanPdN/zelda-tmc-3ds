#include "port_mods.h"
#include "port_asset_loader.h"
#include "port_exe_path.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif


namespace {

std::filesystem::path ExecutableDirectory() {
    if (auto dir = port::ExecutableDir()) {
        return *dir;
    }
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}

std::vector<std::string> SplitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (c != ' ' && c != '\t') {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}  // namespace

extern "C" void Port_Mods_Init(void) {
    const std::filesystem::path exeDir = ExecutableDirectory();
    std::vector<std::filesystem::path> modsRoots;

    auto add_mods_root = [&](const std::filesystem::path& root) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            return;
        }
        std::filesystem::path canonical = std::filesystem::weakly_canonical(root, ec);
        if (ec) canonical = root;
        if (std::find(modsRoots.begin(), modsRoots.end(), canonical) == modsRoots.end()) {
            modsRoots.push_back(std::move(canonical));
        }
    };

    add_mods_root(exeDir / "mods");
    {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) {
            add_mods_root(cwd / "mods");
        }
    }

    const char* env = SDL_getenv("TMC_MODS");
    Port_SetModsExplicitSelection(env != nullptr);
    if (env == nullptr) {
        /* No explicit active set: asset_loader's ScanMods() performs
         * deterministic alphabetical discovery under each asset search
         * root. Keep that logic single-sourced there. */
        return;
    }

    /* TMC_MODS=foo,bar — explicit ordered list, leftmost wins. */
    const std::vector<std::string> chosen = SplitCsv(env);
    if (chosen.empty()) {
        std::fprintf(stderr, "[MOD] TMC_MODS is set but empty; no mods active\n");
        return;
    }

    int registered = 0;
    for (const auto& name : chosen) {
        std::filesystem::path modDir;
        for (const auto& modsRoot : modsRoots) {
            std::error_code ec;
            const std::filesystem::path candidate = modsRoot / name;
            if (std::filesystem::is_directory(candidate, ec)) {
                modDir = candidate;
                break;
            }
        }
        if (modDir.empty()) {
            std::fprintf(stderr, "[MOD] skipping '%s' — not found in active mods roots\n", name.c_str());
            continue;
        }
        Port_AddModRoot(modDir);
        std::fprintf(stderr, "[MOD] active: %s -> %s\n", name.c_str(), modDir.string().c_str());
        registered++;
    }
    std::fprintf(stderr, "[MOD] %d mod%s registered\n", registered, registered == 1 ? "" : "s");
}
