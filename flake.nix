{
  description = "DevShell tmc";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";

  outputs = { self, nixpkgs, ... }:
  let
    system = "x86_64-linux";
    pkgs = import nixpkgs {
      inherit system;
      config.allowUnfree = true;
    };

    python = pkgs.python3.withPackages (ps: [
      ps.pycparser
    ]);

    commonInputs = with pkgs; [
      xmake
      cmake
      ninja
      pkg-config
      git
      curl
      gnumake
      gcc
      python
      sdl3
      libpng
      zlib
      fmt
      nlohmann_json
      xorg.libX11
      xorg.libXcursor
      xorg.libXext
      xorg.libXfixes
      xorg.libXi
      xorg.libXinerama
      xorg.libXrandr
      xorg.libXrender
      xorg.libXScrnSaver
      xorg.libXau
      xorg.libXdmcp
      xorg.libxcb
      xorg.xorgproto
      libxkbcommon
      wayland
      wayland-protocols
      wayland-utils
    ];

    armInputs = [
      pkgs.pkgsCross.arm-embedded.stdenv.cc
    ];

    runtimeLibPath = pkgs.lib.makeLibraryPath commonInputs;

    launcher = pkgs.writeShellApplication {
      name = "tmc-launch";
      runtimeInputs = commonInputs;
      text = ''
        set -euo pipefail

        root="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
        cd "$root"

        if [ ! -f "$root/xmake.lua" ]; then
          echo "Run this command from inside the tmc repository." >&2
          exit 1
        fi

        if [ ! -f "$root/baserom.gba" ]; then
          echo "Missing baserom.gba in the repository root." >&2
          exit 1
        fi

        game_version="''${GAME_VERSION:-USA}"
        case "$game_version" in
          USA|EU) ;;
          *)
            echo "GAME_VERSION must be USA or EU for the PC port." >&2
            exit 1
            ;;
        esac

        export XMAKE_ROOT=y
        export XMAKE_USE_SYSTEM_SDL3=1
        export LD_LIBRARY_PATH="${runtimeLibPath}''${LD_LIBRARY_PATH:+:''${LD_LIBRARY_PATH}}"

        xmake f -y --game_version="$game_version"
        xmake build_assets
        xmake build -y
        exec "$root/build/pc/tmc_pc" "$@"
      '';
    };
  in {
    devShells.${system}.default = pkgs.mkShell {
      buildInputs = commonInputs ++ armInputs;
      shellHook = ''
        export XMAKE_ROOT=y
        export XMAKE_USE_SYSTEM_SDL3=1
        export LD_LIBRARY_PATH="${runtimeLibPath}''${LD_LIBRARY_PATH:+:''${LD_LIBRARY_PATH}}"
      '';
    };

    packages.${system}.default = launcher;

    apps.${system}.default = {
      type = "app";
      program = "${launcher}/bin/tmc-launch";
    };
  };
}
