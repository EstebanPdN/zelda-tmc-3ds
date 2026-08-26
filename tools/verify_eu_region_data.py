#!/usr/bin/env python3
"""Verify every USA-compiled data replacement against clean USA/EU ROMs.

This is an opt-in developer check because retail ROMs cannot be distributed in
the repository. It accepts explicit paths, or defaults to ../ROMs/american.gba
and ../ROMs/europe.gba relative to the repository.
"""

from __future__ import print_function

import hashlib
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "port" / "port_region_data.c"
PORT_CONFIG = ROOT / "port" / "port_config.h"

EXPECTED_SHA1 = {
    "USA": "b4bd50e4131b027c334547b4524e2dbbd4227130",
    "EU": "cff199b36ff173fb6faf152653d1bccf87c26fb7",
}

# symbol: (USA ROM offset, EU ROM offset, minimum bytes, exact differing bytes)
MAPPINGS = {
    "gUnk_080D9328": (0x0D9328, 0x0D8A84, 0x10, 1),
    "gUnk_080DD750 + 0x40": (0x0DD790, 0x0DCECC, 0x50, 5),
    "gUnk_080DD7E0": (0x0DD7E0, 0x0DCF1C, 0x40, 2),
    "gUnk_080DD840": (0x0DD840, 0x0DCF7C, 0x40, 2),
    "gUnk_080EAE60": (0x0EAE60, 0x0EA53C, 0x50, 8),
    "gUnk_080EB9F4": (0x0EB9F4, 0x0EB0C0, 0x70, 7),
    "gUnk_080F78A0": (0x0F78A0, 0x0F6E5C, 0x20, 1),
    "gUnk_080F9BF8": (0x0F9BF8, 0x0F9144, 0x40, 2),
    "gUnk_080F09A0": (0x0F09A0, 0x0EFFD4, 0x60, 3),
    "gUnk_080FEAC8": (0x0FEAC8, 0x0FE00C, 0x120, 18),
    "gUnk_080FEE58": (0x0FEE58, 0x0FE39C, 0x20, 1),
}

# Exact retail section boundaries recovered from the matching linker maps.
# frame_obj_lists has three trailing 0xFF alignment bytes which are not part of
# the self-relative payload. fixed_type_gfx is a packed u32 pointer table.
FRAME_OBJ_TABLES = {
    "USA": (0x2F3D74, 200045, 0x324AE4),
    "EU": (0x2F3460, 199561, 0x323FEC),
}
FIXED_GFX_TABLES = {
    "USA": (0x132B30, 526, 0x133368),
    "EU": (0x132180, 525, 0x1329B4),
}


def _sha1(data):
    return hashlib.sha1(data).hexdigest()


def _read_clean_rom(path, profile, game_code):
    data = path.read_bytes()
    digest = _sha1(data)
    if digest != EXPECTED_SHA1[profile]:
        raise RuntimeError(
            "%s is not the supported clean %s ROM (SHA-1 %s, expected %s)"
            % (path, profile, digest, EXPECTED_SHA1[profile])
        )
    if data[0xAC:0xB0] != game_code:
        raise RuntimeError("%s has unexpected game code %r" % (path, data[0xAC:0xB0]))
    return data


def _parse_registry():
    text = REGISTRY.read_text(encoding="utf-8")
    matches = re.findall(
        r"\{\s*(gUnk_\w+(?:\s*\+\s*0x[0-9A-Fa-f]+)?),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\s*\}",
        text,
    )
    return {name: (int(offset, 16), int(size, 16)) for name, offset, size in matches}


def _parse_integer_macro(text, name):
    match = re.search(r"^#define\s+%s\s+(\d+)u?$" % re.escape(name), text, re.MULTILINE)
    if match is None:
        raise RuntimeError("missing integer macro %s" % name)
    return int(match.group(1))


def _verify_runtime_table_bounds(roms):
    config = PORT_CONFIG.read_text(encoding="utf-8")
    expected_macros = {
        "PORT_USA_FRAME_OBJ_LISTS_SIZE": FRAME_OBJ_TABLES["USA"][1],
        "PORT_EU_FRAME_OBJ_LISTS_SIZE": FRAME_OBJ_TABLES["EU"][1],
        "PORT_USA_FIXED_TYPE_GFX_COUNT": FIXED_GFX_TABLES["USA"][1],
        "PORT_EU_FIXED_TYPE_GFX_COUNT": FIXED_GFX_TABLES["EU"][1],
    }
    for name, expected in expected_macros.items():
        actual = _parse_integer_macro(config, name)
        if actual != expected:
            raise RuntimeError("%s is %d, expected %d" % (name, actual, expected))

    for profile, (start, payload_size, next_section) in FRAME_OBJ_TABLES.items():
        section_size = next_section - start
        if section_size != payload_size + 3:
            raise RuntimeError("%s frame-object section boundary is inconsistent" % profile)
        first_frame_offset = int.from_bytes(roms[profile][start : start + 4], "little")
        expected_top_level_bytes = (
            _parse_integer_macro(config, "PORT_%s_FRAME_OBJ_COUNT" % profile) * 4
        )
        if first_frame_offset != expected_top_level_bytes:
            raise RuntimeError(
                "%s frame-object top-level count is inconsistent with its first offset" % profile
            )
        alignment = roms[profile][start + payload_size : next_section]
        if alignment != b"\xFF\xFF\xFF":
            raise RuntimeError("%s frame-object alignment fingerprint changed" % profile)
        print(
            "PASS %-18s payload %d bytes + 3-byte alignment; next table at 0x%06X"
            % (profile + " frame objects", payload_size, next_section)
        )

    for profile, (start, count, next_section) in FIXED_GFX_TABLES.items():
        if next_section - start != count * 4:
            raise RuntimeError("%s fixed-gfx table boundary is inconsistent" % profile)
        print(
            "PASS %-18s %d packed entries; next table at 0x%06X"
            % (profile + " fixed gfx", count, next_section)
        )


def main(argv):
    usa_path = Path(argv[1]) if len(argv) > 1 else ROOT.parent / "ROMs" / "american.gba"
    eu_path = Path(argv[2]) if len(argv) > 2 else ROOT.parent / "ROMs" / "europe.gba"
    if len(argv) > 3:
        raise SystemExit("usage: verify_eu_region_data.py [USA_ROM [EU_ROM]]")

    usa = _read_clean_rom(usa_path, "USA", b"BZME")
    eu = _read_clean_rom(eu_path, "EU", b"BZMP")
    roms = {"USA": usa, "EU": eu}
    registry = _parse_registry()

    _verify_runtime_table_bounds(roms)

    for symbol, (usa_offset, eu_offset, size, expected_diff) in MAPPINGS.items():
        if registry.get(symbol) != (eu_offset, size):
            raise RuntimeError(
                "%s registry mismatch: got %r, expected EU offset/size (0x%X, 0x%X)"
                % (symbol, registry.get(symbol), eu_offset, size)
            )
        usa_slice = usa[usa_offset : usa_offset + size]
        eu_slice = eu[eu_offset : eu_offset + size]
        if len(usa_slice) != size or len(eu_slice) != size:
            raise RuntimeError("%s slice is truncated" % symbol)
        differing = sum(a != b for a, b in zip(usa_slice, eu_slice))
        if differing != expected_diff:
            raise RuntimeError(
                "%s regional fingerprint changed: %d differing bytes, expected %d"
                % (symbol, differing, expected_diff)
            )
        print("PASS %-18s USA 0x%06X -> EU 0x%06X; %d/%d bytes differ" %
              (symbol, usa_offset, eu_offset, differing, size))

    # Explicit semantic fingerprints for the fields behind Report 1 and the
    # other directly consumed lists. These are more informative than a whole-
    # slice checksum if a future edit changes one flag-bearing byte.
    assert usa[0x0D9328 + 1] == 0x5C and eu[0x0D8A84 + 1] == 0x5A
    assert usa[0x0DD790 + 0x1C] == 0xF4 and eu[0x0DCECC + 0x1C] == 0xF1
    assert usa[0x0DD790 + 0x1E] == 0xF3 and eu[0x0DCECC + 0x1E] == 0xF0
    assert usa[0x0DD790 + 0x2C] == 0xF6 and eu[0x0DCECC + 0x2C] == 0xF3
    assert usa[0x0DD790 + 0x2E] == 0xF5 and eu[0x0DCECC + 0x2E] == 0xF2
    assert usa[0x0DD7E0 + 20] == 0xF3 and eu[0x0DCF1C + 20] == 0xF0
    assert usa[0x0DD7E0 + 46] == 0xF3 and eu[0x0DCF1C + 46] == 0xF0
    assert usa[0x0DD840 + 20] == 0xF5 and eu[0x0DCF7C + 20] == 0xF2
    assert usa[0x0DD840 + 46] == 0xF3 and eu[0x0DCF7C + 46] == 0xF0
    assert usa[0x0F9BF8 + 12] == 0xAC and eu[0x0F9144 + 12] == 0xAA
    assert usa[0x0FEE58 + 14] == 0x57 and eu[0x0FE39C + 14] == 0x55
    print("All clean-ROM regional data and semantic flag fingerprints match.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except (OSError, RuntimeError, AssertionError) as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        raise SystemExit(1)
