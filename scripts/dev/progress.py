#!/usr/bin/env python3

"""Report decompilation progress from the USA linker map."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "src"
MAP_PATH = REPO_ROOT / "build" / "USA" / "tmc.map"
NON_MATCHING_PATTERN = re.compile(
    r'(NONMATCH|ASM_FUNC)\(".*",\W*\w*\W*(\w*).*\)'
)


def collect_non_matching_funcs() -> list[tuple[str, str]]:
    result: list[tuple[str, str]] = []
    for path in SOURCE_ROOT.rglob("*.c"):
        data = path.read_text(encoding="utf-8")
        result.extend(NON_MATCHING_PATTERN.findall(data))
    return result


def parse_map(non_matching_funcs: set[str]) -> tuple[int, int, int, int]:
    if not MAP_PATH.is_file():
        raise FileNotFoundError(
            f"Linker map not found: {MAP_PATH.relative_to(REPO_ROOT)}. "
            "Build the USA target first."
        )

    lines = MAP_PATH.read_text(encoding="utf-8", errors="replace").splitlines()
    try:
        start = next(
            index
            for index, line in enumerate(lines)
            if line.startswith("Linker script and memory map")
        )
        start = next(
            index
            for index, line in enumerate(lines[start + 1 :], start + 1)
            if line.startswith("rom")
        )
    except StopIteration as error:
        raise ValueError("The linker map does not contain the expected ROM section.") from error

    src = asm = src_data = data = non_matching = 0
    previous_symbol: str | None = None
    previous_address = 0

    for line in lines[start + 1 :]:
        if line.startswith(" ."):
            fields = line.split()
            if len(fields) < 4:
                continue
            section, size_text, filepath = fields[0], fields[2], fields[3]
            size = int(size_text, 16)
            directory = filepath.split("/", 1)[0]

            if section == ".text":
                if directory == "src":
                    src += size
                elif directory == "asm":
                    if "asm/src/" in filepath or "asm/lib/" in filepath:
                        src += size
                    else:
                        asm += size
                elif directory == "data":
                    src_data += size
                elif directory == "..":
                    src += size
            elif section == ".rodata":
                if directory == "src":
                    src_data += size
                elif directory == "data":
                    data += size

        elif line.startswith("  "):
            fields = line.split()
            if len(fields) == 2 and fields[1]:
                if previous_symbol in non_matching_funcs:
                    non_matching += int(fields[0], 16) - previous_address
                previous_symbol = fields[1]
                previous_address = int(fields[0], 16)
        elif not line.strip():
            break

    src -= non_matching
    asm += non_matching
    return src, asm, src_data, data


def git_value(format_string: str) -> str:
    return subprocess.check_output(
        ["git", "show", "-s", f"--format={format_string}", "HEAD"],
        cwd=REPO_ROOT,
        text=True,
    ).strip()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compute decompilation progress for the current project."
    )
    parser.add_argument(
        "format",
        nargs="?",
        default="text",
        choices=("text", "csv", "shield-json"),
    )
    parser.add_argument(
        "-m",
        "--matching",
        action="store_true",
        help="Report matching progress instead of decompilation progress.",
    )
    args = parser.parse_args()

    functions = collect_non_matching_funcs()
    if args.matching:
        excluded = {name for _, name in functions}
    else:
        excluded = {name for kind, name in functions if kind == "ASM_FUNC"}

    src, asm, src_data, data = parse_map(excluded)
    total = src + asm
    data_total = src_data + data
    if total == 0 or data_total == 0:
        raise ValueError("The linker map produced an empty progress total.")

    src_percent = 100 * src / total
    data_percent = 100 * src_data / data_total

    if args.format == "csv":
        values = (
            "2",
            git_value("%ct"),
            git_value("%H"),
            str(src),
            str(total),
            str(src_data),
            str(data_total),
        )
        print(",".join(values))
    elif args.format == "shield-json":
        print(
            json.dumps(
                {
                    "schemaVersion": 1,
                    "label": "progress",
                    "message": f"{src_percent:.3g}%",
                    "color": "yellow",
                }
            )
        )
    else:
        adjective = "matched" if args.matching else "decompiled"
        print(f"src:  {src:9} / {total:8} total bytes {adjective:<10} {src_percent:9.4f}%")
        print(
            f"data: {src_data:9} / {data_total:8} total bytes analysed   "
            f"{data_percent:9.4f}%"
        )


if __name__ == "__main__":
    main()
