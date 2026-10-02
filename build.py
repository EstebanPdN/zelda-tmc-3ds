#!/usr/bin/env python3
"""
TMC PC Port — interactive build script
Run from repository root: python3 build.py
"""

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

# ── Constants ─────────────────────────────────────────────────────────────────

REPO_ROOT = Path(__file__).resolve().parent
PLATFORM  = platform.system()   # Linux | Windows | Darwin
EXE_NAME  = "tmc_pc.exe" if PLATFORM == "Windows" else "tmc_pc"

def _read_sha1_file(filename: str) -> Optional[str]:
    p = REPO_ROOT / filename
    if p.exists():
        return p.read_text().split()[0]
    return None

VERSIONS = {
    "USA": {
        "rom_filename": "baserom.gba",
        "sha1":         _read_sha1_file("tmc.sha1"),
        "sha256":       _read_sha1_file("tmc.sha256"),
        "game_version": "USA",
    },
    "EU": {
        "rom_filename": "baserom_eu.gba",
        "sha1":         _read_sha1_file("tmc_eu.sha1"),
        "sha256":       _read_sha1_file("tmc_eu.sha256"),
        "game_version": "EU",
    },
    # JP is ROM-gated: needs a legal BZMJ baserom (tmc_jp.gba, sha1 in
    # tmc_jp.sha1) plus the generated port_offset_JP.h. The build will fail
    # at that missing header until it is generated — see
    # docs/JP_PORT_ENABLEMENT.md.
    "JP": {
        "rom_filename": "baserom_jp.gba",
        "sha1":         _read_sha1_file("tmc_jp.sha1"),
        "sha256":       _read_sha1_file("tmc_jp.sha256"),
        "game_version": "JP",
    },
}

SHA1_TO_VERSION = {
    v["sha1"]: k for k, v in VERSIONS.items() if v["sha1"]
}

# ── UI helpers ────────────────────────────────────────────────────────────────

W = 64
USE_UNICODE_UI = PLATFORM != "Windows"

def _ui_char(unicode_ch: str, ascii_ch: str) -> str:
    return unicode_ch if USE_UNICODE_UI else ascii_ch

def hr(ch=None):
    ch = ch or _ui_char("─", "-")
    print(ch * W)
def blank():       print()
def header(t):     hr(_ui_char("═", "=")); print(f"  {t}"); hr(_ui_char("═", "="))
def section(t):    blank(); hr(); print(f"  {t}"); hr()
def ok(m):         print(f"  \033[32m{_ui_char('✓', 'OK')}\033[0m  {m}")
def warn(m):       print(f"  \033[33m{_ui_char('!', 'WARN')}\033[0m  {m}")
def err(m):        print(f"  \033[31m{_ui_char('✗', 'ERR')}\033[0m  {m}")
def info(m):       print(f"     {m}")

def prompt(msg: str, choices=None) -> str:
    suffix = f" [{'/'.join(choices)}]" if choices else ""
    while True:
        try:
            ans = input(f"  → {msg}{suffix}: ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            blank(); sys.exit(0)
        if not choices or ans in choices:
            return ans
        err(f"Enter one of: {', '.join(choices)}")

# ── Subprocess ────────────────────────────────────────────────────────────────

def run_cmd(cmd, env=None, cwd=None, check=True) -> subprocess.CompletedProcess:
    display = " ".join(str(c) for c in cmd)
    print(f"\n  \033[90m$ {display}\033[0m")
    result = subprocess.run(
        [str(c) for c in cmd],
        env=env,
        cwd=str(cwd) if cwd else None,
    )
    if check and result.returncode != 0:
        raise RuntimeError(f"Command exited {result.returncode}: {display}")
    return result

# ── File utils ────────────────────────────────────────────────────────────────

def sha1_file(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def dir_populated(d: Path) -> bool:
    try:
        return d.is_dir() and any(True for _ in d.iterdir())
    except OSError:
        return False

# ── Dependency detection ──────────────────────────────────────────────────────

def detect_distro() -> str:
    try:
        with open("/etc/os-release") as f:
            for line in f:
                if line.startswith("ID="):
                    return line.split("=", 1)[1].strip().strip('"').lower()
    except FileNotFoundError:
        pass
    return "unknown"

def pkg_config_ok(name: str) -> bool:
    return subprocess.run(["pkg-config", "--exists", name], capture_output=True).returncode == 0

# (label, check_fn, arch_pkg, apt_pkg)
LINUX_DEPS = [
    ("git",           lambda: bool(shutil.which("git")),        "git",           "git"),
    ("libpng",        lambda: pkg_config_ok("libpng"),          "libpng",        "libpng-dev"),
    ("fmt",           lambda: pkg_config_ok("fmt"),             "fmt",           "libfmt-dev"),
    ("nlohmann-json", lambda: pkg_config_ok("nlohmann_json"),   "nlohmann-json", "nlohmann-json3-dev"),
]

WIN_DEPS = [
    ("xmake", lambda: bool(shutil.which("xmake"))),
    ("git",   lambda: bool(shutil.which("git"))),
]

# Apple Clang has no built-in OpenMP runtime, so VirtuaPPU's parallel
# scanline path needs Homebrew's libomp. xmake.lua falls back to a
# single-threaded build if libomp is missing, but we still surface it
# here so `brew install libomp` is an obvious next step.
def _libomp_present() -> bool:
    if subprocess.run(["brew", "--prefix", "libomp"],
                      capture_output=True).returncode == 0:
        return True
    return any(Path(p).is_dir() for p in (
        "/opt/homebrew/opt/libomp",
        "/usr/local/opt/libomp",
    ))

# (label, check_fn, brew_pkg)
# SDL3 is intentionally omitted: xmake builds a vendored copy on macOS and
# XMAKE_USE_SYSTEM_SDL3 is Linux-only by design.
MAC_DEPS = [
    ("xmake",         lambda: bool(shutil.which("xmake")),      "xmake"),
    ("git",           lambda: bool(shutil.which("git")),        "git"),
    ("pkg-config",    lambda: bool(shutil.which("pkg-config")), "pkg-config"),
    ("libpng",        lambda: pkg_config_ok("libpng"),          "libpng"),
    ("fmt",           lambda: pkg_config_ok("fmt"),             "fmt"),
    ("nlohmann-json", lambda: pkg_config_ok("nlohmann_json"),   "nlohmann-json"),
    ("libomp",        _libomp_present,                          "libomp"),
]

def check_deps(non_interactive: bool = False) -> bool:
    all_ok = True

    if PLATFORM == "Linux":
        distro   = detect_distro()
        is_arch  = distro in ("arch", "cachyos", "manjaro", "endeavouros", "garuda")
        miss_arch, miss_apt = [], []

        for label, check_fn, arch_pkg, apt_pkg in LINUX_DEPS:
            if check_fn():
                ok(label)
            else:
                err(label)
                miss_arch.append(arch_pkg)
                miss_apt.append(apt_pkg)

        if miss_arch:
            blank()
            warn("Missing dependencies. Install with:")
            if is_arch:
                info(f"  sudo pacman -S {' '.join(miss_arch)}")
            else:
                info(f"  sudo apt install {' '.join(miss_apt)}")
            blank()
            if non_interactive:
                all_ok = False
            elif prompt("Attempt automatic install?", ["y", "n"]) == "y":
                cmd = (["sudo", "pacman", "-S", "--noconfirm"] + miss_arch if is_arch
                       else ["sudo", "apt", "install", "-y"] + miss_apt)
                if run_cmd(cmd, check=False).returncode != 0:
                    err("Automatic install failed — install manually and re-run.")
                    return False
                # Re-check after install
                still_missing = [l for l, fn, *_ in LINUX_DEPS if not fn()]
                if still_missing:
                    err(f"Still missing after install: {', '.join(still_missing)}")
                    return False
            else:
                all_ok = False

    elif PLATFORM == "Windows":
        missing = []
        for label, check_fn in WIN_DEPS:
            if check_fn():
                ok(label)
            else:
                err(label); missing.append(label)
        if missing:
            blank()
            warn("Missing: " + ", ".join(missing))
            info("xmake : https://xmake.io")
            info("git   : https://git-scm.com")
            all_ok = False

    elif PLATFORM == "Darwin":
        # Apple Command Line Tools provide clang/ld/git; Homebrew won't install
        # them and xmake can't build C++ without them.
        if shutil.which("clang"):
            ok("Xcode Command Line Tools (clang)")
        else:
            err("Xcode Command Line Tools (clang) not found")
            info("Install with: xcode-select --install")
            all_ok = False

        miss_brew = []
        for label, check_fn, brew_pkg in MAC_DEPS:
            if check_fn():
                ok(label)
            else:
                err(label)
                miss_brew.append(brew_pkg)

        if miss_brew:
            blank()
            warn("Missing dependencies. Install with:")
            if shutil.which("brew"):
                info(f"  brew install {' '.join(miss_brew)}")
            else:
                info("  Install Homebrew first: https://brew.sh")
                info(f"  Then: brew install {' '.join(miss_brew)}")
            blank()
            if non_interactive or not shutil.which("brew"):
                all_ok = False
            elif prompt("Attempt automatic install via brew?", ["y", "n"]) == "y":
                if run_cmd(["brew", "install"] + miss_brew, check=False).returncode != 0:
                    err("Automatic install failed — install manually and re-run.")
                    return False
                still_missing = [l for l, fn, *_ in MAC_DEPS if not fn()]
                if still_missing:
                    err(f"Still missing after install: {', '.join(still_missing)}")
                    return False
            else:
                all_ok = False

    else:
        err(f"Unsupported platform: {PLATFORM}")
        return False

    return all_ok

# ── ROM handling ──────────────────────────────────────────────────────────────

def scan_roms() -> dict:
    """Return {version: Path} for all recognized ROMs found nearby."""
    search_dirs = [REPO_ROOT, REPO_ROOT.parent, Path.home() / "Downloads"]
    found: dict = {}
    for d in search_dirs:
        if not d.is_dir():
            continue
        candidates = sorted(d.glob("*.gba")) + sorted(d.glob("*.GBA"))
        for gba in candidates:
            try:
                digest = sha1_file(gba)
            except OSError:
                continue
            version = SHA1_TO_VERSION.get(digest)
            if version and version not in found:
                found[version] = gba
    return found

def ensure_roms(selected: list, found: dict, non_interactive: bool = False) -> dict:
    """Ensure each selected ROM is at REPO_ROOT/<rom_filename>. Returns {version: bool}."""
    result = {}
    for v in selected:
        meta   = VERSIONS[v]
        target = REPO_ROOT / meta["rom_filename"]
        sha1   = meta["sha1"]
        sha256 = meta.get("sha256")

        if (target.exists() and sha1 and sha1_file(target) == sha1
                and (not sha256 or sha256_file(target) == sha256)):
            ok(f"{v}: {target.name}")
            result[v] = True
            continue

        if target.exists() and not sha1:
            warn(f"{v}: SHA1 unknown (missing {v.lower()}.sha1), using existing file")
            result[v] = True
            continue

        if v in found:
            src = found[v]
            if src.resolve() == target.resolve():
                ok(f"{v}: {target.name}")
                result[v] = True
                continue
            info(f"Copy  {src}")
            info(f"  →   {target}")
            if non_interactive or prompt("Proceed?", ["y", "n"]) == "y":
                shutil.copy2(src, target)
                ok(f"Copied {target.name}")
                result[v] = True
            else:
                result[v] = False
        else:
            err(f"{v} ROM not found: {meta['rom_filename']}")
            if sha1:
                info(f"Expected SHA1: {sha1}")
            info(f"Place ROM at:  {target}")
            result[v] = False

    return result

# ── Build pipeline ────────────────────────────────────────────────────────────

def ensure_sounds_embed() -> None:
    """Generate port/generated_sounds_embed.cpp from assets/sounds.json.

    xmake.lua references the file as a static input via add_files(...),
    which is processed during the configure pass - BEFORE xmake's
    before_build hook runs. On a clean checkout the file doesn't exist
    yet and configure prints

        warning: ./xmake.lua:NNN: cannot match add_files("port/generated_sounds_embed.cpp")

    followed by an undefined-reference link error for
    PortSoundsEmbed::kData / kSize. Running the generator here, before
    invoking xmake, sidesteps the ordering issue regardless of whether
    the user passed --slim. The generator is deterministic and no-ops
    when the on-disk output already matches the input, so this is safe
    to run unconditionally without busting xmake's incremental cache.
    The xmake before_build hook is left in place as a safety net for
    direct `xmake build` invocations.
    """
    script = REPO_ROOT / "tools" / "generate_sounds_embed.py"
    sounds = REPO_ROOT / "assets" / "sounds.json"
    output = REPO_ROOT / "port" / "generated_sounds_embed.cpp"
    if not script.exists():
        warn(f"generator missing: {script.relative_to(REPO_ROOT)} - relying on xmake before_build hook")
        return

    # Prefer python3 (matches xmake.lua), fall back to python (Windows
    # installs without the python3 shim).
    last_err: Optional[str] = None
    for interp in ("python3", "python"):
        if not shutil.which(interp):
            continue
        result = subprocess.run(
            [interp, str(script), str(sounds), str(output)],
            cwd=str(REPO_ROOT),
        )
        if result.returncode == 0:
            ok(f"generated_sounds_embed.cpp ({sounds.name} -> {output.relative_to(REPO_ROOT)})")
            return
        last_err = f"{interp} exited {result.returncode}"

    reason = last_err or "no python interpreter found"
    if output.exists():
        warn(f"sounds embed generator failed ({reason})")
        info(f"Reusing existing {output.relative_to(REPO_ROOT)} - it may be stale vs {sounds.name}.")
    else:
        err(f"sounds embed generator failed ({reason}) and "
            f"{output.relative_to(REPO_ROOT)} does not exist.")
        info(f"Run it manually to see the error: python3 {script.relative_to(REPO_ROOT)} "
             f"{sounds.relative_to(REPO_ROOT)} {output.relative_to(REPO_ROOT)}")
        sys.exit(1)


def make_env() -> dict:
    env = os.environ.copy()
    env["XMAKE_ROOT"] = "y"

    if PLATFORM == "Windows":
        env.setdefault("TMC_XMAKE_PLATFORM", "mingw")
        env.setdefault("TMC_XMAKE_TOOLCHAIN", "mingw")

    if PLATFORM == "Linux" and pkg_config_ok("sdl3"):
        env["XMAKE_USE_SYSTEM_SDL3"] = "1"

    return env

def build_version(version: str, env: dict, non_interactive: bool = False,
                  slim: bool = False, multi_region: bool = True) -> Optional[Path]:
    """Build tmc_pc for `version` and stage it under dist/<version>/.

    `slim=True` produces a minimal dist (just the binary). The
    embedded extractor + embedded sounds.json fallback in tmc_pc
    handle first-launch asset extraction and audio metadata from a
    bare `tmc_pc + baserom.gba` install, so the dist no longer
    needs to ship `assets/` or a separate `sounds.json`. Trade-off:
    first launch takes ~3-5 s with a progress bar instead of being
    instant. After that first run, `assets/` lives next to the
    binary and warm launches are instant either way.
    """
    dist_dir = REPO_ROOT / "dist" / version

    # Skip prompt if dist binary already exists
    dst_bin = dist_dir / EXE_NAME
    if dst_bin.exists() and not non_interactive:
        ans = prompt(f"{version} already built at dist/{version}/{EXE_NAME}. Rebuild?", ["y", "n"])
        if ans == "n":
            return dst_bin

    dist_dir.mkdir(parents=True, exist_ok=True)

    configure_cmd = ["xmake", "f", "-y", f"--game_version={version}"]

    xmake_platform = env.get("TMC_XMAKE_PLATFORM", "").strip()
    if xmake_platform:
        configure_cmd.append(f"--plat={xmake_platform}")

    # Target architecture override. Native builds use the host arch (no
    # flag needed), but cross-style configs must be explicit: the `mingw`
    # platform defaults to x86_64 regardless of host, so the Windows-ARM64
    # CI job sets TMC_XMAKE_ARCH=arm64 to build an aarch64 binary.
    xmake_arch = env.get("TMC_XMAKE_ARCH", "").strip()
    if xmake_arch:
        configure_cmd.append(f"--arch={xmake_arch}")

    toolchain = env.get("TMC_XMAKE_TOOLCHAIN", "").strip()
    if toolchain:
        configure_cmd.append(f"--toolchain={toolchain}")

    # CI on Windows pins the MinGW root via TMC_XMAKE_MINGW. Without
    # this xmake auto-picks Git for Windows's stripped MinGW (no `ar`),
    # libpng install fails with "cannot get program for ar".
    mingw = env.get("TMC_XMAKE_MINGW", "").strip()
    if mingw:
        configure_cmd.append(f"--mingw={mingw}")

    # Release builds always enable the SDL_GPU backend so users can
    # pick GPU from F8 → Renderer and the .glslp shader-preset picker
    # is exposed.  Without --gpu_renderer=y the entire port_gpu_renderer.cpp
    # body is stubbed by `#ifndef TMC_GPU_RENDERER`, Port_GPU_IsActive
    # returns false unconditionally, and the shader preset entry shows
    # "(GPU backend required)" even when the user has GPU selected.
    # Default-on costs ~150 KB of embedded SPIR-V; no runtime impact
    # for users who stay on the software backend.
    configure_cmd.append("--gpu_renderer=y")
    # Enable multi-region single-binary support (the shipped-release default).
    # --single-region disables it to exercise the per-region build path (and
    # the GBA-matching decomp config), which surfaces half-flattened
    # MULTI_REGION guards — an _eu/_jp twin referenced unguarded but defined
    # only under #ifdef MULTI_REGION — that the fat multi-region build hides.
    configure_cmd.append("--multi_region=y" if multi_region else "--multi_region=n")

    # 0.5.0 release cutover: ship the real widescreen build. Direct xmake
    # developer builds still default to 240 unless explicitly configured, but
    # every build.py/CI/release artifact uses the wide viewport.
    configure_cmd.append("--widescreen_width=384")

    assets_dir = REPO_ROOT / "build" / version / "assets"
    assets_src_dir = REPO_ROOT / "build" / version / "assets_src"
    assets_ready = assets_dir.exists() and assets_src_dir.exists()

    steps = [
        (f"Configure ({version})", configure_cmd),
    ]

    # Slim builds skip the legacy xmake extract/convert/build_assets
    # tasks entirely — the dist won't include build/<version>/assets/
    # so there's no point producing it. tmc_pc's embedded extractor
    # handles first-launch extraction from baserom.gba.
    if slim:
        info("Slim build — skipping xmake extract_assets/convert_assets/build_assets.")
    elif assets_ready:
        info("Assets already exist in build/<version>/assets and build/<version>/assets_src — skipping extract/convert/build_assets.")
    else:
        steps.extend([
            ("Extract assets",              ["xmake", "extract_assets"]),
            ("Convert assets",              ["xmake", "convert_assets"]),
            ("Build assets",                ["xmake", "build_assets"]),
        ])

    steps.append((f"Compile tmc_pc ({version})", ["xmake", "build", "-y", "tmc_pc"]))
    steps.append((f"Compile asset_extractor ({version})", ["xmake", "build", "-y", "asset_extractor"]))

    for label, cmd in steps:
        info(f"{label}...")
        try:
            run_cmd(cmd, env=env, cwd=REPO_ROOT)
        except RuntimeError as exc:
            err(str(exc))
            return None

    if not slim:
        # Copy ROM so the standalone asset_extractor can find it next
        # to itself and pre-populate build/pc/assets/ — a convenience
        # for local-dev runs of build/pc/tmc_pc.
        src = Path("baserom.gba").resolve()
        dst = Path("build/pc/baserom.gba")
        if not dst.exists() or dst.resolve() != src:
            shutil.copy2("baserom.gba", "build/pc/baserom.gba")
            print("✓  Copied baserom.gba → build/pc/")
        else:
            print("✓  baserom.gba already in build/pc/ (same file)")

        extractor = REPO_ROOT / "build" / "pc" / (
            "asset_extractor.exe" if PLATFORM == "Windows" else "asset_extractor"
        )
        if extractor.exists():
            info("Extracting runtime assets (pak mode)...")
            try:
                # --pak matches tmc_pc's default (anything other than
                # --loose-assets triggers pak mode), so a subsequent
                # warm-launch of build/pc/tmc_pc skips re-extraction
                # via the ROM-fingerprint fast path.
                run_cmd([extractor, "--pak"], cwd=REPO_ROOT)
            except RuntimeError:
                warn("asset_extractor failed — runtime assets may be incomplete")
        else:
            warn("asset_extractor not built — run: xmake build asset_extractor")

    # ── Copy artefacts to dist/<version>/ ────────────────────────────────────

    src_bin = REPO_ROOT / "build" / "pc" / EXE_NAME
    if not src_bin.exists():
        err(f"Binary not found: {src_bin}")
        return None

    shutil.copy2(src_bin, dst_bin)
    if PLATFORM != "Windows":
        dst_bin.chmod(dst_bin.stat().st_mode | 0o111)
    ok(f"Binary    →  dist/{version}/{EXE_NAME}")

    # Linux: bundle libSDL3 + libgomp so the tarball runs on systems
    # that don't ship SDL3 yet (Steam Deck SteamOS, older Ubuntu/Fedora).
    # The binary is linked with -Wl,-rpath,$ORIGIN so it finds these
    # next to itself before falling back to /usr/lib (xmake.lua).
    # Resolved via ldd against the just-built binary.
    if PLATFORM == "Linux":
        import re
        try:
            ldd_out = subprocess.run(
                ["ldd", str(src_bin)], capture_output=True, text=True, check=True
            ).stdout
        except Exception as e:
            warn(f"ldd failed ({e}); skipping shared-library bundling")
            ldd_out = ""
        bundle_libs = ("libSDL3.so.0", "libgomp.so.1")
        for libname in bundle_libs:
            m = re.search(rf"^\s*{re.escape(libname)}\s*=>\s*(\S+)", ldd_out, re.MULTILINE)
            if not m:
                continue
            src_lib = Path(m.group(1))
            if not src_lib.exists():
                continue
            # cp -aP semantics: preserve the symlink AND copy the versioned
            # target it points to, so `libSDL3.so.0 -> libSDL3.so.0.4.4`
            # works the same way ld.so resolves it at runtime.
            dst_lib = dist_dir / src_lib.name
            if dst_lib.exists() or dst_lib.is_symlink():
                dst_lib.unlink()
            if src_lib.is_symlink():
                shutil.copy(src_lib, dst_lib, follow_symlinks=False)
                resolved = src_lib.resolve()
                if resolved != src_lib and resolved.exists():
                    dst_resolved = dist_dir / resolved.name
                    if not dst_resolved.exists():
                        shutil.copy2(resolved, dst_resolved)
                ok(f"{src_lib.name} (+target) →  dist/{version}/")
            else:
                shutil.copy2(src_lib, dst_lib)
                ok(f"{src_lib.name} →  dist/{version}/")

    if slim:
        # In slim mode the binary is the entire dist. tmc_pc's
        # embedded extractor will create assets/ on first run, and
        # the embedded sounds.json fallback (compiled into the
        # binary by tools/generate_sounds_embed.py) handles audio.
        info("Slim mode — assets/, assets_src/, and sounds.json are NOT copied.")
        info("tmc_pc will self-extract assets on first launch using the embedded extractor.")
        return dst_bin

    # Runtime assets (build/<version>/assets/) and editable assets (build/<version>/assets_src/)
    for src_name in ("assets", "assets_src"):
        src = REPO_ROOT / "build" / version / src_name
        dst = dist_dir / src_name
        if src.exists():
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
            ok(f"{src_name}/  →  dist/{version}/{src_name}/")
        else:
            warn(f"build/{version}/{src_name}/ not found — skipping")

    sounds_src = REPO_ROOT / "assets" / "sounds.json"
    if sounds_src.exists():
        shutil.copy2(sounds_src, dist_dir / "sounds.json")
        ok(f"sounds.json →  dist/{version}/sounds.json")
    else:
        # Not fatal: the binary embeds a build-time copy as a fallback
        # (see tools/generate_sounds_embed.py). The on-disk file just
        # lets users tweak song metadata without rebuilding.
        warn("assets/sounds.json not found — using the embedded fallback baked into tmc_pc")

    return dst_bin

# ── Main ──────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="TMC PC Port build helper")
    parser.add_argument("--usa", action="store_true", help="Build USA version without prompts")
    parser.add_argument("--eur", action="store_true", help="Build EU version without prompts")
    parser.add_argument("--jp", action="store_true",
                        help="Build JP version without prompts (ROM-gated; needs a JP baserom "
                             "and the generated port_offset_JP.h — see docs/JP_PORT_ENABLEMENT.md)")
    parser.add_argument(
        "--slim",
        action="store_true",
        help=(
            "Produce a minimal dist/<version>/ containing only tmc_pc. "
            "Skips the standalone asset_extractor invocation, the "
            "assets/ + assets_src/ copy, and the on-disk sounds.json copy. "
            "tmc_pc self-extracts assets on first launch (3-5 s) and uses "
            "its compiled-in sounds.json fallback for audio metadata."
        ),
    )
    parser.add_argument(
        "--single-region",
        action="store_true",
        help=(
            "Configure with --multi_region=n (a per-region build) instead of "
            "the fat single-binary default. Used as a build guard: surfaces "
            "half-flattened MULTI_REGION guards (an _eu/_jp twin referenced "
            "unguarded but defined only under #ifdef MULTI_REGION) that the "
            "default multi-region build hides."
        ),
    )
    return parser.parse_args()

def main():
    args = parse_args()
    non_interactive = args.usa or args.eur or args.jp
    slim = args.slim
    single_region = args.single_region

    header("TMC PC Port Builder")
    info(f"Platform : {PLATFORM}")
    info(f"Repo root: {REPO_ROOT}")

    section("Dependencies")
    if not check_deps(non_interactive=non_interactive):
        err("Fix missing dependencies and re-run.")
        sys.exit(1)

    section("ROM Detection")
    found = scan_roms()
    for v, path in found.items():
        ok(f"{v}: {path}")
    for v in VERSIONS:
        if v not in found:
            expected = REPO_ROOT / VERSIONS[v]["rom_filename"]
            if expected.exists():
                ok(f"{v}: {expected.name} (already in repo root)")
            else:
                warn(f"{v}: not found")

    keys = list(VERSIONS.keys())
    if non_interactive:
        selected = []
        if args.usa:
            selected.append("USA")
        if args.eur:
            selected.append("EU")
        if args.jp:
            selected.append("JP")
        info("Non-interactive mode enabled via --usa/--eur/--jp")
        info(f"Selected: {', '.join(selected)}")
    else:
        section("Select Version")
        for i, v in enumerate(keys, 1):
            rom_ready = (
                v in found
                or (REPO_ROOT / VERSIONS[v]["rom_filename"]).exists()
            )
            tag = "\033[32mROM ready\033[0m" if rom_ready else "\033[31mROM missing\033[0m"
            print(f"  {i}) {v:<6} [{tag}]")
        print(f"  {len(keys) + 1}) All")
        print(f"  q) Quit")

        valid = [str(i) for i in range(1, len(keys) + 2)] + ["q"]
        choice = prompt("Choice", valid)
        if choice == "q":
            sys.exit(0)

        idx = int(choice)
        selected = keys if idx == len(keys) + 1 else [keys[idx - 1]]

    if slim:
        # Slim builds skip xmake extract_assets/convert_assets/build_assets
        # AND the post-compile asset_extractor invocation, none of which
        # the binary itself needs to compile. So the ROM is optional —
        # if it's missing we can still produce dist/<version>/tmc_pc.
        # CI uses this path: no ROM in the runner, only the binaries.
        section("Preparing ROMs (slim — ROM optional)")
        rom_ok = ensure_roms(selected, found, non_interactive=non_interactive)
        buildable = list(selected)
        for v in selected:
            if not rom_ok.get(v):
                warn(f"{v}: no ROM, slim build will produce binary only")
    else:
        section("Preparing ROMs")
        rom_ok = ensure_roms(selected, found, non_interactive=non_interactive)
        buildable = [v for v in selected if rom_ok.get(v)]
        skipped   = [v for v in selected if not rom_ok.get(v)]

        if skipped:
            warn(f"Skipping (no ROM): {', '.join(skipped)}")
        if not buildable:
            err("Nothing to build.")
            sys.exit(1)

    blank()
    info(f"Will build: {', '.join(buildable)}")
    if not non_interactive and prompt("Start?", ["y", "n"]) == "n":
        sys.exit(0)

    env     = make_env()
    if slim:
        info("Slim mode — dist will contain only tmc_pc (assets self-extract on first run).")

    """Generate port/generated_sounds_embed.cpp before xmake's configure
    pass reads it via add_files. xmake.lua keeps a before_build hook
    as a safety net, but that fires AFTER configure, so a clean
    checkout would otherwise warn + fail to link with undefined
    references to PortSoundsEmbed::kData/kSize."""
    ensure_sounds_embed()

    results = {}
    for v in buildable:
        section(f"Building {v}")
        results[v] = build_version(v, env, non_interactive=non_interactive, slim=slim,
                                   multi_region=not single_region)

    section("Done")
    any_ok = False
    for v, exe in results.items():
        if exe:
            any_ok = True
            rel = exe.parent.relative_to(REPO_ROOT)
            ok(f"{v} binary: {exe}")
            blank()
            info(f"  Run {v}:")
            info(f"    cd {rel}")
            run_cmd = f".\\{EXE_NAME}" if PLATFORM == "Windows" else f"./{EXE_NAME}"
            info(f"    {run_cmd}")
            blank()
        else:
            err(f"{v} — build failed")

    sys.exit(0 if any_ok else 1)

if __name__ == "__main__":
    main()
