set_project("tmc")
-- Keep in sync with port/port_version.h.
local TMC_PC_VERSION = "0.8.3"
set_version(TMC_PC_VERSION)
set_xmakever("2.7.0")

-- ====================
-- Configuration
-- ====================
add_rules("mode.debug", "mode.release")
set_defaultmode("release")

-- Game version options
option("game_version")
    set_default("USA")
    set_showmenu(true)
    set_description("Game version to build", "USA", "EU", "JP", "DEMO_USA", "DEMO_JP")
option_end()

-- Multi-region (fat) PC binary: one binary that detects the loaded ROM and
-- selects USA/EU/JP data + behavior at runtime. Compiles src/ once; the
-- REGION_IS_* macros (include/region.h) become runtime instead of compile-time.
-- game_version still controls the asset/offset baseline used for the build
-- (USA by default); only behavior sites already converted to REGION_IS_* are
-- runtime-driven. No effect on the matching GBA ROM build.
-- DEFAULT ON (M7): the shipped release has been the fat binary since v0.7.0;
-- defaulting dev builds to match removes the config drift between what
-- developers test and what users run. `--multi_region=n` (or build.py
-- --single-region) still builds the per-region binary — CI keeps it as a
-- guard against half-flattened MULTI_REGION #ifdefs.
option("multi_region")
    set_default(true)
    set_showmenu(true)
    set_description("Build one PC binary supporting USA/EU/JP at runtime (default; =n for per-region)")
option_end()

option("pc_avx2")
    set_default(true)
    set_showmenu(true)
    set_description("Enable AVX2 optimizations for tmc_pc when supported")
option_end()

option("pc_sanitize")
    set_default(false)
    set_showmenu(true)
    set_description("Build tmc_pc with -fsanitize=address,undefined for runtime UB/NULL-deref detection")
option_end()

option("pc_lto_type_check")
    set_default(false)
    set_showmenu(true)
    set_description("GCC LTO gate for cross-translation-unit type mismatches")
option_end()

option("pc_profile")
    set_default(false)
    set_showmenu(true)
    set_description("Build tmc_pc with -pg gprof instrumentation for hotspot profiling")
option_end()

-- GPU shader pipeline (SDL_GPU). Stage 1: scaffold only — init the
-- device, load the passthrough SPIR-V shaders, log. Doesn't yet drive
-- presentation (SDL_Renderer still owns the screen). Stage 2 onward
-- replaces port_ppu.cpp's present path with the SDL_GPU one, unlocking
-- per-pixel shader filters (CRT, scanlines, lcd-grid, etc.) without
-- the CPU-side approximation tradeoff. See port/port_gpu_renderer.{h,cpp}.
option("gpu_renderer")
    -- Default ON: GPU (SDL_GPU) presentation is the primary path, with an
    -- automatic SDL_Renderer software fallback (render_backend=auto tries GPU
    -- first, falls back cleanly — see port_main.c / port_ppu.cpp). Compiling it
    -- in everywhere means capable devices use the GPU; devices whose driver
    -- can't provide a usable backend (e.g. an Adreno 405 GSI with a stub Vulkan
    -- ICD) fall back to software automatically. Costs ~150 KB embedded SPIR-V.
    -- Pass --gpu_renderer=n to compile a pure-software binary.
    set_default(true)
    set_showmenu(true)
    set_description("Compile the SDL_GPU presentation path (default ON; auto-falls back to SDL_Renderer).")
option_end()

-- Widescreen: render the GBA frame at a non-native horizontal width by
-- overriding MODE1_GBA_WIDTH at compile time.
--   240: GBA-native (3:2). No widescreen, no pillarbox, no stretch.
--   >240: ViruaPPU pillarboxes BG/OAM at col 240 (the engine's 32-tile
--         BG buffer holds reliable tile data only in cols 0..29, plus
--         parked off-screen sprites at x>=240). port_ppu.cpp uniformly
--         stretches the 240-px frame to fill the wider window. Real
--         widescreen needs a 64-tile sa2-style BGCNT_TXT512x256 engine
--         extension — Phase 2.
-- Default 240 = clean, no artifacts.
option("widescreen_width")
    set_default(240)
    set_showmenu(true)
    set_description("MODE1_GBA_WIDTH (240=native, >240=stretched until Phase 2)")
option_end()

-- Build directories
local build_dir = "build/$(plat)"
-- Host build utilities (asset_processor, gbagfx, preproc, ...). They exist to
-- be RUN on the machine doing the build, so `tools/bin` must keep host
-- binaries. A cross build (`-p android`) compiles every target for the target
-- ABI, and with a shared targetdir it used to overwrite tools/bin/* with
-- aarch64-Android executables — after which `xmake extract_assets` (and any
-- other host tool invocation) died with "cannot execute binary file". Park the
-- cross-built copies somewhere harmless so the host set survives. See #3: this
-- is what made the EU asset-extraction step impossible to run from an Android
-- configuration.
local tools_bin = is_plat("android") and ("build/android/tools/" .. (get_config("arch") or "arm64-v8a"))
                  or "tools/bin"

-- True when the active target arch is x86-64. ARM (arm64/aarch64) ports
-- skip x86-only codegen flags (-mavx2, -mno-ms-bitfields) and the GCC-only
-- -static-libgcc/-static-libstdc++ (aarch64 llvm-mingw uses -static alone).
-- 32-bit arches are not listed: the PC port targets 64-bit systems only
-- (enforced by a pointer-size static assert in port/port_types.h).
local function is_x86_target()
    return is_arch("x64", "x86_64", "amd64")
end

-- Force GCC-style bitfield packing on MinGW (x86 only).
--
-- MinGW defaults to `-mms-bitfields` (MSVC-compatible bitfield layout),
-- which gives different sizeof() and member offsets than Linux GCC for
-- any `__attribute__((packed))` struct that contains bitfields. The TMC
-- decomp has several such structs (e.g. gUnk_08128DE8_struct_2 in the
-- pause-menu cursor data — 5+5+6 bits, packs to 2 bytes on Linux but
-- 3 bytes on MinGW), so the same C source compiles to different memory
-- layouts on the two platforms — manifests as #44 (Windows pause-map
-- cursor at wrong position, grey-blocks/bouncing). `-mno-ms-bitfields`
-- restores parity with Linux GCC across the board.
--
-- It's an x86 GCC option that doesn't apply to the AArch64 ABI (and
-- aarch64 llvm-mingw clang rejects it), so it's gated to x86 targets.
-- A Windows-ARM64 bitfield divergence would be caught by the
-- PORT_STATIC_ASSERT layout checks.
local function add_mingw_bitfield_parity()
    if is_plat("windows", "mingw") and is_x86_target() then
        add_cflags("-mno-ms-bitfields", {force = true})
        add_cxxflags("-mno-ms-bitfields", {force = true})
    end
end

-- Statically link the runtime so the Windows binary is standalone.
-- `-static` covers everything (incl. libgcc/libstdc++ or, on aarch64
-- llvm-mingw, libc++/libunwind/compiler-rt). The extra -static-libgcc/
-- -static-libstdc++ are GCC spellings kept for the established x86 build;
-- aarch64 clang doesn't need them and may reject -static-libstdc++.
local function add_mingw_static_runtime()
    if is_plat("windows", "mingw") then
        add_ldflags("-static", {force = true})
        if is_x86_target() then
            add_ldflags("-static-libgcc", {force = true})
        end
        add_mingw_bitfield_parity()
    end
end

local function add_mingw_static_cpp_runtime()
    add_mingw_static_runtime()
    if is_plat("windows", "mingw") and is_x86_target() then
        add_ldflags("-static-libstdc++", {force = true})
    end
end

-- ====================
-- Third-party packages
-- ====================
local use_system_packages = is_host("linux") and (os.getenv("XMAKE_USE_SYSTEM_SDL3") or os.getenv("IN_NIX_SHELL"))
if use_system_packages then
    add_requires("nlohmann_json", {system = true, configs = {cmake = false}})
    add_requires("fmt", {configs = {header_only = true}})
    add_requires("libsdl3", {system = true})
else
    add_requires("nlohmann_json", {configs = {cmake = false}})
    -- #15: passing system=false makes xmake ignore the host's
    -- pacman::fmt / apt::libfmt-dev package (which is always shared)
    -- and build fmt from source as a header-only target. Without
    -- this, header_only=true was silently ignored and the binary
    -- recorded a NEEDED dep on libfmt.so.12, which broke on Fedora 43
    -- (ships fmt 11.x).
    add_requires("fmt", {system = false, configs = {header_only = true}})
    -- Pinned: the Android Java glue in android/app/src/main/java/org/libsdl/
    -- is vendored from this exact SDL release. A floating version desyncs the
    -- JNI signatures (3.4.12 added a scancode arg to onNativePadDown; the
    -- 3.4.4-era glue shipped in v0.8.1 crashed every Android launch with
    -- NoSuchMethodError at registration). Bump BOTH together.
    add_requires("libsdl3 3.4.12", {configs = {shared = false}})
end
add_requires("guilite")

-- libpng + zlib are linked by port_bugreport (F9 screenshot codec) and
-- by gbagfx (build-time PNG codec). On Linux + macOS we have system
-- packages (apt libpng-dev / brew libpng); on Windows MinGW the system
-- isn't available so xmake has to build from source. Mark the system
-- preference per platform and DON'T pass optional=true on Windows —
-- without libpng there, port_bugreport.cpp fails to compile (png.h
-- missing) and the whole release build dies.
if is_plat("mingw", "windows") then
    add_requires("libpng")
    add_requires("zlib")
elseif is_plat("android") then
    -- NDK triplet has no system packages; build both from source (small,
    -- cross-compile cleanly).
    add_requires("libpng", {system = false})
    add_requires("zlib",   {system = false})
else
    add_requires("libpng", {system = true, optional = true})
    add_requires("zlib",   {system = true, optional = true})
end

-- Global -mno-ms-bitfields on MinGW so the entire codebase matches the
-- Linux GCC bitfield layout. MinGW's default `-mms-bitfields` makes
-- packed-bitfield structs (e.g. pauseMenu.c's gUnk_08128DE8_struct_2 —
-- 5+5+6 packed bits) size to 3 bytes instead of 2, shifting every
-- subsequent member offset in the containing struct. Manifests as
-- issue #44 (Windows-only pause-map cursor at wrong position +
-- bouncing). Pin the GCC layout globally — every target picks it up,
-- so we don't have to remember to call a helper per target.
if is_plat("mingw", "windows") and is_x86_target() then
    add_cflags("-mno-ms-bitfields", {force = true})
    add_cxxflags("-mno-ms-bitfields", {force = true})
end

-- Dear ImGui for the in-game settings UI. Built against the SDL3 +
-- SDL_Renderer backends so it sits on top of the existing PPU present
-- without needing a separate GL/Vulkan context. Header-only-ish: the
-- xmake package builds a tiny static lib that wraps imgui.cpp +
-- imgui_demo.cpp + the two backend cpps.
-- Enable both SDL_Renderer and SDL_GPU ImGui backends so the F8 menu
-- works on the default build (SDL_Renderer) AND on --gpu_renderer=y
-- builds (SDL_GPU). The two backends are mutually exclusive at runtime
-- — port_imgui_menu.cpp picks via TMC_GPU_RENDERER define — but the
-- compiled package needs both files available.
add_requires("imgui v1.92.7", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true}})

-- #15: even with `header_only = true` requested above, the xmake fmt
-- package still links the system libfmt.so when one happens to be
-- installed on the build host (Arch ships fmt 12.x; Fedora 43 ships
-- 11.x, so the resulting binary ImportError'd on Fedora).  Force the
-- header-only path everywhere by defining FMT_HEADER_ONLY globally;
-- fmt's headers then inline all the formatting code and the binary
-- needs no libfmt at runtime.  --as-needed drops the now-unused
-- `-lfmt` xmake still passes on the link line so the binary stops
-- recording libfmt.so as a runtime dependency.
add_defines("FMT_HEADER_ONLY=1")
if is_plat("linux") then
    add_ldflags("-Wl,--as-needed", {force = true})
end

-- ====================
-- Tools
-- ====================

-- agb2mid
target("agb2mid")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/agb2mid/*.cpp")
    add_includedirs("tools/src/agb2mid")
    add_mingw_static_cpp_runtime()
target_end()

-- aif2pcm
target("aif2pcm")
    set_kind("binary")
    set_languages("c11")
    set_targetdir(tools_bin)
    add_files("tools/src/aif2pcm/*.c")
    add_includedirs("tools/src/aif2pcm")
    add_mingw_static_runtime()
target_end()

-- asset_processor
target("asset_processor")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/asset_processor/*.cpp")
    add_files("tools/src/asset_processor/assets/*.cpp")
    add_includedirs("tools/src/asset_processor")
    add_includedirs("tools/src/util")
    add_packages("nlohmann_json")
    add_mingw_static_cpp_runtime()
target_end()

-- asset_extractor
target("asset_extractor")
    set_kind("binary")
    set_languages("cxx20")
    set_targetdir("build/pc")
    add_defines("PC_PORT", "NON_MATCHING", "USA", "ENGLISH", "REVISION=0")
    add_files("tools/src/assets_extractor/*.cpp")
    remove_files("tools/src/assets_extractor/asset_extractor_runner.cpp")
    add_files("port/port_asset_pipeline.cpp")
    add_files("port/port_asset_log.cpp")
    add_files("port/port_asset_pak.cpp")
    add_files("port/port_asset_index.c")
    add_includedirs("tools/src/assets_extractor")
    add_includedirs("include", "port", ".")
    add_packages("nlohmann_json", "fmt")
    add_mingw_static_cpp_runtime()
    -- Embed assets/sounds.json into the binary so the extractor can guarantee
    -- it appears next to itself even when a release tarball forgets to ship
    -- the file (the v0.1.6 packaging bug behind issue #50). xmake's bin2c
    -- rule writes a raw "0xNN, 0xNN, ..." byte sequence to a header that we
    -- #include inside a C array initializer (see embedded_sounds_json.cpp).
    add_rules("utils.bin2c", {extensions = {".json"}})
    add_files("assets/sounds.json", {rule = "utils.bin2c", nozeroend = true})
    after_build(function (target)
        local mirrored_exe = path.join(tools_bin, path.filename(target:targetfile()))
        if mirrored_exe ~= target:targetfile() then
            os.cp(target:targetfile(), mirrored_exe)
        end
    end)
target_end()

-- bin2c
target("bin2c")
    set_kind("binary")
    set_languages("c11")
    set_targetdir(tools_bin)
    add_files("tools/src/bin2c/*.c")
    add_includedirs("tools/src/bin2c")
    add_mingw_static_runtime()
target_end()

-- gbafix
target("gbafix")
    set_kind("binary")
    set_languages("c11")
    set_targetdir(tools_bin)
    add_files("tools/src/gbafix/*.c")
    add_includedirs("tools/src/gbafix")
    add_mingw_static_runtime()
target_end()

-- gbagfx
target("gbagfx")
    set_kind("binary")
    set_languages("c11")
    set_targetdir(tools_bin)
    add_files("tools/src/gbagfx/*.c")
    add_includedirs("tools/src/gbagfx")
    add_packages("libpng", "zlib")
    add_mingw_static_runtime()
target_end()

-- mid2agb
target("mid2agb")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/mid2agb/*.cpp")
    add_includedirs("tools/src/mid2agb")
    add_mingw_static_cpp_runtime()
target_end()

-- preproc
target("preproc")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/preproc/*.cpp")
    add_includedirs("tools/src/preproc")
    add_mingw_static_cpp_runtime()
target_end()

-- scaninc
target("scaninc")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/scaninc/*.cpp")
    add_includedirs("tools/src/scaninc")
    add_packages("fmt")
    add_mingw_static_cpp_runtime()
target_end()

-- tmc_strings
target("tmc_strings")
    set_kind("binary")
    set_languages("cxx17")
    set_targetdir(tools_bin)
    add_files("tools/src/tmc_strings/*.cpp")
    add_includedirs("tools/src/tmc_strings")
    add_packages("nlohmann_json", "fmt")
    add_mingw_static_cpp_runtime()
target_end()

-- Group all tools
target("tools")
    set_kind("phony")
    add_deps("agb2mid", "aif2pcm", "asset_processor", "bin2c", "gbafix", "gbagfx", "mid2agb", "preproc", "scaninc", "tmc_strings")
target_end()

-- ====================
-- Asset Tasks
-- ====================

-- Extract assets task
task("extract_assets")
    set_category("plugin")
    on_run(function ()
        import("core.project.config")
        config.load()
        
        local game_version = get_config("game_version") or "USA"
        local build_assets_dir = "build/" .. game_version .. "/assets"
        
        print("===========================================")
        print("Extracting assets for " .. game_version)
        print("===========================================")
        
        -- Build tools first
        print("[1/3] Building tools...")
        os.exec("xmake build tools")
        print("[1/3] Tools built successfully!")
        
        -- Create build directory
        print("[2/3] Creating build directory: " .. build_assets_dir)
        os.mkdir(build_assets_dir)
        
        -- Run asset_processor extract
        print("[3/3] Running asset_processor extract (verbose mode)...")
        print("-------------------------------------------")
        os.execv("tools/bin/asset_processor", {"-v", "extract", game_version, build_assets_dir})
        print("-------------------------------------------")
        
        print("===========================================")
        print("Assets extracted to " .. build_assets_dir)
        print("===========================================")
    end)
    set_menu {
        usage = "xmake extract_assets [options]",
        description = "Extract assets from baserom",
        options = {}
    }
task_end()

-- Convert assets task  
task("convert_assets")
    set_category("plugin")
    on_run(function ()
        import("core.project.config")
        config.load()
        
        local game_version = get_config("game_version") or "USA"
        local build_assets_dir = "build/" .. game_version .. "/assets"
        
        print("===========================================")
        print("Converting assets for " .. game_version)
        print("===========================================")
        
        -- Build tools first
        print("[1/3] Building tools...")
        os.exec("xmake build tools")
        print("[1/3] Tools built successfully!")
        
        -- Create build directory
        print("[2/3] Checking build directory: " .. build_assets_dir)
        os.mkdir(build_assets_dir)
        
        -- Run asset_processor convert
        print("[3/3] Running asset_processor convert (verbose mode)...")
        print("-------------------------------------------")
        os.execv("tools/bin/asset_processor", {"-v", "convert", game_version, build_assets_dir})
        print("-------------------------------------------")
        
        print("===========================================")
        print("Assets converted to " .. build_assets_dir)
        print("===========================================")
    end)
    set_menu {
        usage = "xmake convert_assets [options]",
        description = "Convert extracted assets",
        options = {}
    }
task_end()

-- Build assets task
task("build_assets")
    set_category("plugin")
    on_run(function ()
        import("core.project.config")
        config.load()
        
        local game_version = get_config("game_version") or "USA"
        local build_assets_dir = "build/" .. game_version .. "/assets"
        
        -- Build tools first
        os.exec("xmake build tools")
        
        -- Create build directory
        os.mkdir(build_assets_dir)
        
        -- Run asset_processor build
        os.execv("tools/bin/asset_processor", {"build", game_version, build_assets_dir})
        
        print("Assets built to " .. build_assets_dir)
    end)
    set_menu {
        usage = "xmake build_assets [options]",
        description = "Build assets from source",
        options = {}
    }
task_end()

-- ====================
-- PC Port Target
-- ====================
target("tmc_pc")
    if is_plat("android") then
        -- Android: SDL's Java glue (SDLActivity) loads the game as a JNI
        -- shared library named libmain.so and calls its SDL_main. Own
        -- targetdir per ABI so android/gradle can consume them as jniLibs
        -- and desktop build/pc is never clobbered.
        set_kind("shared")
        set_basename("main")
        set_targetdir("build/android/" .. (get_config("arch") or "arm64-v8a"))
        -- Android 15+ requires 16 KB page-aligned segments (same flag the
        -- SameBoy Android port ships).
        add_shflags("-Wl,-z,max-page-size=16384", {force = true})
    else
        set_kind("binary")
        set_targetdir("build/pc")
    end
    set_languages("c11", "cxx20")

    local use_avx2 = get_config("pc_avx2")
    if use_avx2 == nil then
        use_avx2 = true
    end
    local target_arch = get_config("arch") or ""
    local arch_supports_avx2 = is_arch("x64", "x86_64", "amd64")
    if not arch_supports_avx2 and target_arch ~= "" then
        local arch_l = target_arch:lower()
        arch_supports_avx2 = (arch_l == "x64" or arch_l == "x86_64" or arch_l == "amd64")
    end

    -- The software GBA PPU is now vendored first-party under port/ppu/ (see
    -- port/ppu/README.md); there is no longer a ViruaPPU submodule to patch at
    -- build time. This before_build makes sure the region's blob-offset headers
    -- exist, regenerates the embedded sounds blob, and logs the AVX2 decision.
    before_build(function (target)
        -- ---- Per-region blob-offset headers (issue #3) ----
        -- src/common.c, src/data/objPalettes.c and friends #include
        -- "assets/map_offsets.h" / "assets/gfx_offsets.h", resolved through
        -- add_includedirs("build/<game_version>"). Only the USA pair is
        -- committed (see .gitignore), and they are NOT interchangeable: every
        -- offset past the first ~30 differs between USA and EU. So building
        -- --game_version=EU (or JP) on a fresh checkout used to die with a bare
        --     fatal error: 'assets/map_offsets.h' file not found
        -- with no hint that an extraction step was missing. Generate them here
        -- when we can, and otherwise say exactly what to run.
        do
            local ver = get_config("game_version") or "USA"
            local dir = path.join(os.projectdir(), "build", ver, "assets")
            if not os.isfile(path.join(dir, "map_offsets.h")) or not os.isfile(path.join(dir, "gfx_offsets.h")) then
                local baseroms = {
                    USA = "baserom.gba", EU = "baserom_eu.gba", JP = "baserom_jp.gba",
                    DEMO_USA = "baserom_demo.gba", DEMO_JP = "baserom_demo_jp.gba",
                }
                local rom = baseroms[ver] or "baserom.gba"
                local proc = path.join(os.projectdir(), "tools", "bin", "asset_processor")
                if is_host("windows") then proc = proc .. ".exe" end
                local hint = "Generate them on the HOST (they cannot be produced from a cross build):\n"
                    .. "    xmake f -P . -y -p " .. (is_host("windows") and "windows" or (is_host("macosx") and "macosx" or "linux")) .. "\n"
                    .. "    xmake build -P . -y asset_processor\n"
                    .. "    " .. proc .. " extract " .. ver .. " build/" .. ver .. "/assets\n"
                    .. "then re-run your original configure + build."
                if not os.isfile(path.join(os.projectdir(), rom)) then
                    raise("build/" .. ver .. "/assets/{map,gfx}_offsets.h are missing and " .. rom
                          .. " is not in the project root, so they cannot be generated.\n"
                          .. "A " .. ver .. " build needs a " .. ver .. " ROM named " .. rom .. ".\n" .. hint)
                end
                local generated = os.isfile(proc) and try {
                    function ()
                        os.mkdir(dir)
                        -- Cross-built (e.g. aarch64-Android) copies of the tool
                        -- live elsewhere now, but a stale one may still be here;
                        -- os.execv raises on "cannot execute", caught by try.
                        os.execv(proc, {"extract", ver, path.join("build", ver, "assets")})
                        return true
                    end
                }
                if not generated or not os.isfile(path.join(dir, "map_offsets.h")) then
                    raise("build/" .. ver .. "/assets/{map,gfx}_offsets.h are missing and could not be generated"
                          .. " automatically (no runnable host tools/bin/asset_processor).\n" .. hint)
                end
                print("[tmc_pc] generated build/%s/assets/{map,gfx}_offsets.h from %s", ver, rom)
            end
        end

        -- Regenerate port/generated_sounds_embed.cpp from
        -- assets/sounds.json so the binary always carries an
        -- up-to-date fallback. The Python helper no-ops when the
        -- output is already byte-identical, so xmake's incremental
        -- cache stays warm. Missing input -> empty fallback (the
        -- audio backend logs "songs will be silent" in that case).
        do
            local script = path.join(os.projectdir(), "tools", "generate_sounds_embed.py")
            local input  = path.join(os.projectdir(), "assets", "sounds.json")
            local output = path.join(os.projectdir(), "port", "generated_sounds_embed.cpp")
            if os.isfile(script) then
                local ok = try {
                    function ()
                        os.execv("python3", {script, input, output})
                        return true
                    end
                }
                if not ok then
                    -- Fall back to `python` (Windows installs without
                    -- the python3 shim).
                    try {
                        function ()
                            os.execv("python", {script, input, output})
                            return true
                        end
                    }
                end
            end
        end

        print("[tmc_pc] arch=%s pc_avx2=%s", target_arch ~= "" and target_arch or "auto", use_avx2 and "on" or "off")
        if use_avx2 and arch_supports_avx2 then
            print("[tmc_pc] AVX2: enabled")
        elseif not use_avx2 then
            print("[tmc_pc] AVX2: disabled (pc_avx2 option is off)")
        else
            print("[tmc_pc] AVX2: disabled (unsupported target arch)")
        end
    end)

    -- PC port version configurations
    local pc_versions = {
        USA = { region = "USA", language = "ENGLISH" },
        EU  = { region = "EU",  language = "ENGLISH" },
        -- JP is wired but ROM-gated: building it needs a JP baserom (BZMJ),
        -- extracted JP assets in build/JP/, and the generated port_offset_JP.h.
        -- region/language become the JP + JAPANESE defines the decomp's
        -- #ifdef JP paths expect. See docs/JP_PORT_ENABLEMENT.md.
        JP  = { region = "JP",  language = "JAPANESE" },
    }
    local pc_game_version = get_config("game_version") or "USA"
    local pc_ver = pc_versions[pc_game_version] or pc_versions["USA"]
    if pc_versions[pc_game_version] == nil then
        print("WARNING: PC port has no config for game_version='" .. pc_game_version
              .. "'; falling back to USA. (JP is ROM-gated — see docs/JP_PORT_ENABLEMENT.md.)")
    end

    add_defines("PC_PORT", "NON_MATCHING", "USE_HDMA", pc_ver.region, pc_ver.language, "REVISION=0")
    -- Inject the version string from the top-of-file constant so the
    add_defines('TMC_PC_VERSION="' .. TMC_PC_VERSION .. '"')
    add_defines('TMC_PORT_VERSION="' .. TMC_PC_VERSION .. '"')

    -- Multi-region fat binary: switch REGION_IS_* (include/region.h) to runtime.
    -- Force-include region.h everywhere so converted gameplay-behavior sites and
    -- the matching ROM build see the same macros. region.h is pure macros in the
    -- non-multi-region path, so this is inert for normal single-region builds.
    add_forceincludes("region.h")
    if get_config("multi_region") then
        add_defines("MULTI_REGION")
        print("PC port: MULTI_REGION build — one binary for USA/EU/JP at runtime "
              .. "(asset/offset baseline: " .. pc_game_version .. ").")
    end

    -- Widescreen: single source of truth for the rendered viewport width.
    -- The orphaned `widescreen_width` option is injected here as
    -- MODE1_GBA_WIDTH so BOTH the ViruaPPU renderer (compiled into this
    -- target via add_files) and the port's engine-side cull tests
    -- (CheckOnScreen / CheckRectOnScreen, via port/port_widescreen.h) key
    -- off one value and can never disagree. 240 = GBA-native; the cull
    -- bounds collapse to their original literals at 240.
    local ws_width = get_config("widescreen_width") or 240
    add_defines("MODE1_GBA_WIDTH=" .. ws_width)

    -- Discord Rich Presence application ID — optional, gitignored.
    -- Local devs/release builders drop their Discord App ID into
    -- discord_app_id.txt at repo root and it gets baked in as the
    -- compile-time fallback used when TMC_DISCORD_APP_ID env var is
    -- unset. The file is in .gitignore so the public repo never
    -- contains it (see port/port_discord_rpc.c security-model note).
    --
    -- File-reading APIs only exist in script-scope (on_load), so we
    -- inject the macro from there rather than the surrounding
    -- description scope.
    on_load(function (target)
        if os.isfile("discord_app_id.txt") then
            local app_id = io.readfile("discord_app_id.txt")
            if app_id then
                app_id = app_id:gsub("%s+", "")
                if app_id ~= "" then
                    target:add("defines",
                        'TMC_DISCORD_APP_ID_DEFAULT="' .. app_id .. '"')
                end
            end
        end
    end)
    if use_avx2 and arch_supports_avx2 then
        add_defines("USE_AVX2")
        add_cflags("-mavx2", "-mfma", {tools = {"gcc", "clang"}})
        add_cxxflags("-mavx2", "-mfma", {tools = {"gcc", "clang"}})
        add_cflags("/arch:AVX2", {tools = {"cl"}})
        add_cxxflags("/arch:AVX2", {tools = {"cl"}})
    end

    -- Include directories
    add_includedirs("include", "libs")
    add_includedirs("port")
    add_includedirs(".")
    add_includedirs("build/" .. pc_game_version) -- For assets/map_offsets.h etc (USA, EU, or JP)
    add_includedirs("port/ppu/include")          -- vendored software GBA PPU
    add_includedirs("port/shaders/build")        -- generated MSL .inl for the GPU rasterizer (optional)
    add_includedirs("libs/VirtuaAPU/include")
    add_includedirs("libs/agbplay_core")
    add_includedirs("tools/src/assets_extractor") -- AssetExtractorApi linked in-process

    -- tmc-Modern-Launcher is a private matheo repo; gate on submodule
    -- presence so checkouts without access still build. Detect by
    -- looking for the launcher's public header (the submodule pointer
    -- exists in .gitmodules but the working tree is empty when the
    -- clone fails).
    if os.isfile("libs/tmc-Modern-Launcher/include/tmc_launcher.h") then
        add_defines("launcher", "GUILITE_ON", "TMC_HAS_MODERN_LAUNCHER=1")
        add_includedirs("libs/tmc-Modern-Launcher/include")
        add_includedirs("libs/tmc-Modern-Launcher/3p")
        add_rules("utils.bin2c", {extensions = {".png"}})
        add_files("libs/tmc-Modern-Launcher/assets/github.png", {rule = "utils.bin2c", nozeroend = true})
        add_files("libs/tmc-Modern-Launcher/src/launcher_github_icon.cpp")
        add_files("libs/tmc-Modern-Launcher/src/tmc_launcher.cpp")
        add_files("port/port_launcher_bootstrap.cpp")
    end

    add_files("port/port_main.c")
    add_files("port/port_android_log.c") -- stderr->logcat bridge (no-op off Android)
    add_files("port/port_audio.c")
    add_files("port/port_shm_framebuffer.c")  -- publishes GBA framebuffer to shm (opt-in)
    add_files("port/port_runtime_config.cpp")
    add_files("port/port_debug_menu.cpp")
    add_files("port/port_imgui_menu.cpp")
    add_files("port/port_prelaunch_logo.cpp")
    add_files("port/port_tts.cpp")
    add_files("port/port_a11y_cues.c")     -- accessibility audio cues (surroundings scan, F10)
    add_files("port/port_a11y_audio.c")    -- spatialized tone cues (audio-thread mixer)
    -- Embed docs/picori-logo.png into the binary so the prelaunch
    -- screen always has the logo regardless of cwd / install layout.
    -- xmake's utils.bin2c rule writes a "0xNN, 0xNN, ..." byte sequence
    -- to a header we include inside an array initializer
    -- (see port_prelaunch_logo.cpp).
    add_rules("utils.bin2c", {extensions = {".png"}})
    add_files("docs/picori-logo.png", {rule = "utils.bin2c", nozeroend = true})
    add_files("port/port_debug_actions.c")
    add_files("port/port_debug_entities.c")
    add_files("port/port_debug_memory_watch.c")  -- live GBA memory-watch list (F8 Memory tab)
    add_files("port/port_quicksave.c")
    add_files("port/port_practice.c")   -- Speedrun practice mode (timer/inputs/reload/slow-mo)
    add_files("port/port_inline_ptrs.c")
    add_files("port/port_script_addrs.c")  -- per-region GBA_script_* address translation (M3)
    add_files("port/port_offset_remap.c")  -- per-region compiled blob-offset remap (M7, generated)
    add_files("port/port_asset_bootstrap.cpp")
    add_files("port/port_asset_index.c")
    add_files("port/port_update_check.c")
    add_files("port/port_asset_loader.cpp")
    add_files("port/port_asset_pipeline.cpp")
    add_files("port/port_asset_log.cpp")
    add_files("port/port_asset_pak.cpp")
    add_files("port/port_asset_pak_loader.cpp")
    add_files("port/port_exe_path.cpp")   -- shared executable-directory query (C++)
    add_files("port/port_debug_verbose.c")  -- per-frame log gate, env-controlled
    add_files("port/port_rom_picker.c")     -- SDL3 file picker when no ROM is found
    -- In-process randomizer (port/rando/), derived from the GPL-3.0 Minish Cap
    -- randomizer (minishmaker/randomizer); GPL-3.0, see THIRD-PARTY-LICENSES.md.
    add_files("port/rando/rando.cpp")
    add_files("port/rando/rando_logic.cpp")
    add_files("port/rando/rando_file_menu.c")
    add_files("port/rando/rando_save.c")
    add_files("port/rando/rando_runtime.c")    -- eventdefine-driven runtime features (C: needs player.h/save.h layouts)
    add_files("port/rando/rando_newfile.c") -- new-file randomizer baseline flag/figurine tables
    add_files("port/rando/rando_entrance.cpp") -- coupled dungeon-entrance shuffle
    add_files("port/rando/rando_cosmetic.cpp") -- tunic / heart color eventdefines
    add_files("port/rando/rando_music.c")    -- MUSIC_RANDO area-BGM remap
    add_files("port/rando/rando_keymap.c")   -- curated ground-item location keys (area-room-flag)
    -- Minish Cap Reborn parity toggles, ported from Admentus64/The-Minish-Cap-
    -- Reborn (GPL-3.0); GPL-3.0, see THIRD-PARTY-LICENSES.md.
    add_files("port/port_reborn.cpp")
    -- Env-gated auto-repro / capture harnesses (no-op when off).
    add_files("port/port_repro_perfcap.c")
    add_files("port/port_repro_rando.c")
    add_files("port/port_repro_a11y.c")
    add_files("port/port_repro_roomcap.c")  -- generic in-game room capture (TMC_ROOMCAP)
    add_files("port/port_repro_roll_macro.c") -- roll-attack macro e2e test (TMC_REPRO_ROLL_MACRO)
    add_files("port/port_repro_npc_talk.c") -- NPC-talk e2e test (TMC_REPRO_NPC_TALK)
    add_files("port/port_repro_itemget.c") -- item-get perf repro (TMC_REPRO_ITEMGET)
    -- Link the asset extractor implementation directly so tmc_pc can
    -- run extraction in-process at startup (no shell-out) and share
    -- the engine's already-loaded ROM buffer.
    add_files("tools/src/assets_extractor/assets_extractor_api.cpp")
    add_files("port/port_m4a_backend.cpp")
    add_files("port/generated_sounds_embed.cpp")  -- compile-time sounds.json fallback
    add_files("port/port_ppu.cpp")      -- PPU bridge (C++ → ViruaPPU)
    add_files("port/port_gpu_renderer.cpp")  -- SDL_GPU presentation (Stage 1: scaffold; gated on --gpu_renderer=y)
    add_files("port/port_gpu_raster.cpp")    -- SDL_GPU PPU rasterizer (docs/gpu-rasterizer-design.md; gated on --gpu_renderer=y)
    add_files("port/port_gpu_obj_cull.c")    -- per-line OBJ candidate cull (shared by GPU raster backends)
    add_files("port/port_gpu_raster_gl.cpp") -- GLES 3.1 compute PPU rasterizer (no-Vulkan devices; docs/gpu-rasterizer-design.md)
    add_files("port/port_glslp_parser.cpp")  -- libretro .glslp parser — steps 1-4 of the runtime scaffold (Stage 5+C)
    add_files("port/port_glslp_runtime.cpp") -- libretro .glslp runtime — step 5 (load + shader-build; present pending)

    -- SDL_GPU shader blobs. Compiled offline via port/shaders/build.sh
    -- (run when *.vert / *.frag change); the .spv files are committed
    -- so the build doesn't depend on glslangValidator on every host.
    -- Only embedded when the gpu_renderer option is enabled — keeps
    -- the default build identical in size for users who don't opt in.
    if has_config("gpu_renderer") then
        add_defines("TMC_GPU_RENDERER=1")
        -- The extensions filter on the rule auto-matches add_files
        -- entries with .spv suffix — re-specifying `rule = "utils.bin2c"`
        -- on the add_files line was registering them twice and
        -- triggered xmake's "job has already been added" warning.
        add_rules("utils.bin2c", {extensions = {".spv", ".glsl"}, nozeroend = true})
        add_files("port/shaders/build/passthrough.vert.spv")
        add_files("port/shaders/build/passthrough.frag.spv")
        add_files("port/shaders/build/lcd_grid.frag.spv")
        add_files("port/shaders/build/scanline.frag.spv")
        add_files("port/shaders/build/handheld.frag.spv")
        add_files("port/shaders/build/vignette.frag.spv")
        add_files("port/shaders/build/crt_composite.frag.spv")
        add_files("port/shaders/build/crt_rf.frag.spv")
        add_files("port/shaders/build/blur5h.frag.spv")
        add_files("port/shaders/build/crt_rf_p2.frag.spv")
        add_files("port/shaders/build/ppu_raster.vert.spv")
        add_files("port/shaders/build/ppu_raster.frag.spv")
        -- GLES compute backend: embed the shared core GLSL (built at runtime).
        add_files("port/shaders/ppu_core.glsl")
    end
    add_files("port/port_icon.cpp")     -- SDL window icon (placeholder, ROM-extracted in future)
    add_files("port/port_mods.cpp")     -- Tier 1 mod loader: asset overrides from <exe>/mods/
    add_files("port/port_rom.c")        -- ROM loading & symbol resolution
    add_files("port/port_region_data.c") -- USA-compiled data -> active-region data provenance resolver
        -- PC port stubs for undefined symbols
    add_files("port/port_stubs.c")
    add_files("port/stubs_autogen.c")
    if has_config("pc_lto_type_check") and is_plat("linux") then
        -- Raw ROM blobs intentionally back many differently typed decomp symbols.
        -- Keep the gate focused on cross-TU code/runtime ABI, not ROM storage casts.
        add_files("port/data_stubs_autogen.c", {force = {cxflags = "-fno-lto"}})
        add_files("port/data_const_stubs.c", {force = {cxflags = "-fno-lto"}})
    else
        add_files("port/data_stubs_autogen.c")
        add_files("port/data_const_stubs.c") -- Const ROM data (generated tools/generate_const_data.py)
    end
    add_files("port/flag_remap_generated.c")  -- Per-region flag-ordinal remap (generated by tools/generate_flag_remap.py)
    add_files("port/port_rom_tables.c")   -- Compile-time ROM offset tables (generated by tools/generate_rom_tables.py)
    add_files("port/port_bios.c")
    add_files("port/port_bugreport.cpp")     -- F9 bug-report capture (screenshot + save + state dump)
    add_files("port/port_bugreport_state.c") -- Crash-handler state snapshot
    add_files("port/port_linked_stubs.c")
    add_files("port/port_figurines.c")  -- gFigurines[] resolved from ROM (#57)
    add_files("port/port_draw.c")
    add_files("port/port_gba_mem.c")
    add_files("port/port_hdma.c")    -- HBlank-DMA simulation (iris/circle WIN0H)
    add_files("port/port_upscale.c") -- xBRZ-style pixel-art upscaler
    add_files("port/port_save.c")        -- EEPROM save emulation
    add_files("port/port_softslots.c")   -- Extra item-equip buttons (X/Y/L2/R2)
    add_files("port/port_second_screen.c") -- Second-display panel (AYN Thor); compositor compiles everywhere, surface plumbing is Android-only
    add_files("port/port_second_screen_state.c") -- Thread-safe gSave/gRoomControls snapshot for the second screen
    add_files("port/port_second_screen_render.c") -- Item-icon compositor for the second screen (reads ROM/asset data directly)
    add_files("port/port_second_screen_worldmap.c") -- Hyrule map art for the second screen, decoded from ROM at runtime
    add_files("port/port_second_screen_dungeonmap.c") -- Authentic dungeon-map art for the second screen
    add_files("port/port_second_screen_quest.c") -- Quest-status screen art for the second screen
    add_files("port/port_second_screen_theme.c") -- TMC menu chrome / HUD art for the second screen, decoded from ROM
    if is_plat("android") then
        -- JNI bridge only — references <jni.h>/<android/native_window_jni.h>,
        -- doesn't compile on other platforms.
        add_files("port/port_second_screen_jni.cpp")
    end
    add_files("port/port_roll_attack_macro.c") -- One-button roll attack (default: D)
    add_files("port/port_touch_controls.cpp")
    add_files("port/port_filter.c")      -- CRT/LCD post-process filters
    add_files("port/port_animation.c")   -- Animation system (ported from ASM)
    add_files("port/port_math.c")        -- Math functions (CalcDistance, direction, Sqrt, Div)
    add_files("port/port_text_render.c") -- Text rendering (UnpackTextNibbles, glyph pixel writers)
    add_files("port/port_gameplay_stubs.c") -- Ported gameplay helpers from ASM (tile interaction, ice movement, SFX queue)
    add_files("port/port_m4a_stubs.c") -- Ported m4a API stubs with typed behavior for PC
    add_files("port/port_room_funcs.c") -- Room function pointer lookup table (generated)
    add_files("port/port_script_funcs.c") -- Script Call/CallWithArg function lookup (generated)
    add_files("port/port_analog_movement.c") -- 360° left-stick movement bridge (REBORN_FEAT_ANALOG_360_MOVEMENT)
    add_files("port/port_audio_mute.cpp")    -- per-category SFX mute toggles (F8 → Audio)
    add_files("port/port_discord_rpc.c")     -- Discord Rich Presence (Unix IPC, F8 → Display toggle)
    add_files("port/ppu/src/*.c")            -- vendored software GBA PPU (was libs/ViruaPPU)
    add_files("libs/VirtuaAPU/src/*.c")
    add_files("libs/agbplay_core/*.cpp")
    
    -- Game source files - Main game code
    add_files("src/main.c")
    add_files("src/common.c")
    add_files("src/interrupts.c")
    add_files("src/game.c")
    add_files("src/title.c")
    add_files("src/fileselect.c")
    add_files("src/flags.c")
    add_files("src/save.c")
    add_files("src/fade.c")
    add_files("src/color.c")
    add_files("src/vram.c")
    add_files("src/screen*.c")
    add_files("src/message.c")
    add_files("src/text.c")
    add_files("src/sound.c")
    add_files("src/script.c")
    add_files("src/scroll.c")
    add_files("src/room.c")
    add_files("src/roomInit.c")
    add_files("src/entity.c")
    add_files("src/physics.c")
    add_files("src/collision.c")
    add_files("src/movement.c")
    add_files("src/affine.c")
    add_files("src/sineTable.c")
    add_files("src/ui.c")
    
    -- Player
    add_files("src/player.c")
    add_files("src/playerUtils.c")
    add_files("src/playerHitbox.c")
    add_files("src/playerItem.c")
    add_files("src/playerItemUtils.c")
    add_files("src/playerItemDefinitions.c")
    add_files("src/playerItem/*.c")
    
    -- Entities
    add_files("src/enemy.c")
    add_files("src/enemyUtils.c")
    add_files("src/enemy/*.c")
    add_files("src/npc.c")
    add_files("src/npcUtils.c")
    add_files("src/npcFunctions.c")
    add_files("src/npcDefinitions.c")
    add_files("src/npc/*.c")
    add_files("src/object.c")
    add_files("src/objectUtils.c")
    add_files("src/objectDefinitions.c")
    add_files("src/object/*.c")
    add_files("src/enemyUpdate.c")
    add_files("src/projectile.c")
    add_files("src/projectileUtils.c")
    add_files("src/projectileUpdate.c")
    add_files("src/projectile/*.c")
    add_files("src/manager.c")
    add_files("src/manager/*.c")
    add_files("src/item.c")
    add_files("src/itemUtils.c")
    add_files("src/itemDefinitions.c")
    add_files("src/itemMetaData.c")
    add_files("src/item/*.c")
    
    -- Other game systems
    add_files("src/subtask.c")
    add_files("src/subtask/*.c")
    add_files("src/cutscene.c")  -- re-enabled for PC port
    add_files("src/backgroundAnimations.c")
    add_files("src/beanstalkSubtask.c")
    add_files("src/enterPortalSubtask.c")
    add_files("src/gameOverTask.c")
    add_files("src/gameUtils.c")
    add_files("src/gameData.c")
    add_files("src/kinstone.c")
    add_files("src/droptables.c")
    add_files("src/demo.c")
    add_files("src/debug.c")
    add_files("src/flagDebug.c")
    add_files("src/staffroll.c")
    add_files("src/menu/*.c")
    add_files("src/data/figurineMenuData.c")
    add_files("src/data/hitbox.c")
    add_files("src/data/areaMetadata.c")
    add_files("src/data/caveBorderMapData.c")
    add_files("src/data/data_080046A4.c")
    add_files("src/data/mapActTileToSurfaceType.c")
    add_files("src/data/objPalettes.c")
    add_files("src/data/screenTransitions.c")
    add_files("src/data/transitions.c")
    add_files("src/worldEvent/*.c")
    
    -- Code stubs (functions that need decompilation)
    add_files("src/code_08049CD4.c")
    add_files("src/code_08049DF4.c")
    add_files("src/code_0805EC04.c")
    

    
    -- GBA library (m4a sound) - skipped for PC, using stubs
    
    -- libpng + zlib are needed by port_bugreport.cpp for F9 screenshot
    -- PNG encoding. imgui drives the F8/ribbon menus (HEAD).
    -- guilite is matheo's mobile/touch UI overlay.
    -- Keep all of them so both UI stacks build cleanly.
    add_packages("libsdl3", "nlohmann_json", "fmt", "libpng", "zlib", "imgui", "guilite")


    -- Build a standalone Windows binary with MinGW (static SDL + runtimes).
    -- x86 uses the GCC -static-libgcc/-static-libstdc++ spellings; aarch64
    -- llvm-mingw gets -static alone (statically links libc++/libunwind/
    -- compiler-rt/winpthreads), since clang may reject -static-libstdc++.
    if is_plat("windows", "mingw") then
        add_ldflags("-static", {force = true})
        if is_x86_target() then
            add_ldflags("-static-libgcc", "-static-libstdc++", {force = true})
        end
        -- dbghelp: SymInitialize / SymFromAddr used by port_bugreport.cpp's
        -- WriteBacktraceWindows() for symbol resolution in the crash handler.
        add_syslinks("winhttp", "winpthread", "dbghelp")
    end
    
    -- Math library
    if is_plat("linux", "macosx") then
        add_links("m")
    end

    -- GLES compute rasterizer backend (port_gpu_raster_gl.cpp) needs EGL + GLES
    -- on Linux/Android; it's a no-op stub on Windows/macOS (Vulkan/Metal there).
    if has_config("gpu_renderer") and is_plat("linux", "android") then
        add_syslinks("EGL", "GLESv2")
        if is_plat("android") then
            -- The compute entry points that backend calls (glDispatchCompute,
            -- glBindBufferBase, glMemoryBarrier, glMapBufferRange,
            -- glUnmapBuffer) are GLES 3.1, and the NDK puts those in
            -- libGLESv3 — libGLESv2 stops at 2.0, so the link fails without
            -- this even though the desktop GL loader resolves them fine.
            add_syslinks("GLESv3")
        end
    end

    -- Compiler flags
    add_cflags("-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-missing-field-initializers",
               "-fno-strict-aliasing", "-fwrapv", "-fno-strict-overflow", "-O3", "-g",
               "-fvisibility=default", "-funsigned-char")

    add_cxxflags("-Wall", "-Wextra", "-Wno-unused-parameter",
                 "-fno-strict-aliasing", "-fwrapv", "-fno-strict-overflow", "-O3", "-g", "-funsigned-char")
    add_cflags("/J", {tools = {"cl"}})
    add_cxxflags("/J", {tools = {"cl"}})

    -- OpenMP scanline parallelism for the vendored PPU mode1.
    --
    -- port/ppu/src/mode1.c contains `#pragma omp parallel for
    -- schedule(static)` over the 160-row render loop. The .c files are
    -- pulled into this target directly via add_files(), so -fopenmp must
    -- be wired explicitly here or the pragmas compile as silent no-ops.
    -- Wire -fopenmp explicitly so
    -- the render actually runs on every core. The HDMA pre-line
    -- callback still runs serially (with per-line IO snapshot) so
    -- water-FX / BLDY fades stay correct; only the BG+OBJ+composite
    -- inner pass parallelises.
    --
    -- Platform notes:
    --   - Linux: gcc/clang both accept -fopenmp directly.
    --   - MinGW (Windows x86 cross): libgomp ships with the toolchain and
    --     is linked statically via the existing -static-libgcc.
    --   - macOS: Apple Clang doesn't accept bare -fopenmp (would need
    --     `brew install libomp` + `-Xpreprocessor -fopenmp -lomp`).
    --     The mode1 #pragma simply degrades to a serial loop on macOS
    --     when the flag isn't set — render is slower but visually
    --     identical. v0.3.0-experimental release CI hit this; skip the
    --     flag on macOS so the build succeeds.
    --   - Windows ARM64 (llvm-mingw): ships libomp (not libgomp); static
    --     linking of it under -static is unverified, so degrade to the
    --     same serial mode1 path as macOS to guarantee the build links.
    --     Re-enable once a CI run confirms libomp links cleanly.
    local openmp_ok = not is_plat("macosx")
    if is_plat("windows", "mingw") and not is_x86_target() then
        openmp_ok = false
    end
    if openmp_ok then
        add_cflags("-fopenmp")
        add_cxxflags("-fopenmp")
        add_ldflags("-fopenmp", {force = true})
        if is_plat("android") then
            -- Shared-lib link (libmain.so) uses shflags, and the NDK's
            -- libomp must be linked STATICALLY (-static-openmp) — devices
            -- don't ship a system libomp.so.
            add_shflags("-fopenmp", "-static-openmp", {force = true})
            add_ldflags("-static-openmp", {force = true})
        end
    end

    -- Optional sanitizer build (xmake f -y --pc_sanitize=y). Catches NULL
    -- deref, OOB reads/writes, signed-overflow UB, use-after-free with a
    -- precise stack trace at the moment of the violation.
    if has_config("pc_sanitize") then
        add_cflags  ("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all")
        add_cxxflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all")
        add_ldflags ("-fsanitize=address,undefined", {force = true})
        add_shflags ("-fsanitize=address,undefined", {force = true})
        print("[tmc_pc] sanitizer: AddressSanitizer + UndefinedBehaviorSanitizer enabled")
    end

    -- GCC-only whole-program ABI gate. LTO compares every external declaration
    -- with its definition and turns cross-translation-unit type drift into a
    -- link failure. CI enables this on Linux x86_64.
    if has_config("pc_lto_type_check") then
        add_cflags("-flto", "-Werror=lto-type-mismatch", {tools = {"gcc"}})
        add_cxxflags("-flto", "-Werror=lto-type-mismatch", {tools = {"gxx"}})
        add_ldflags("-flto", "-Werror=lto-type-mismatch", {force = true})
        print("[tmc_pc] GCC LTO cross-translation-unit type gate enabled")
    end

    -- Optional gprof profiling build (xmake f -y --pc_profile=y). Adds -pg
    -- instrumentation so a headless repro run drops gmon.out for
    -- `gprof ./tmc_pc gmon.out`. Run with OMP_NUM_THREADS=1 so the parallel
    -- scanline render attributes to the profiled main thread. Off by default.
    if has_config("pc_profile") then
        add_cflags  ("-pg", "-fno-omit-frame-pointer")
        add_cxxflags("-pg", "-fno-omit-frame-pointer")
        add_ldflags ("-pg", {force = true})
        print("[tmc_pc] gprof profiling: -pg enabled (run with OMP_NUM_THREADS=1; gmon.out -> gprof)")
    end

    -- Keep symbols even in release mode so SIGSEGV traces are useful
    -- locally (CI release tarballs may strip later). The xmake mode.release
    -- rule adds -s/--strip-all by default which makes addr2line useless.
    set_strip("none")
target_end()

-- ====================
-- Native randomizer self-test
-- ====================
target("rando_logic_test")
    set_kind("binary")
    set_languages("c11", "cxx20")
    set_targetdir("build/pc")
    add_includedirs("port")
    add_includedirs("include")
    add_files("port/rando/rando.cpp")
    add_files("port/rando/rando_logic.cpp")
    add_files("port/rando/rando_save.c")
    add_files("port/rando/rando_entrance.cpp")
    add_files("port/rando/rando_music.c")
    add_files("port/rando/rando_newfile.c")
    add_files("port/rando/rando_keymap.c")
    add_files("port/rando/rando_test.c")
target_end()

-- ====================
-- PRNG golden-vector regression test (guards the gameplay RNG from drift)
-- ====================
target("rng_golden_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("port")
    add_files("port/port_rng_golden_test.c")
target_end()


-- ====================
-- Debug-menu action-layer regression test (guards per-item toggle exclusivity
-- + flag/dungeon/stat/bottle helpers; see port_debug_actions_test.c)
-- ====================
target("debug_actions_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT")
    add_files("port/port_debug_actions.c")
    add_files("port/port_debug_actions_test.c")
target_end()


-- ====================
-- GBA/native address-resolution regression test.
-- ====================
target("gba_mem_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT")
    add_files("port/port_gba_mem.c")
    add_files("port/port_gba_mem_test.c")
target_end()


-- ====================
-- Transition preserved-coordinate regression test.
-- ====================
target("transition_axis_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_files("port/port_transition_axis_test.c")
target_end()


-- ====================
-- Deepwood rolling-barrel doorway HDMA lifecycle regression test.
-- ====================
target("rolling_barrel_transition_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_includedirs("build/USA")
    add_defines("PC_PORT", "USA", "ENGLISH", "PORT_ROLLING_BARREL_TRANSITION_TEST")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_rolling_barrel_transition_test.c")
    add_files("port/port_hdma.c")
    add_files("src/common.c")
    add_files("src/manager/rollingBarrelManager.c")
target_end()


-- ====================
-- Multi-region language-slot regression test.
-- ====================
target("region_language_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("port/port_region_language_test.c")
target_end()


-- ====================
-- Multi-region fixed-UI and collision-pointer regression test.
-- ====================
target("region_runtime_data_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("port/port_region_runtime_data_test.c")
    add_files("port/port_offset_remap.c")
target_end()


-- ====================
-- Retail Object70 doorway/swamp head-overlay priority regression test.
-- ====================
target("object70_overlay_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_object70_overlay_test.c")
    add_files("src/object/object70.c")
target_end()


-- ====================
-- EU sprite-table hole + byte-exact production OAM regression test.
-- ====================
target("sprite_region_oam_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_includedirs("port/ppu/include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_sprite_region_oam_test.c")
    add_files("port/port_draw.c")
    add_files("src/enemyUtils.c")
    add_files("src/projectileUtils.c")
target_end()


-- ====================
-- ROM-backed raw gfx-group EWRAM alias regression test.
-- ====================
target("gfx_group_dma_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("port")
    add_includedirs("include")
    add_files("port/port_gfx_group_dma_test.c")
target_end()


-- ====================
-- Hidden-HUD second-screen charge-meter regression test.
-- ====================
target("second_screen_charge_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_packages("libsdl3")
    add_defines("PORT_SECOND_SCREEN_TEST")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_second_screen_charge_test.c")
    add_files("port/port_second_screen.c")
target_end()


-- ====================
-- Bottom-screen Kinstone OBJ-footprint regression test (NEW-4d).
-- ====================
target("second_screen_kinstone_layout_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH", "PORT_SECOND_SCREEN_QUEST_TEST")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_second_screen_kinstone_layout_test.c")
    add_files("port/port_second_screen_quest.c")
target_end()


-- ====================
-- Exact Kinstone report / E1 EU fuser-migration regression fixtures.
-- ====================
target("kinstone_report_fixtures_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("port/port_kinstone_report_fixtures_test.c")
target_end()


-- ====================
-- Enter-room banner widescreen lifecycle regression test.
-- ====================
target("widescreen_banner_state_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("port")
    add_files("port/port_widescreen_banner_state_test.c")
target_end()


-- ====================
-- Pullable-mushroom transactional allocation regression test.
-- ====================
target("pullable_mushroom_allocation_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/pullable_mushroom_allocation_test.c")
    add_files("src/object/pullableMushroom.c")
target_end()


-- ====================
-- Transition-time enemy-target null-safety regression test.
-- ====================
target("enemy_target_null_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_enemy_target_null_test.c")
    add_files("src/enemyUpdate.c")
target_end()


-- ====================
-- Delayed scripted-entity transactional spawn regression test.
-- ====================
target("delayed_entity_script_slot_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_delayed_entity_script_slot_test.c")
    add_files("src/manager/delayedEntityLoadManager.c")
target_end()


-- ====================
-- Semantic flag remap fail-closed runtime regression test.
-- ====================
target("flag_runtime_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("src/flags.c")
    add_files("port/flag_remap_generated.c")
    add_files("port/port_flag_runtime_test.c")
target_end()


-- ====================
-- Compiled-USA Dialog local-flag provenance regression test.
-- ====================
target("dialog_region_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("src/npcUtils.c")
    add_files("port/port_dialog_region_test.c")
target_end()


-- ====================
-- Compiled-USA -> region-native entity/tile data regression test.
-- ====================
target("region_data_resolver_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("port/port_region_data_test.c")
    add_files("port/port_region_data.c")
target_end()


-- ====================
-- Cloud Tops EU/JP fight flag parity regression test.
-- ====================
target("cloud_tops_fight_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if has_config("pc_sanitize") then
        add_cflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all")
        add_ldflags("-fsanitize=address,undefined", {force = true})
    end
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_cloud_tops_fight_test.c")
    add_files("port/port_cloud_tops_fight.c")
target_end()


-- ====================
-- USA/EU Vaati progression invariant and one-shot repair regression test.
-- ====================
target("vaati_progress_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "USA", "ENGLISH")
    add_files("port/port_vaati_progress_test.c")
    add_files("port/port_vaati_progress.c")
target_end()


-- ====================
-- Spiked-roller signed-range trajectory regression test.
-- ====================
target("spiked_rollers_motion_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_spiked_rollers_motion_test.c")
    add_files("src/projectile/spikedRollers.c")
target_end()


-- ====================
-- Gleerok affine-flame geometry and last-damage quick-dump diagnostics.
-- ====================
target("gleerok_fire_hitbox_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_gleerok_fire_hitbox_test.c")
    add_files("src/object/gleerokParticle.c")
    add_files("src/collision.c")
target_end()


-- ====================
-- Bow Moblin arrow direction table and exact retail collision boundaries.
-- ====================
target("arrow_projectile_hitbox_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_arrow_projectile_hitbox_test.c")
    add_files("src/projectile/arrowProjectile.c")
    add_files("src/playerHitbox.c")
    add_files("src/collision.c")
target_end()


-- ====================
-- Canonical 0x500-byte EEPROM save-record layout regression test.
-- ====================
target("save_layout_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_files("port/port_save_layout_test.c")
target_end()


-- ====================
-- Fail-closed raw EEPROM persistence and interrupted-write recovery test.
-- ====================
target("save_persistence_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "PORT_SAVE_TEST", "USA", "ENGLISH")
    add_files("port/port_save.c")
    add_files("port/port_save_persistence_test.c")
target_end()


-- ====================
-- End-to-end E1 raw-profile migration through production ReadSaveFile.
-- ====================
target("save_migration_integration_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "PORT_SAVE_TEST", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if has_config("pc_sanitize") then
        add_cflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all")
        add_ldflags("-fsanitize=address,undefined", {force = true})
    end
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_save.c")
    add_files("src/save.c")
    add_files("port/port_save_migration_integration_test.c")
target_end()


-- ====================
-- End-to-end E1 EU Kinstone repair through production GetFusionToOffer.
-- ====================
target("kinstone_integration_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_includedirs("build/USA")
    add_defines("PC_PORT", "MULTI_REGION", "TMC_3DS", "PORT_SAVE_TEST", "USA", "ENGLISH")
    add_cflags("-ffunction-sections", "-fdata-sections")
    if has_config("pc_sanitize") then
        add_cflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all")
        add_ldflags("-fsanitize=address,undefined", {force = true})
    end
    if is_plat("macosx") then
        add_ldflags("-Wl,-dead_strip")
    else
        add_ldflags("-Wl,--gc-sections")
    end
    add_files("port/port_save.c")
    add_files("src/common.c")
    add_files("port/port_kinstone_integration_test.c")
target_end()


-- ====================
-- Story invariant, regional fusion-marker, and bounded fuser-data test.
-- ====================
target("save_story_region_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "MULTI_REGION", "USA", "ENGLISH")
    add_files("port/flag_remap_generated.c")
    add_files("port/port_save_story_region_test.c")
target_end()


-- ====================
-- Retail tile-interaction table regression test.
-- ====================
target("tile_interaction_data_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "USA", "ENGLISH")
    add_files("port/port_tile_interaction_data_test.c")
    add_files("src/data/data_080046A4.c")
target_end()


-- ====================
-- Multi-region Castle Maid script-callback regression test.
-- ====================
target("castle_maid_dialog_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("port")
    add_includedirs("include")
    add_files("port/port_castle_maid_dialog_test.c")
target_end()


-- ====================
-- 3DS ARM11 host-pointer regression test. This builds the TMC_3DS branch on
-- the desktop with a mocked memory-map query.
-- ====================
target("host_pointer_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT", "TMC_3DS")
    add_files("port/port_gba_mem.c")
    add_files("port/port_host_pointer_3ds_test.c")
target_end()


-- ====================
-- 3DS title/file-select bottom-screen procedural render regression test.
-- ====================
target("bottom_idle_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/source/bottom_idle_3ds.c")
    add_files("platform/3ds/tests/bottom_idle_3ds_test.c")
target_end()


-- ====================
-- 3DS bottom-screen paint/submit/visibility generation regression test.
-- ====================
target("bottom_frame_state_3ds_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/source/bottom_frame_state_3ds.c")
    add_files("platform/3ds/tests/bottom_frame_state_3ds_test.c")
target_end()


-- ====================
-- Old 3DS 60 Hz logic-target / adaptive presentation pacing regression test.
-- ====================
target("old3ds_frame_pacer_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("platform/3ds/source")
    add_files("platform/3ds/source/old3ds_frame_pacer.c")
    add_files("platform/3ds/tests/old3ds_frame_pacer_test.c")
target_end()


-- ====================
-- Deterministic nearest-resampler PCM parity/CRC and microbenchmark. This
-- guards the allocation-free callback path used by the 3DS audio mixer.
-- ====================
target("audio_resampler_test")
    set_kind("binary")
    set_languages("cxx20")
    set_targetdir("build/pc")
    add_includedirs("libs/agbplay_core")
    add_includedirs("port")
    add_files("libs/agbplay_core/Resampler.cpp")
    add_files("libs/agbplay_core/tests/audio_resampler_test.cpp")
    add_syslinks("m")
target_end()

target("audio_mix_bench")
    set_kind("binary")
    set_languages("cxx20")
    set_targetdir("build/pc")
    add_includedirs("libs/agbplay_core")
    add_includedirs("port")
    add_defines("TMC_3DS")
    add_files("libs/agbplay_core/*.cpp")
    add_files("tools/audio_mix_bench.cpp")
    add_syslinks("m")
target_end()


-- ====================
-- Memory-watch list regression test (fault-safe little-endian reads + watch
-- bookkeeping; see port_memory_watch_test.c)
-- ====================
target("memory_watch_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs(".")
    add_includedirs("port")
    add_includedirs("include")
    add_defines("PC_PORT")
    add_files("port/port_debug_memory_watch.c")
    add_files("port/port_memory_watch_test.c")
target_end()


-- ====================
-- GPU-vs-CPU PPU parity harness (docs/gpu-rasterizer-design.md). Renders
-- synthetic GBA memory states through the CPU software rasterizer and the
-- GPU rasterizer and diffs them pixel-for-pixel. Headless SDL_GPU on the
-- `offscreen` video driver. The PPU .c files build as C (matching the game)
-- and link against the C++ GPU raster module.
-- ====================
target("ppu_gpu_parity")
    set_kind("binary")
    set_languages("c11", "cxx17")
    set_targetdir("build/pc")
    add_includedirs("port")
    add_includedirs("port/ppu/include")
    add_files("tools/ppu_gpu_parity.cpp")
    add_files("port/port_gpu_raster.cpp")
    add_files("port/port_gpu_obj_cull.c")
    add_files("port/ppu/src/virtuappu.c")
    add_files("port/ppu/src/mode1.c")
    add_files("port/port_gpu_raster_gl.cpp")
    add_packages("libsdl3")
    add_syslinks("EGL", "GLESv2")
    if is_plat("android") then
        add_syslinks("GLESv3") -- see the tmc_pc target: GLES 3.1 lives there on the NDK
    end
    add_mingw_static_cpp_runtime()
target_end()

target("mode1_native_fast_path_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("port/ppu/include")
    add_defines("VIRTUAPPU_TESTING", "MODE1_GBA_WIDTH=266")
    add_files("port/ppu/tests/mode1_native_fast_path_test.c")
    add_files("port/ppu/src/mode1.c")
    add_syslinks("m")
target_end()

target("mode1_full_view_fast_path_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("port/ppu/include")
    add_defines("VIRTUAPPU_TESTING", "MODE1_GBA_WIDTH=400", "MODE1_GBA_HEIGHT=240")
    add_files("port/ppu/tests/mode1_native_fast_path_test.c")
    add_files("port/ppu/src/mode1.c")
    add_syslinks("m")
target_end()

target("mode1_draw_order_profile_test")
    set_kind("binary")
    set_languages("c11")
    set_targetdir("build/pc")
    add_includedirs("port/ppu/include")
    add_defines("VIRTUAPPU_TESTING", "MODE1_GBA_WIDTH=266")
    add_files("port/ppu/tests/mode1_draw_order_profile_test.c")
    add_files("port/ppu/src/mode1.c")
    add_syslinks("m")
target_end()


-- ====================
-- ROM Build Task
-- ====================
task("rom")
    set_category("plugin")
    on_run(function ()
        import("core.project.config")
        import("lib.detect.find_program")
        import("async.runjobs")
        config.load()
        
        -- Number of parallel jobs (default: number of CPU cores)
        local njobs = tonumber(os.getenv("XMAKE_JOBS")) or os.cpuinfo().ncpu or 8
        
        local game_version = get_config("game_version") or "USA"
        local build_dir = "build/" .. game_version
        local assets_dir = build_dir .. "/assets"
        
        -- Game version configurations
        local versions = {
            USA = { code = "BZME", name = "tmc", language = "ENGLISH" },
            EU = { code = "BZMP", name = "tmc_eu", language = "ENGLISH" },
            JP = { code = "BZMJ", name = "tmc_jp", language = "JAPANESE" },
            DEMO_USA = { code = "BZHE", name = "tmc_demo_usa", language = "ENGLISH" },
            DEMO_JP = { code = "BZMJ", name = "tmc_demo_jp", language = "JAPANESE" }
        }
        
        local ver = versions[game_version]
        if not ver then
            print("Error: Unknown game version: " .. game_version)
            return
        end
        
        local rom_name = ver.name .. ".gba"
        local elf_name = ver.name .. ".elf"
        
        print("===========================================")
        print("Building ROM for " .. game_version)
        print("ROM: " .. rom_name)
        print("===========================================")
        
        -- Check for arm-none-eabi toolchain
        local arm_gcc = find_program("arm-none-eabi-gcc")
        if not arm_gcc then
            -- Try DevkitARM
            local devkitarm = os.getenv("DEVKITARM")
            if devkitarm then
                arm_gcc = path.join(devkitarm, "bin", "arm-none-eabi-gcc")
            end
        end
        if not arm_gcc then
            -- Try common Windows installation paths
            local common_paths = {
                "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe",
                "C:/Program Files/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe",
                "C:/devkitPro/devkitARM/bin/arm-none-eabi-gcc.exe"
            }
            for _, p in ipairs(common_paths) do
                if os.isfile(p) then
                    arm_gcc = p
                    break
                end
            end
        end
        
        if not arm_gcc or (not os.isfile(arm_gcc .. ".exe") and not os.isfile(arm_gcc)) then
            print("Error: arm-none-eabi-gcc not found!")
            print("Please install DevkitARM or arm-none-eabi toolchain")
            return
        end
        
        local toolchain_dir = path.directory(arm_gcc)
        local as_cmd = path.join(toolchain_dir, "arm-none-eabi-as")
        local ld_cmd = path.join(toolchain_dir, "arm-none-eabi-ld")
        local objcopy_cmd = path.join(toolchain_dir, "arm-none-eabi-objcopy")
        local cpp_cmd = arm_gcc
        
        -- Check agbcc
        local agbcc = "tools/agbcc/bin/agbcc"
        if is_host("windows") then
            agbcc = agbcc .. ".exe"
        end
        if not os.isfile(agbcc) then
            print("Error: agbcc not found at " .. agbcc)
            return
        end
        
        print("[1/9] Building tools...")
        os.exec("xmake build tools")
        
        print("[2/9] Checking assets...")
        if not os.isdir(assets_dir) then
            print("  Assets not found, extracting...")
            os.exec("xmake extract_assets")
        end
        
        -- Create build directories
        os.mkdir(build_dir .. "/src")
        os.mkdir(build_dir .. "/src/gba")
        os.mkdir(build_dir .. "/asm")
        os.mkdir(build_dir .. "/asm/lib/src")
        os.mkdir(build_dir .. "/data")
        os.mkdir(build_dir .. "/enum_include")
        
        -- Common flags
        local asinclude = "-I " .. assets_dir .. " -I " .. build_dir .. "/enum_include"
        local asflags = "-mcpu=arm7tdmi --defsym " .. game_version .. "=1 --defsym REVISION=0 --defsym " .. ver.language .. "=1 " .. asinclude
        -- `-I port` lets the GBA ROM build resolve the self-PC_PORT-guarded
        local cinclude = "-I include -I " .. build_dir
        local cppflags = "-I tools/agbcc -I tools/agbcc/include " .. cinclude .. " -nostdinc -undef -D" .. game_version .. " -DREVISION=0 -D" .. ver.language
        
        -- Interwork files need special flags
        local interwork_files = {
            "src/interrupts.c", "src/collision.c", "src/playerItem.c",
            "src/object.c", "src/manager.c", "src/npc.c", "src/gba/m4a.c"
        }
        
        print("[3/9] Generating enum includes...")
        -- Generate enum includes from headers using gcc (not arm-none-eabi-gcc)
        local gcc_cmd = "gcc"
        local headers = os.files("include/*.h")
        local enum_count = 0
        for _, header in ipairs(headers) do
            local filename = path.filename(header)
            local inc_file = build_dir .. "/enum_include/" .. filename:gsub("%.h$", ".inc")
            -- Use os.iorunv to capture output and write to file
            local output, err = os.iorunv("python", {
                "tools/extract_include_enum.py",
                header:gsub("\\", "/"),  -- Use forward slashes for gcc
                gcc_cmd,
                "-D__attribute__(x)=",
                "-D" .. game_version,
                "-E",
                "-nostdinc",
                "-Itools/agbcc",
                "-Itools/agbcc/include",
                "-iquote",
                "include"
            })
            if output and #output > 0 then
                -- Write with ASCII encoding (no BOM) to avoid assembler issues
                io.writefile(inc_file, output)
                enum_count = enum_count + 1
            end
        end
        print("  Generated " .. enum_count .. " enum include files")
        
        print("[4/9] Compiling translations...")
        -- Compile translation JSON files to binary
        local translations = {"English", "French", "German", "Spanish", "Italian"}
        for _, lang in ipairs(translations) do
            local json_file = "translations/" .. lang .. ".json"
            local bin_file = "translations/" .. lang .. ".bin"
            if os.isfile(json_file) and not os.isfile(bin_file) then
                os.execv("tools/bin/tmc_strings", {"-p", "--source", json_file, "--dest", bin_file})
                print("  Compiled: " .. lang .. ".bin")
            end
        end
        
        print("[5/9] Preprocessing linker script...")
        -- Preprocess linker script using os.execv to handle paths with spaces
        os.execv(cpp_cmd, {
            "-I", "tools/agbcc",
            "-I", "tools/agbcc/include",
            "-I", "include",
            "-I", build_dir,
            "-nostdinc",
            "-undef",
            "-D" .. game_version,
            "-DREVISION=0",
            "-D" .. ver.language,
            "-E",
            "-x", "c",
            "linker.ld",
            "-o", build_dir .. "/linker_pp.ld"
        })
        -- Remove preprocessor lines
        local linker_content = io.readfile(build_dir .. "/linker_pp.ld")
        linker_content = linker_content:gsub("#[^\n]*\n", "")
        io.writefile(build_dir .. "/linker.ld", linker_content)
        
        print("[6/9] Checking libc.a...")
        -- Ensure libc.a exists (needed for linking)
        local libc_path = "tools/agbcc/lib/libc.a"
        if not os.isfile(libc_path) then
            print("  Building minimal libc.a...")
            os.mkdir("tools/agbcc/lib")
            local old_agbcc = "tools/agbcc/bin/old_agbcc"
            if is_host("windows") then
                old_agbcc = old_agbcc .. ".exe"
            end
            if os.isfile(old_agbcc) then
                -- Compile memcpy.c from agbcc libc
                local memcpy_src = "tools/agbcc/libc/string/memcpy.c"
                if os.isfile(memcpy_src) then
                    os.execv(old_agbcc, {"-O2", "-o", "tools/agbcc/lib/memcpy.s", memcpy_src})
                    os.execv(as_cmd, {"-mcpu=arm7tdmi", "-o", "tools/agbcc/lib/memcpy.o", "tools/agbcc/lib/memcpy.s"})
                    local ar_cmd = path.join(toolchain_dir, "arm-none-eabi-ar")
                    os.execv(ar_cmd, {"rcs", libc_path, "tools/agbcc/lib/memcpy.o"})
                    print("  Created libc.a with memcpy")
                end
            else
                print("  Warning: old_agbcc not found, cannot build libc.a")
            end
        end
        
        print("[7/9] Compiling C files (" .. njobs .. " jobs)...")
        -- Compile C files in parallel
        local c_files = os.files("src/**.c")
        local obj_files = {}
        
        -- Create all directories first
        for _, cfile in ipairs(c_files) do
            local rel_path = path.relative(cfile, ".")
            local obj_path = build_dir .. "/" .. rel_path:gsub("%.c$", ".o")
            os.mkdir(path.directory(obj_path))
            table.insert(obj_files, obj_path)
        end
        
        -- Compile in parallel
        local compiled_count = 0
        runjobs("compile_c", function (index)
            local cfile = c_files[index]
            local rel_path = path.relative(cfile, ".")
            local obj_path = build_dir .. "/" .. rel_path:gsub("%.c$", ".o")
            local i_path = build_dir .. "/" .. rel_path:gsub("%.c$", ".i")
            local s_path = build_dir .. "/" .. rel_path:gsub("%.c$", ".s")
            
            -- Check if interwork
            local extra_cflags = {"-O2", "-Wimplicit", "-Wparentheses", "-Werror", "-Wno-multichar", "-g3"}
            for _, iw in ipairs(interwork_files) do
                if rel_path:gsub("\\", "/") == iw then
                    table.insert(extra_cflags, "-mthumb-interwork")
                    break
                end
            end
            if rel_path:find("eeprom%.c") then
                extra_cflags = {"-O1", "-Wimplicit", "-Wparentheses", "-Werror", "-Wno-multichar", "-g3", "-mthumb-interwork"}
            end
            
            -- Preprocess
            os.execv(cpp_cmd, {
                "-I", "tools/agbcc",
                "-I", "tools/agbcc/include",
                "-I", "include",
                "-I", build_dir,
                -- Lets the GBA ROM build resolve the self-PC_PORT-guarded port
                -- headers that committed src/ files #include (e.g. port_scripts.h).
                -- PC_PORT is undefined here, so those headers expand to nothing —
                -- this only fixes header resolution, pulling in no port/PC/SDL code.
                "-I", "port",
                "-nostdinc",
                "-undef",
                "-D" .. game_version,
                "-DREVISION=0",
                "-D" .. ver.language,
                "-E",
                cfile,
                "-o", i_path
            })
            
            -- Compile with agbcc
            local agbcc_args = {}
            for _, flag in ipairs(extra_cflags) do
                table.insert(agbcc_args, flag)
            end
            table.insert(agbcc_args, "-o")
            table.insert(agbcc_args, s_path)
            table.insert(agbcc_args, i_path)
            os.execv("tools/agbcc/bin/agbcc", agbcc_args)
            
            -- Append alignment
            local f = io.open(s_path, "a")
            f:write("\t.text\n\t.align\t2, 0 @ Don't pad with nop\n")
            f:close()
            
            -- Assemble
            os.execv(as_cmd, {
                "-mcpu=arm7tdmi",
                "--defsym", game_version .. "=1",
                "--defsym", "REVISION=0",
                "--defsym", ver.language .. "=1",
                "-I", assets_dir,
                "-I", build_dir .. "/enum_include",
                "-o", obj_path,
                s_path
            })
        end, {total = #c_files, comax = njobs})
        print("  Compiled " .. #c_files .. " C files")
        
        print("[8/9] Assembling ASM files (" .. njobs .. " jobs)...")
        -- Assemble ASM files in parallel
        local asm_files = os.files("asm/**.s")
        table.join2(asm_files, os.files("data/**.s"))
        
        -- Create all directories first
        for _, asmfile in ipairs(asm_files) do
            local rel_path = path.relative(asmfile, ".")
            local obj_path = build_dir .. "/" .. rel_path:gsub("%.s$", ".o")
            os.mkdir(path.directory(obj_path))
            table.insert(obj_files, obj_path)
        end
        
        -- Assemble in parallel
        runjobs("assemble_asm", function (index)
            local asmfile = asm_files[index]
            local rel_path = path.relative(asmfile, ".")
            local obj_path = build_dir .. "/" .. rel_path:gsub("%.s$", ".o")
            
            -- Preprocess and assemble
            local asm_output = os.iorunv("tools/bin/preproc", {
                ver.name,
                asmfile,
                "--",
                "-I", assets_dir,
                "-I", build_dir .. "/enum_include"
            })
            
            -- Write preprocessed output and assemble
            local pp_path = obj_path:gsub("%.o$", ".pp.s")
            -- Convert CRLF to LF for assembler compatibility
            if asm_output then
                asm_output = asm_output:gsub("\r\n", "\n")
            end
            io.writefile(pp_path, asm_output)
            
            os.execv(as_cmd, {
                "-mcpu=arm7tdmi",
                "--defsym", game_version .. "=1",
                "--defsym", "REVISION=0",
                "--defsym", ver.language .. "=1",
                "-I", assets_dir,
                "-I", build_dir .. "/enum_include",
                "-o", obj_path,
                pp_path
            })
        end, {total = #asm_files, comax = njobs})
        print("  Assembled " .. #asm_files .. " ASM files")
        
        print("[9/9] Linking...")
        -- Link - need to run from build dir for relative paths in linker script
        local old_dir = os.cd(build_dir)
        os.execv(ld_cmd, {
            "-Map", ver.name .. ".map",
            "-n",
            "-T", "linker.ld",
            "-o", "../../" .. elf_name,
            "-L", "../../tools/agbcc/lib",
            "-lc"
        })
        os.cd(old_dir)
        
        -- Convert to GBA ROM binary
        os.execv(objcopy_cmd, {
            "-O", "binary",
            "--gap-fill", "0xFF",
            "--pad-to", "0x9000000",
            elf_name,
            rom_name
        })
        
        -- Fix ROM header
        os.execv("tools/bin/gbafix", {
            rom_name,
            "-tGBAZELDA MC",
            "-c" .. ver.code,
            "-m01",
            "-r0",
            "--silent"
        })
        
        print("===========================================")
        print("ROM built successfully: " .. rom_name)
        print("===========================================")
    end)
    set_menu {
        usage = "xmake rom [options]",
        description = "Build the GBA ROM",
        options = {}
    }
task_end()
