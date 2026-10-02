/* Mod-manifest parsing and runtime mod-root scan, extracted from
 * port_asset_loader.cpp so the loader's stages stay grouped by concern.
 * This file is included into the same translation unit and intentionally
 * relies on the surrounding helpers / cache state already visible there. */
void LoadModManifest(const std::filesystem::path& modsRoot, const std::filesystem::path& modDir,
                     const std::filesystem::path& manifestPath) {
    nlohmann::json manifest;
    if (!LoadJsonFile(manifestPath, manifest) || !manifest.is_object()) {
        std::fprintf(stderr, "[MOD] %s: manifest is not valid JSON (expected a top-level object)\n",
                     PathForLog(manifestPath).c_str());
        return;
    }

    const std::string modName =
        (manifest.contains("name") && manifest["name"].is_string())
            ? manifest["name"].get<std::string>()
            : modDir.filename().generic_string();

    /* Warn on unknown top-level keys — catches typos like "replacments"
     * or "files" that would otherwise silently apply nothing. */
    static const char* const kKnownKeys[] = {"name", "replace", "replacements", "description"};
    for (auto it = manifest.begin(); it != manifest.end(); ++it) {
        bool known = false;
        for (const char* k : kKnownKeys) {
            if (it.key() == k) { known = true; break; }
        }
        if (!known) {
            std::fprintf(stderr, "[MOD] %s: unknown manifest key \"%s\" (ignored)\n",
                         modName.c_str(), it.key().c_str());
        }
    }

    const nlohmann::json* replacements = nullptr;
    for (const char* key : {"replace", "replacements"}) {
        if (!manifest.contains(key)) continue;
        if (manifest[key].is_object()) {
            replacements = &manifest[key];
            break;
        }
        std::fprintf(stderr, "[MOD] %s: \"%s\" must be a JSON object of "
                     "\"asset/path\": \"replacement/file\" pairs\n", modName.c_str(), key);
    }
    if (replacements == nullptr) {
        std::fprintf(stderr, "[MOD] %s: no usable \"replace\" map; nothing to apply\n",
                     modName.c_str());
        return;
    }

    size_t loaded = 0, skipped = 0;
    for (auto it = replacements->begin(); it != replacements->end(); ++it) {
        if (!it.value().is_string()) {
            std::fprintf(stderr, "[MOD] %s: ignoring \"%s\" — value must be a string path\n",
                         modName.c_str(), it.key().c_str());
            ++skipped;
            continue;
        }

        const std::string assetPath = NormalizeAssetPath(it.key());
        const std::string replacement = NormalizeAssetPath(it.value().get<std::string>());
        if (assetPath.empty() || replacement.empty()) {
            std::fprintf(stderr, "[MOD] %s: ignoring entry with empty asset path or replacement\n",
                         modName.c_str());
            ++skipped;
            continue;
        }

        std::filesystem::path replacementPath = modsRoot / std::filesystem::path(replacement);
        if (!std::filesystem::exists(replacementPath)) {
            replacementPath = modDir / std::filesystem::path(replacement);
        }
        if (!std::filesystem::exists(replacementPath) || !std::filesystem::is_regular_file(replacementPath)) {
            std::fprintf(stderr, "[MOD] %s: missing replacement file for \"%s\" -> \"%s\"\n",
                         modName.c_str(), assetPath.c_str(), replacement.c_str());
            ++skipped;
            continue;
        }
        auto [_, inserted] = gAssetGroupCache.modReplacements.emplace(assetPath, replacementPath);
        if (inserted) {
            ++loaded;
        } else {
            std::fprintf(stderr, "[MOD] %s: \"%s\" already provided by a higher-priority mod (skipped)\n",
                         modName.c_str(), assetPath.c_str());
            ++skipped;
        }
    }

    std::string summary = modName + ": " + std::to_string(loaded) +
                          (loaded == 1 ? " replacement loaded" : " replacements loaded");
    if (skipped != 0) summary += ", " + std::to_string(skipped) + " skipped";
    std::fprintf(stderr, "[MOD] %s\n", summary.c_str());
}

/* Walk a single mod's directory and register every regular file as a
 * path-mirror replacement. The asset path is the file's path relative
 * to the mod dir, normalized (forward slashes, lowercase per
 * NormalizeAssetPath). Used as the implicit no-manifest convention:
 * drop `gfx/foo.bin` into `mods/mymod/gfx/foo.bin` and it overrides
 * the corresponding asset, no JSON required.
 *
 * Skips `mod_manifest.json`, README*, .git*, and any file under
 * `assets-src/` (raw editing source, not runtime input).
 */
void AutoDiscoverModReplacements(const std::filesystem::path& modDir, const std::string& modName) {
    auto should_skip = [](const std::filesystem::path& p) {
        const std::string name = p.filename().generic_string();
        if (name == "mod_manifest.json") return true;
        if (name.rfind("README", 0) == 0) return true;
        if (!name.empty() && name[0] == '.') return true;
        return false;
    };
    std::error_code ec;
    size_t loaded = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(
             modDir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;
        if (entry.is_directory(ec)) {
            const std::string dname = entry.path().filename().generic_string();
            if (dname == "assets-src" || dname == ".git") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file(ec)) continue;
        if (should_skip(entry.path())) continue;

        const std::filesystem::path rel = std::filesystem::relative(entry.path(), modDir, ec);
        if (ec || rel.empty()) continue;
        const std::string assetPath = NormalizeAssetPath(rel.generic_string());
        if (assetPath.empty()) continue;

        /* First registration wins — earlier-scanned mods take priority
         * over later ones, matching the directory-iterator order
         * (typically alphabetical). Manifest entries inserted before
         * this scan also stay intact. */
        auto [_, inserted] = gAssetGroupCache.modReplacements.emplace(assetPath, entry.path());
        if (inserted) ++loaded;
    }
    if (loaded != 0) {
        std::fprintf(stderr, "[MOD] Auto-discovered %s (%zu file%s)\n",
                     modName.c_str(), loaded, loaded == 1 ? "" : "s");
    }
}

/* Treat `modDir` as one mod's root: if a manifest exists, honour it;
 * otherwise auto-discover by walking the tree. */
void LoadModFromDirectory(const std::filesystem::path& parentForRelativeReplacements,
                          const std::filesystem::path& modDir) {
    const std::filesystem::path manifestPath = modDir / "mod_manifest.json";
    const std::string modName = modDir.filename().generic_string();
    if (std::filesystem::exists(manifestPath)) {
        LoadModManifest(parentForRelativeReplacements, modDir, manifestPath);
    } else {
        AutoDiscoverModReplacements(modDir, modName);
    }
}

void ScanMods() {
    if (gAssetGroupCache.modsScanned) {
        return;
    }
    gAssetGroupCache.modsScanned = true;
    gAssetGroupCache.modReplacements.clear();

    /* Dedupe by canonicalised path so a mod reachable via both
     * <exe-dir>/mods (auto-scan) and Port_AddModRoot (explicit) loads
     * once. We canonicalise once into `seen`; later registrations of
     * the same physical directory short-circuit. */
    std::unordered_set<std::string> seen;
    auto canonical_key = [](const std::filesystem::path& p) -> std::string {
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(p, ec);
        if (ec) canon = p;
        return canon.generic_string();
    };

    auto load_one = [&](const std::filesystem::path& parent,
                        const std::filesystem::path& modDir) {
        if (!std::filesystem::is_directory(modDir)) return;
        const std::string key = canonical_key(modDir);
        if (!seen.insert(key).second) return; /* already loaded */
        LoadModFromDirectory(parent, modDir);
    };

    /* Sorted directory iteration so mod precedence is alphabetical
     * (first-wins). Without this, the filesystem returns inode order
     * and the same checkout can flip precedence between machines. */
    auto load_children_sorted = [&](const std::filesystem::path& modsRoot) {
        if (!std::filesystem::exists(modsRoot) || !std::filesystem::is_directory(modsRoot)) {
            return;
        }
        std::error_code ec;
        std::vector<std::filesystem::path> entries;
        for (const auto& entry : std::filesystem::directory_iterator(modsRoot, ec)) {
            if (ec) break;
            if (entry.is_directory()) entries.push_back(entry.path());
        }
        std::sort(entries.begin(), entries.end());
        for (const auto& modDir : entries) load_one(modsRoot, modDir);
    };

    /* 1. Explicit mod paths registered via Port_AddModRoot from
     * port_mods.cpp. Preserve registration order: TMC_MODS=foo,bar
     * means foo has priority over bar. */
    for (const auto& explicitMod : gAssetGroupCache.modRoots) {
        load_one(explicitMod.parent_path(), explicitMod);
    }

    /* 2. Asset-root-relative mods: <assets>/mods/<name>/ — matheo's
     * default convention. AssetSearchRoots already yields exe-dir +
     * cwd, so dropping mods next to either the binary or the working
     * directory works. Skipped when TMC_MODS supplied an explicit
     * active set. */
    if (!gAssetGroupCache.modExplicitSelection) {
        for (const auto& root : AssetSearchRoots()) {
            load_children_sorted(root / "mods");
        }
    }

    if (!gAssetGroupCache.modReplacements.empty()) {
        std::fprintf(stderr, "[MOD] %zu file%s overriding base assets\n",
                     gAssetGroupCache.modReplacements.size(),
                     gAssetGroupCache.modReplacements.size() == 1 ? "" : "s");
    }
}
