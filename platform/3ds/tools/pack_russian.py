#!/usr/bin/env python3
"""Pack translations/Russian.json into The Minish Cap's USA text-table format.

No third-party modules are required. Cyrillic is encoded into raw bytes 0x80..
which the non-Japanese text renderer maps to glyph bank 2.
"""

import argparse
import json
import struct
import sys
from pathlib import Path

UPPER = "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"
LOWER = "абвгдеёжзийклмнопрстуфхцчшщъыьэюя"
EXTRA = "«»—№"
RU_CHARS = UPPER + LOWER + EXTRA
RU_BYTES = {ch: 0x80 + i for i, ch in enumerate(RU_CHARS)}

COLORS = {name: i for i, name in enumerate(("White", "Red", "Green", "Blue", "Yellow"))}
KEYS = {name: i for i, name in enumerate(("A", "B", "Left", "Right", "DUp", "DDown", "DLeft", "DRight", "Dpad", "Select", "Start"))}


def hexbyte(value: str) -> int:
    n = int(value, 16)
    if not 0 <= n <= 0xFF:
        raise ValueError(f"byte out of range: {value}")
    return n


def encode_tag(body: str) -> bytes:
    parts = body.split(":")
    name = parts[0]

    if name == "Color" and len(parts) == 2:
        return bytes((0x02, COLORS[parts[1]]))
    if name == "Sound" and len(parts) == 3:
        return bytes((0x03, hexbyte(parts[1]), hexbyte(parts[2])))
    if name == "Choice" and len(parts) in (2, 3):
        first = hexbyte(parts[1])
        return bytes((0x05, first)) if first == 0xFF else bytes((0x05, first, hexbyte(parts[2])))
    if name == "Player" and len(parts) == 1:
        return bytes((0x06, 0x00))
    if name == "Var" and len(parts) == 2:
        return bytes((0x06, hexbyte(parts[1])))
    if name == "Key" and len(parts) == 2:
        return bytes((0x0C, KEYS[parts[1]]))
    if name == "Symbol" and len(parts) == 2:
        return bytes((0x0F, hexbyte(parts[1])))

    # Raw command notation used by the original extractor, e.g.
    # {04:10:0E}, {07:05:84}, {08:FF}, {09:78}, {01:04}.
    try:
        return bytes(hexbyte(p) for p in parts)
    except ValueError as exc:
        raise ValueError(f"unsupported tag {{{body}}}") from exc


def encode_string(text: str) -> bytes:
    out = bytearray()
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "{":
            end = text.find("}", i + 1)
            if end < 0:
                raise ValueError(f"unterminated tag in {text!r}")
            out.extend(encode_tag(text[i + 1:end]))
            i = end + 1
            continue

        if ch in RU_BYTES:
            out.append(RU_BYTES[ch])
        else:
            cp = ord(ch)
            if ch == "\n":
                out.append(0x0A)
            elif ch == "\r":
                out.append(0x0D)
            elif 0x20 <= cp <= 0x7A:
                # The supplied Russian table only uses the same ASCII subset
                # supported by tmc_strings for the USA translation.
                out.append(cp)
            else:
                raise ValueError(f"unsupported character U+{cp:04X} {ch!r}")
        i += 1

    out.append(0)
    return bytes(out)


def pack_table(data) -> bytes:
    if not isinstance(data, list):
        raise ValueError("top-level JSON must be an array")

    root_size = len(data) * 4
    root = bytearray(root_size)
    payload = bytearray()

    for category_index, category in enumerate(data):
        if not isinstance(category, list):
            raise ValueError(f"category {category_index} is not an array")

        category_start = root_size + len(payload)
        struct.pack_into("<I", root, category_index * 4, category_start)

        offsets = bytearray(len(category) * 4)
        strings = bytearray()
        strings_start = len(offsets)

        for string_index, item in enumerate(category):
            if not isinstance(item, str):
                raise ValueError(f"category {category_index}, string {string_index} is not a string")
            struct.pack_into("<I", offsets, string_index * 4, strings_start + len(strings))
            try:
                strings.extend(encode_string(item))
            except Exception as exc:
                raise ValueError(f"category {category_index}, string {string_index}: {exc}") from exc

        # Match tools/src/tmc_strings/main.cpp: pad the string payload of each
        # category to 16 bytes, then append it after the category offset table.
        while len(strings) & 0x0F:
            strings.append(0xFF)
        payload.extend(offsets)
        payload.extend(strings)

    out = root + payload
    while len(out) & 0x0F:
        out.append(0xFF)
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("dest", type=Path)
    args = parser.parse_args()

    with args.source.open("r", encoding="utf-8") as fh:
        data = json.load(fh)

    packed = pack_table(data)
    args.dest.parent.mkdir(parents=True, exist_ok=True)
    args.dest.write_bytes(packed)
    print(f"packed {sum(len(c) for c in data)} strings, {len(data)} categories -> {len(packed)} bytes")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
