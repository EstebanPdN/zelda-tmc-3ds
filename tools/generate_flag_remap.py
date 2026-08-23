#!/usr/bin/env python3
"""Audit regional flag enums and generate the baseline-to-region remap tables.

The multi-region port is compiled with the USA enum layout.  Values loaded from
the active ROM are already native to that ROM, but named flags compiled into C
need a semantic (name-based) translation before accessing a local flag bank.

This tool intentionally preprocesses the retail USA/EU/JP declarations without
PC_PORT first.  Some conditions in include/flags.h contain ``defined(PC_PORT)``
and would otherwise make a European declaration look like the fat binary's USA
baseline.  JP is inventoried from source only; it is not a supported/verified
runtime profile in this repository.
"""

from __future__ import print_function

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
FLAGS_HEADER = ROOT / "include" / "flags.h"
GENERATED_HEADER = ROOT / "port" / "flag_remap_generated.h"
GENERATED_SOURCE = ROOT / "port" / "flag_remap_generated.c"
GENERATED_REPORT = ROOT / "docs" / "REGIONAL-FLAG-AUDIT.md"

ENUM_NAMES = ["Flag"] + ["LocalFlags%d" % bank for bank in range(1, 13)]
LOCAL_ENUM_NAMES = ENUM_NAMES[1:]
TABLE_WIDTH = 256

PROFILE_DEFINES = {
    "USA": ("USA", "ENGLISH"),
    "EU": ("EU", "ENGLISH"),
    "JP_SOURCE_ONLY": ("JP", "JAPANESE"),
    "FAT_BINARY_USA_PC": ("USA", "ENGLISH", "PC_PORT", "MULTI_REGION"),
}

RUNTIME_TARGETS = ("EU", "JP_SOURCE_ONLY")


def _compiler_command(override=None):
    if override:
        command = shlex.split(override)
    elif os.environ.get("CC"):
        command = shlex.split(os.environ["CC"])
    else:
        command = []
        for candidate in ("clang", "cc", "gcc"):
            resolved = shutil.which(candidate)
            if resolved:
                command = [resolved]
                break
    if not command:
        raise RuntimeError("no C preprocessor found (set CC or pass --cc)")
    return command


def _preprocess(defines, compiler=None):
    command = _compiler_command(compiler)
    command += [
        "-E",
        "-P",
        "-I%s" % ROOT,
        "-I%s" % (ROOT / "include"),
        "-I%s" % (ROOT / "port"),
    ]
    command += ["-D%s" % define for define in defines]
    command.append(str(FLAGS_HEADER))
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            "preprocessor failed for %s:\n%s"
            % (", ".join(defines), result.stderr.strip())
        )
    return result.stdout


def _parse_enum_body(body, enum_name):
    entries = []
    next_value = 0
    for raw_entry in body.split(","):
        raw_entry = raw_entry.strip()
        if not raw_entry:
            continue
        match = re.fullmatch(
            r"([A-Za-z_]\w*)(?:\s*=\s*((?:0[xX][0-9A-Fa-f]+)|(?:[0-9]+)))?",
            raw_entry,
        )
        if not match:
            raise RuntimeError(
                "unsupported entry in %s after preprocessing: %r"
                % (enum_name, raw_entry)
            )
        name, explicit_value = match.groups()
        if explicit_value is not None:
            next_value = int(explicit_value, 0)
        entries.append({"name": name, "value": next_value})
        next_value += 1

    names = [entry["name"] for entry in entries]
    if len(names) != len(set(names)):
        duplicates = sorted(name for name in set(names) if names.count(name) > 1)
        raise RuntimeError("duplicate names in %s: %s" % (enum_name, duplicates))
    return entries


def parse_flag_enums(preprocessed):
    enums = {}
    pattern = re.compile(r"typedef\s+enum\s*\{(.*?)\}\s*(\w+)\s*;", re.S)
    for match in pattern.finditer(preprocessed):
        body, enum_name = match.groups()
        if enum_name in ENUM_NAMES:
            enums[enum_name] = _parse_enum_body(body, enum_name)
    missing = [enum_name for enum_name in ENUM_NAMES if enum_name not in enums]
    if missing:
        raise RuntimeError("flag enums missing after preprocessing: %s" % missing)
    return enums


def load_inventory(compiler=None):
    return {
        profile: parse_flag_enums(_preprocess(defines, compiler))
        for profile, defines in PROFILE_DEFINES.items()
    }


def _is_marker(name):
    return name == "BEGIN" or name == "END" or name.startswith("BEGIN_") or name.startswith("END_")


def _value_map(entries, include_markers=False):
    return {
        entry["name"]: entry["value"]
        for entry in entries
        if include_markers or not _is_marker(entry["name"])
    }


def compare_enum(baseline_entries, target_entries):
    baseline = _value_map(baseline_entries)
    target = _value_map(target_entries)
    common_names = [name for name in baseline if name in target]
    return {
        "remapped": [
            {
                "name": name,
                "baseline": baseline[name],
                "target": target[name],
            }
            for name in common_names
            if baseline[name] != target[name]
        ],
        "baseline_only": [
            {"name": name, "baseline": value}
            for name, value in baseline.items()
            if name not in target
        ],
        "target_only": [
            {"name": name, "target": value}
            for name, value in target.items()
            if name not in baseline
        ],
        "common_identity": sum(
            1 for name in common_names if baseline[name] == target[name]
        ),
    }


def build_comparisons(inventory):
    baseline = inventory["USA"]
    return {
        target: {
            enum_name: compare_enum(baseline[enum_name], inventory[target][enum_name])
            for enum_name in ENUM_NAMES
        }
        for target in RUNTIME_TARGETS
    }


def build_remap_table(baseline_entries, target_entries):
    baseline = _value_map(baseline_entries)
    target = _value_map(target_entries)
    if baseline and max(baseline.values()) >= TABLE_WIDTH:
        raise RuntimeError("baseline local flag ordinal exceeds table width")
    if target and max(target.values()) >= TABLE_WIDTH:
        raise RuntimeError("target local flag ordinal exceeds table width")

    # A missing semantic name has no valid regional translation.  Keep an
    # identity placeholder in the byte-sized remap ABI; the parallel validity
    # table marks it 0 so runtime must reject it rather than use that byte.
    table = list(range(TABLE_WIDTH))
    for name, baseline_value in baseline.items():
        if name in target:
            table[baseline_value] = target[name]
    return table


def build_tables(inventory):
    baseline = inventory["USA"]
    return {
        target: [
            build_remap_table(baseline[enum_name], inventory[target][enum_name])
            for enum_name in LOCAL_ENUM_NAMES
        ]
        for target in RUNTIME_TARGETS
    }


def build_validity_table(baseline_entries, target_entries):
    target_names = set(_value_map(target_entries, include_markers=True))
    validity = [0] * TABLE_WIDTH
    for entry in baseline_entries:
        name = entry["name"]
        value = entry["value"]
        if value >= TABLE_WIDTH:
            raise RuntimeError("baseline local flag ordinal exceeds validity width")
        if _is_marker(name):
            validity[value] = int(name.startswith("BEGIN") and name in target_names)
            continue
        validity[value] = int(name in target_names)
    return validity


def build_validity_tables(inventory):
    baseline = inventory["USA"]
    return {
        target: [
            build_validity_table(baseline[enum_name], inventory[target][enum_name])
            for enum_name in LOCAL_ENUM_NAMES
        ]
        for target in RUNTIME_TARGETS
    }


def _names(entries, value_key):
    return ["%s=%d" % (entry["name"], entry[value_key]) for entry in entries]


def _summary_lines(comparisons):
    lines = []
    for target in RUNTIME_TARGETS:
        label = "EU (runtime-supported)" if target == "EU" else "JP (source-only; runtime unverified)"
        lines.append(label + ":")
        identical = []
        for enum_name in ENUM_NAMES:
            comparison = comparisons[target][enum_name]
            changed = comparison["remapped"]
            baseline_only = comparison["baseline_only"]
            target_only = comparison["target_only"]
            if not changed and not baseline_only and not target_only:
                identical.append(enum_name)
                continue
            lines.append(
                "  %s: semantic-remaps=%d; baseline-only=[%s]; target-only=[%s]"
                % (
                    enum_name,
                    len(changed),
                    ", ".join(_names(baseline_only, "baseline")),
                    ", ".join(_names(target_only, "target")),
                )
            )
        if identical:
            lines.append("  identical: " + ", ".join(identical))
    return lines


def render_header(comparisons):
    summary = "\n".join(" * " + line for line in _summary_lines(comparisons))
    return """/* AUTO-GENERATED by tools/generate_flag_remap.py - DO NOT EDIT BY HAND.
 * USA-baseline local-flag ordinal remap, matched strictly by semantic name.
 * Retail enums are preprocessed without PC_PORT so regional branches remain
 * visible. A remap byte is usable only when the matching Valid byte is 1;
 * baseline-only names, END markers, and out-of-enum ordinals are invalid.
 *
%s
 */
#ifndef FLAG_REMAP_GENERATED_H
#define FLAG_REMAP_GENERATED_H

#define FLAG_REMAP_BANK_COUNT 12
#define FLAG_REMAP_TABLE_WIDTH 256

#ifdef __cplusplus
extern "C" {
#endif

/* [bankNumber-1][USA baseline ordinal] -> active-region ordinal. */
extern const unsigned char gFlagRemapEU[FLAG_REMAP_BANK_COUNT][FLAG_REMAP_TABLE_WIDTH];
extern const unsigned char gFlagRemapJP[FLAG_REMAP_BANK_COUNT][FLAG_REMAP_TABLE_WIDTH];

/* 1 only for BEGIN or a semantic USA name present in the target region. */
extern const unsigned char gFlagRemapEUValid[FLAG_REMAP_BANK_COUNT][FLAG_REMAP_TABLE_WIDTH];
extern const unsigned char gFlagRemapJPValid[FLAG_REMAP_BANK_COUNT][FLAG_REMAP_TABLE_WIDTH];

#ifdef __cplusplus
}
#endif

#endif /* FLAG_REMAP_GENERATED_H */
""" % summary


def _format_table(table):
    rows = []
    for offset in range(0, TABLE_WIDTH, 16):
        rows.append("    " + ", ".join("%3d" % value for value in table[offset : offset + 16]) + ",")
    return "\n".join(rows)


def render_source(tables, validity, comparisons):
    summary = "\n".join(" * " + line for line in _summary_lines(comparisons))
    blocks = []
    validity_blocks = []
    for target, c_name in (("EU", "gFlagRemapEU"), ("JP_SOURCE_ONLY", "gFlagRemapJP")):
        bank_blocks = []
        valid_bank_blocks = []
        for index, table in enumerate(tables[target], 1):
            comparison = comparisons[target]["LocalFlags%d" % index]
            bank_blocks.append(
                "  { /* LocalFlags%d: semantic-remaps=%d, unresolved=%d */\n%s\n  },"
                % (
                    index,
                    len(comparison["remapped"]),
                    len(comparison["baseline_only"]),
                    _format_table(table),
                )
            )
            valid_bank_blocks.append(
                "  { /* LocalFlags%d: valid=%d */\n%s\n  },"
                % (
                    index,
                    sum(validity[target][index - 1]),
                    _format_table(validity[target][index - 1]),
                )
            )
        blocks.append(
            "const unsigned char %s[FLAG_REMAP_BANK_COUNT][FLAG_REMAP_TABLE_WIDTH] = {\n%s\n};"
            % (c_name, "\n".join(bank_blocks))
        )
        validity_blocks.append(
            "const unsigned char %sValid[FLAG_REMAP_BANK_COUNT][FLAG_REMAP_TABLE_WIDTH] = {\n%s\n};"
            % (c_name, "\n".join(valid_bank_blocks))
        )
    return """/* AUTO-GENERATED by tools/generate_flag_remap.py - DO NOT EDIT BY HAND.
 * USA-baseline local-flag ordinal remap, matched strictly by semantic name.
 * A remap byte is accepted only when the corresponding Valid byte is 1.
 *
%s
 */
#include "flag_remap_generated.h"

%s

%s

%s

%s
""" % (summary, blocks[0], blocks[1], validity_blocks[0], validity_blocks[1])


def _format_ordinal(value):
    if value is None:
        return "—"
    return "%d (`0x%02X`)" % (value, value)


def _report_status(namespace, name, usa_value, eu_value):
    if namespace == "Flag":
        if _is_marker(name):
            return "global boundary marker; not locally remapped"
        if usa_value == eu_value:
            return "global flag; same ordinal"
        return "global flag; regional difference"
    if usa_value is None:
        return "EU-only; no USA baseline ordinal"
    if eu_value is None:
        return "USA-only; invalid (no EU semantic equivalent)"
    if _is_marker(name):
        if name.startswith("BEGIN"):
            return "BEGIN boundary marker; valid"
        if usa_value == eu_value:
            return "END boundary marker; invalid (same terminal ordinal)"
        return "END boundary marker; invalid (different terminal ordinal)"
    if usa_value == eu_value:
        return "same ordinal; valid"
    return "semantic remap required; valid"


def build_report_rows(inventory):
    rows = []
    for enum_name in ENUM_NAMES:
        usa = _value_map(inventory["USA"][enum_name], include_markers=True)
        eu = _value_map(inventory["EU"][enum_name], include_markers=True)
        names = set(usa) | set(eu)
        ordered_names = sorted(
            names,
            key=lambda name: (
                min(usa.get(name, TABLE_WIDTH + 1), eu.get(name, TABLE_WIDTH + 1)),
                usa.get(name, TABLE_WIDTH + 1),
                eu.get(name, TABLE_WIDTH + 1),
                name,
            ),
        )
        for name in ordered_names:
            usa_value = usa.get(name)
            eu_value = eu.get(name)
            rows.append(
                {
                    "namespace": enum_name,
                    "name": name,
                    "usa": usa_value,
                    "eu": eu_value,
                    "status": _report_status(enum_name, name, usa_value, eu_value),
                }
            )
    return rows


def _compress_remaps(remapped):
    groups = []
    for entry in remapped:
        if (
            groups
            and entry["baseline"] == groups[-1][-1]["baseline"] + 1
            and entry["target"] == groups[-1][-1]["target"] + 1
        ):
            groups[-1].append(entry)
        else:
            groups.append([entry])
    return groups


def _format_name_list(entries, value_key):
    if not entries:
        return "—"
    return "<br>".join(
        "`%s` = %d (`0x%02X`)" % (entry["name"], entry[value_key], entry[value_key])
        for entry in entries
    )


def render_report(inventory, comparisons):
    report_rows = build_report_rows(inventory)
    lines = [
        "# Regional Flag Audit",
        "",
        "<!-- AUTO-GENERATED by tools/generate_flag_remap.py. DO NOT EDIT BY HAND. -->",
        "",
        "This report is the reproducible USA/European inventory of every named entry in",
        "`Flag` and `LocalFlags1` through `LocalFlags12` from `include/flags.h`, followed",
        "by the runtime and compiled-data corrections applied from that audit. It contains",
        "no ROM, save, dump, or extracted game bytes.",
        "",
        "## Method and scope",
        "",
        "The generator preprocesses the retail declarations separately for USA and Europe",
        "**without `PC_PORT`**. This is required because some source conditions deliberately",
        "include USA-only declarations when `PC_PORT` is defined. It then compares entries",
        "strictly by semantic identifier, never by proximity or numeric guesswork.",
        "",
        "The effective `USA + PC_PORT + MULTI_REGION` fat-binary declarations are checked",
        "independently and are currently identical to the raw USA retail declaration in all",
        "13 namespaces. Europe is the supported non-USA runtime profile. JP is also parsed",
        "for generator validation, but remains source-only and runtime-unverified, so it is",
        "not presented as a supported column in this report.",
        "",
        "`BEGIN*` and `END*` boundary markers are included in the exhaustive table because",
        "they are named enum entries. They are excluded from semantic remap tables and from",
        "the semantic counts below.",
        "",
        "## Summary",
        "",
        "| Namespace | USA entries | EU entries | Same semantic ordinal | Semantic remaps | USA-only | EU-only |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]

    for enum_name in ENUM_NAMES:
        comparison = comparisons["EU"][enum_name]
        lines.append(
            "| `%s` | %d | %d | %d | %d | %d | %d |"
            % (
                enum_name,
                len(inventory["USA"][enum_name]),
                len(inventory["EU"][enum_name]),
                comparison["common_identity"],
                len(comparison["remapped"]),
                len(comparison["baseline_only"]),
                len(comparison["target_only"]),
            )
        )

    lines += [
        "",
        "The exact result is:",
        "",
        "- `Flag` and `LocalFlags6` through `LocalFlags12` are region-identical.",
        "- `LocalFlags1` has 238 shared semantic flags whose ordinals differ.",
        "- `LocalFlags2` through `LocalFlags5` differ structurally, but every shared",
        "  non-marker name keeps the same ordinal. Their differences are USA-only flags",
        "  with no European semantic equivalent.",
        "- A USA-only or EU-only name is **unresolved**, not an invitation to map it to a",
        "  nearby numeric slot. Its numeric remap byte remains an ABI-compatible identity",
        "  placeholder, while the parallel validity byte is zero so runtime rejects it.",
        "- Validity is one only when a USA semantic name exists in Europe. `BEGIN*` at",
        "  ordinal zero is the sole marker exception; `END*`, baseline-only names, and all",
        "  slots outside the USA enum are invalid and rejected.",
        "",
        "## Names without a cross-region equivalent",
        "",
        "| Namespace | USA-only | EU-only |",
        "|---|---|---|",
    ]

    for enum_name in ENUM_NAMES:
        comparison = comparisons["EU"][enum_name]
        if comparison["baseline_only"] or comparison["target_only"]:
            lines.append(
                "| `%s` | %s | %s |"
                % (
                    enum_name,
                    _format_name_list(comparison["baseline_only"], "baseline"),
                    _format_name_list(comparison["target_only"], "target"),
                )
            )

    lines += [
        "",
        "## `LocalFlags1` semantic remap ranges",
        "",
        "These 14 consecutive ranges compactly cover all 238 shared flags that move. The",
        "exhaustive table below remains the authoritative per-name inventory.",
        "",
        "| Semantic names | USA ordinal(s) | EU ordinal(s) | Delta |",
        "|---|---:|---:|---:|",
    ]

    for group in _compress_remaps(comparisons["EU"]["LocalFlags1"]["remapped"]):
        first = group[0]
        last = group[-1]
        if len(group) == 1:
            names = "`%s`" % first["name"]
            usa_range = str(first["baseline"])
            eu_range = str(first["target"])
        else:
            names = "`%s` … `%s`" % (first["name"], last["name"])
            usa_range = "%d–%d" % (first["baseline"], last["baseline"])
            eu_range = "%d–%d" % (first["target"], last["target"])
        lines.append(
            "| %s | %s | %s | %+d |"
            % (names, usa_range, eu_range, first["target"] - first["baseline"])
        )

    lines += [
        "",
        "Cloud Tops is inside the final range: `KUMOUE_02_00` is USA ordinal 243",
        "and European ordinal 240. A USA-compiled entity list that writes 243 while an",
        "EU-ROM script waits for 240 creates the exact hidden-whirlwind softlock. The table",
        "proves the equivalence; correct runtime provenance is still required at every",
        "compiled-data and C call site.",
        "",
        "## Implemented corrections",
        "",
        "The enum comparison is enforced at runtime, not only documented:",
        "",
        "- `gFlagRemapEU` translates a USA-baseline ordinal only after",
        "  `gFlagRemapEUValid` proves that the same semantic name exists in Europe.",
        "  Checks of an unresolved flag return false; set/clear operations become no-ops.",
        "- Multi-bit baseline checks translate every semantic member independently. This",
        "  is required because a consecutive USA range can be non-contiguous in Europe.",
        "- Original raw numeric flag accesses are treated as region-native source logic.",
        "  The 157 calls which an earlier implementation incorrectly converted to `*B`",
        "  were restored to their plain APIs. Named enums and fields proven to originate",
        "  in compiled USA tables use the provenance-aware `*B` APIs.",
        "- Stockwell's indirect shop flag is now handled as USA-baseline data. Its",
        "  USA-only `SHOP00_BOMBBAG` case is rejected on EU instead of writing an",
        "  unrelated European bit at ordinal 205.",
        "- The computed Armos flag range and `LV4_0a_TSUBO` room check now also use",
        "  baseline-aware access. Their ordinals currently match, but the contract now",
        "  remains correct if a later regional declaration moves them.",
        "- ROM-native entity and tile records are never remapped a second time. Known",
        "  compiled-USA records are replaced at the loader boundary by the complete",
        "  verified EU record/list, including terminators and structural differences.",
        "",
        "### Compiled-USA data replaced by verified EU data",
        "",
        "| USA symbol | EU ROM offset | Minimum bytes | Reason |",
        "|---|---:|---:|---|",
        "| `gUnk_080D9328` | `0x0D8A84` | `0x10` | HAKA tile-entity local flag |",
        "| `gUnk_080DD7E0` | `0x0DCF1C` | `0x40` | Cloud Tops upper fight: cloud and spawn-manager flag |",
        "| `gUnk_080DD840` | `0x0DCF7C` | `0x40` | Cloud Tops lower fight: completion and prerequisite flags |",
        "| `gUnk_080EAE60` | `0x0EA53C` | `0x50` | EU list terminates where USA has an extra manager |",
        "| `gUnk_080EB9F4` | `0x0EB0C0` | `0x70` | EU list has fewer records than USA |",
        "| `gUnk_080F78A0` | `0x0F6E5C` | `0x20` | Regionally different entity type |",
        "| `gUnk_080F9BF8` | `0x0F9144` | `0x40` | Ezlo-hint local flag |",
        "| `gUnk_080F09A0` | `0x0EFFD4` | `0x60` | Castle Garden packed local flags |",
        "| `gUnk_080FEAC8` | `0x0FE00C` | `0x120` | World-event small-chest local flags |",
        "| `gUnk_080FEE58` | `0x0FE39C` | `0x20` | Pushable-grave local flag |",
        "",
        "The USA-only direct lists `gUnk_080F58A8` and `gUnk_080F5B3C` have no EU",
        "counterpart and are explicitly rejected if an EU path ever attempts to load",
        "them. Unknown compiled pointers are left unchanged rather than guessed.",
        "",
        "Other compiled tables whose structure is shared but whose flag fields are USA",
        "baseline are translated at the proven field boundary. This includes world-event",
        "conditions/rewards, kinstone hint conditions, beanstalk flags and compiled NPC,",
        "manager, enemy, object and room-initialization enum references audited in source.",
        "",
        "### Save compatibility",
        "",
        "No save format, save size or slot layout is changed, and no bulk USA/EU flag",
        "migration is performed. Existing EU saves already store EU-native bits; bulk",
        "remapping them would corrupt valid progress. Corrections happen only when",
        "USA-compiled code/data accesses the active region's existing save bitfield.",
        "",
        "## Exhaustive USA/EU inventory",
        "",
        "This table contains %d unique `(namespace, name)` rows." % len(report_rows),
        "",
        "| Namespace | Name | USA | EU | Status |",
        "|---|---|---:|---:|---|",
    ]

    for row in report_rows:
        lines.append(
            "| `%s` | `%s` | %s | %s | %s |"
            % (
                row["namespace"],
                row["name"],
                _format_ordinal(row["usa"]),
                _format_ordinal(row["eu"]),
                row["status"],
            )
        )

    lines += [
        "",
        "## Reproduction and verification",
        "",
        "```sh",
        "python3 tools/generate_flag_remap.py --write-report",
        "python3 tools/generate_flag_remap.py --check-report",
        "python3 tools/generate_flag_remap.py --check",
        "python3 tools/flag_remap_test.py",
        "python3 tools/verify_eu_region_data.py [USA_ROM [EU_ROM]]",
        "xmake build flag_runtime_test region_data_resolver_test",
        "./build/pc/flag_runtime_test",
        "./build/pc/region_data_resolver_test",
        "```",
        "",
        "For machine-readable output containing USA, EU, source-only JP, the effective fat",
        "binary profile, comparisons, and complete 256-entry remap/validity tables:",
        "",
        "```sh",
        "python3 tools/generate_flag_remap.py --json",
        "```",
        "",
    ]
    return "\n".join(lines)


def build_json_inventory(inventory, comparisons, tables, validity):
    return {
        "source": "include/flags.h",
        "baseline": "USA retail enum layout",
        "fat_binary_profile": "FAT_BINARY_USA_PC",
        "jp_status": "source-only; runtime unverified",
        "profiles": inventory,
        "comparisons_from_usa": comparisons,
        "tables": tables,
        "validity": validity,
    }


def validate(inventory, comparisons, tables, validity):
    errors = []
    if inventory["FAT_BINARY_USA_PC"] != inventory["USA"]:
        errors.append("fat binary enum layout differs from the raw USA baseline")

    for target in RUNTIME_TARGETS:
        if comparisons[target]["Flag"]["remapped"]:
            errors.append("global Flag enum differs semantically for %s" % target)
        for bank_index, enum_name in enumerate(LOCAL_ENUM_NAMES):
            table = tables[target][bank_index]
            valid = validity[target][bank_index]
            target_values = _value_map(inventory[target][enum_name])
            baseline_all = _value_map(
                inventory["USA"][enum_name], include_markers=True
            )
            target_all = _value_map(
                inventory[target][enum_name], include_markers=True
            )
            if len(valid) != TABLE_WIDTH or any(value not in (0, 1) for value in valid):
                errors.append("invalid validity table shape/value for %s %s" % (target, enum_name))
                continue
            for name, ordinal in baseline_all.items():
                expected = int(
                    name in target_all
                    and (not _is_marker(name) or name.startswith("BEGIN"))
                )
                if valid[ordinal] != expected:
                    errors.append(
                        "bad %s %s validity for %s" % (target, enum_name, name)
                    )
            baseline_ordinals = set(baseline_all.values())
            for ordinal in range(TABLE_WIDTH):
                if ordinal not in baseline_ordinals and valid[ordinal] != 0:
                    errors.append(
                        "out-of-enum %s %s ordinal %d marked valid"
                        % (target, enum_name, ordinal)
                    )
            for entry in comparisons[target][enum_name]["remapped"]:
                if table[entry["baseline"]] != target_values[entry["name"]]:
                    errors.append("bad %s %s mapping for %s" % (target, enum_name, entry["name"]))
                if valid[entry["baseline"]] != 1:
                    errors.append("mapped %s %s entry %s is invalid" % (target, enum_name, entry["name"]))
            for entry in comparisons[target][enum_name]["baseline_only"]:
                if table[entry["baseline"]] != entry["baseline"]:
                    errors.append("non-identity unresolved %s %s entry %s" % (target, enum_name, entry["name"]))
                if valid[entry["baseline"]] != 0:
                    errors.append("unresolved %s %s entry %s is valid" % (target, enum_name, entry["name"]))
    if errors:
        raise RuntimeError("regional flag audit failed:\n- " + "\n- ".join(errors))


def _generated_outputs(inventory):
    comparisons = build_comparisons(inventory)
    tables = build_tables(inventory)
    validity = build_validity_tables(inventory)
    validate(inventory, comparisons, tables, validity)
    return (
        comparisons,
        tables,
        validity,
        render_header(comparisons),
        render_source(tables, validity, comparisons),
    )


def _check_file(path, expected):
    if not path.exists():
        return "%s is missing" % path.relative_to(ROOT)
    actual = path.read_text(encoding="utf-8")
    if actual != expected:
        return "%s is stale; run tools/generate_flag_remap.py" % path.relative_to(ROOT)
    return None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--check", action="store_true", help="validate that all generated files are current")
    action.add_argument("--write-report", action="store_true", help="write only docs/REGIONAL-FLAG-AUDIT.md")
    action.add_argument("--check-report", action="store_true", help="validate only docs/REGIONAL-FLAG-AUDIT.md")
    action.add_argument("--summary", action="store_true", help="print the concise regional divergence summary")
    action.add_argument("--json", action="store_true", help="print the exhaustive machine-readable inventory")
    parser.add_argument("--cc", help="C compiler/preprocessor command (defaults to CC, clang, cc, or gcc)")
    args = parser.parse_args(argv)

    inventory = load_inventory(args.cc)
    comparisons, tables, validity, header, source = _generated_outputs(inventory)
    report = render_report(inventory, comparisons)

    if args.check:
        failures = [
            failure
            for failure in (
                _check_file(GENERATED_HEADER, header),
                _check_file(GENERATED_SOURCE, source),
                _check_file(GENERATED_REPORT, report),
            )
            if failure
        ]
        if failures:
            print("\n".join(failures), file=sys.stderr)
            return 1
        print("regional flag inventory, remap tables, and public report are current")
        return 0
    if args.check_report:
        failure = _check_file(GENERATED_REPORT, report)
        if failure:
            print(failure, file=sys.stderr)
            return 1
        print("public regional flag audit report is current")
        return 0
    if args.write_report:
        GENERATED_REPORT.write_text(report, encoding="utf-8")
        print("updated %s" % GENERATED_REPORT.relative_to(ROOT))
        return 0
    if args.summary:
        print("\n".join(_summary_lines(comparisons)))
        return 0
    if args.json:
        print(
            json.dumps(
                build_json_inventory(inventory, comparisons, tables, validity),
                indent=2,
                sort_keys=True,
            )
        )
        return 0

    GENERATED_HEADER.write_text(header, encoding="utf-8")
    GENERATED_SOURCE.write_text(source, encoding="utf-8")
    GENERATED_REPORT.write_text(report, encoding="utf-8")
    print(
        "updated %s, %s, and %s"
        % (
            GENERATED_HEADER.relative_to(ROOT),
            GENERATED_SOURCE.relative_to(ROOT),
            GENERATED_REPORT.relative_to(ROOT),
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
