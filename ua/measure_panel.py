#!/usr/bin/env python3
"""
tloz-tmc-ua: measure the Ukrainian panel strings with the real banner font.

The Ukrainian banner font is wider than the English one. Buttons and value
chips adapt, but settings rows (label + value), developer rows and dialog lines
are drawn at a fixed scale, so their Ukrainian text must fit the row
(settings) or the English text the port was laid out for (developer rows,
dialogs). This script reports what overflows.

    python3 ua/measure_panel.py --ua-rom tmc-ua-port.gba --en-rom baserom.gba

Needs gcc (builds ua/test_panel_text.c) and both ROMs; nothing is written to them.
"""
import argparse
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# settings rows: label -> possible values (port/port_second_screen.c)
SETTINGS = {
    "TOP HUD": ["SHOW", "HIDE"], "WIDESCREEN": ["ON", "OFF", "RESTART"], "FOLLOW CAM": ["ON", "OFF"],
    "WINDCREST PINS": ["ON", "OFF"], "FLOOR AUTO RETURN": ["ON", "OFF"], "TURBO SPEED": ["X5"],
    "MASTER VOLUME": ["100"], "AUTOSAVE": ["ON", "OFF"], "COLOR CORRECTION": ["ON", "OFF"],
    "SHOW FPS": ["ON", "OFF"], "HOLD TO ADVANCE TEXT": ["ON", "OFF"], "RANDOMIZER": ["ON", "OFF"],
    "PANEL BACKDROP": ["PATTERN", "CREAM", "DARK", "DIM", "STONE", "SLATE", "NAVY"],
    "SWAP SCREENS": ["ON", "OFF", "RESTART"], "ASPECT RATIO": ["WIDE", "ORIGINAL", "STRETCH"],
    "DISPLAY STYLE": ["BLUR", "BILINEAR", "PIXEL PERFECT"],
}
# developer rows: label -> values
DEVELOPER = {
    "MEM DUMP": ["WRITE", "DONE"],
    "LOAD STATE": ["LOAD", "LOADED", "LEGACY", "NO DUMP", "NO STATE", "INVALID", "WRONG ROM", "I O ERROR", "ERROR"],
}
# dialogs: every line must fit the widest English line of the same dialog
DIALOGS = [
    ["LOAD LATEST DUMP?", "THE LATEST DUMP IN THE", "DUMPS FOLDER WILL REPLACE", "THE CURRENT GAME STATE.",
     "UNSAVED PROGRESS MAY BE LOST.", "THE GAME WILL RESTART."],
    ["RANDOMIZER REQUIRES A NEW GAME.", "UNSAVED PROGRESS MAY BE LOST."],
    ["THE ACTIVE PROFILE SAVE,", "AUTOSAVES, SAVESTATES, AND", "RANDOMIZER DATA WILL BE",
     "DELETED. THE ROM IS KEPT.", "THE GAME WILL RESTART."],
]
DEVELOPER_BUDGET = 200  # px at scale 1; verified on hardware screenshots
# settings row (3DS, scale 1): row 285 px wide (PaintSettingsPanel), label at +6,
# chip 3.3 px from the right edge with 20u + 2 * Port_UA_ChipPad (10.7 px) of
# padding, and an 8 px gap between label and chip -> label + value <= 257.
SETTINGS_BUDGET = 257


def bank8(rom):
    return struct.unpack_from("<I", rom, 0x109248 + 8 * 4)[0] - 0x8000000


def cell_width(glyph):
    row0 = struct.unpack_from("<I", glyph, 0)[0]
    mask, i = 0xF, 0
    while i < 8 and (row0 & mask) == mask:
        mask <<= 4
        i += 1
    j = i
    while i < 8 and (row0 & mask) != mask:
        mask <<= 4
        i += 1
    return i - j


def width(rom, codes):
    base, w = bank8(rom), 0
    for c in codes:
        if c == 0x20:
            w += 8
            continue
        g = rom[base + c * 128: base + c * 128 + 128]
        adv = cell_width(g[:64]) + cell_width(g[64:])
        w += adv - 1 if adv > 1 else adv
    return w


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ua-rom", required=True)
    ap.add_argument("--en-rom", required=True, help="retail USA ROM")
    args = ap.parse_args()
    ua_rom = Path(args.ua_rom).read_bytes()
    en_rom = Path(args.en_rom).read_bytes()

    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "test_panel_text"
        defs = "-DPC_PORT -DNON_MATCHING -DUSE_HDMA -DTMC_3DS -DMULTI_REGION -DUSA -DENGLISH -DREVISION=0".split()
        inc = ["-I.", "-Iinclude", "-Iport", "-Iport/ppu/include", "-Ibuild/USA"]
        subprocess.run(["gcc", "-std=gnu11", "-O1", "-w", "-include", "region.h", *defs, *inc,
                        "ua/test_panel_text.c", "-o", str(exe)], cwd=ROOT, check=True)

        strings = sorted({s for l, vs in {**SETTINGS, **DEVELOPER}.items() for s in [l, *vs]} |
                         {s for d in DIALOGS for s in d})
        out = subprocess.run([str(exe), "big"], input="\n".join(strings) + "\n", capture_output=True,
                             text=True, check=True).stdout.splitlines()
    ua = {}
    for line in out:
        en, hx = line.split("\t")
        ua[en] = width(ua_rom, bytes.fromhex(hx))

    def en(s):
        return width(en_rom, s.encode())

    problems = []
    budget = SETTINGS_BUDGET
    for l, vs in SETTINGS.items():
        w = ua[l] + max(ua[v] for v in vs)
        if w > budget:
            problems.append(f"settings row  {w:4d} > {budget}: {l}")
    for l, vs in DEVELOPER.items():
        for v in vs:
            w = ua[l] + ua[v]
            if w > DEVELOPER_BUDGET:
                problems.append(f"developer row {w:4d} > {DEVELOPER_BUDGET}: {l} / {v}")
    for d in DIALOGS:
        limit = max(en(s) for s in d)
        for s in d:
            if ua[s] > limit:
                problems.append(f"dialog line   {ua[s]:4d} > {limit}: {s}")

    for p in problems:
        print("OVER " + p)
    print("panel widths OK" if not problems else f"{len(problems)} string(s) too wide")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
