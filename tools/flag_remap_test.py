#!/usr/bin/env python3
"""Regression tests for the regional flag inventory/remap generator."""

import sys
from pathlib import Path
import unittest


TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

import generate_flag_remap as flags  # noqa: E402


class RegionalFlagInventoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inventory = flags.load_inventory()
        cls.comparisons = flags.build_comparisons(cls.inventory)
        cls.tables = flags.build_tables(cls.inventory)
        cls.validity = flags.build_validity_tables(cls.inventory)
        flags.validate(cls.inventory, cls.comparisons, cls.tables, cls.validity)

    def test_fat_binary_uses_raw_usa_enum_layout(self):
        self.assertEqual(
            self.inventory["USA"], self.inventory["FAT_BINARY_USA_PC"]
        )

    def test_global_and_banks_6_through_12_are_region_identical(self):
        identical = ["Flag"] + ["LocalFlags%d" % bank for bank in range(6, 13)]
        for target in flags.RUNTIME_TARGETS:
            for enum_name in identical:
                with self.subTest(target=target, enum=enum_name):
                    self.assertEqual(
                        self.inventory["USA"][enum_name],
                        self.inventory[target][enum_name],
                    )

    def test_eu_divergence_inventory_is_exact(self):
        expected = {
            "LocalFlags1": {
                "remapped": 238,
                "baseline_only": ["KS_A06", "KS_B18", "KS_C21", "KS_C25"],
                "target_only": ["HIKYOU_00_T0", "HIKYOU_00_H00", "SOUGEN_07_H00"],
            },
            "LocalFlags2": {
                "remapped": 0,
                "baseline_only": [
                    "SHOP00_BOMBBAG",
                    "CAFE_01_CAP_1",
                    "KS_A02",
                    "KS_A09",
                    "KS_A18",
                    "KS_B07",
                    "KS_B16",
                ],
                "target_only": [],
            },
            "LocalFlags3": {
                "remapped": 0,
                "baseline_only": ["MACHI_CHIKA2_10_CAP_0", "KS_C02"],
                "target_only": [],
            },
            "LocalFlags4": {
                "remapped": 0,
                "baseline_only": [
                    "KS_B06",
                    "KS_B15",
                    "KS_B01",
                    "KS_B12",
                    "KS_C12",
                    "KS_C37",
                ],
                "target_only": [],
            },
            "LocalFlags5": {
                "remapped": 0,
                "baseline_only": ["LV1_12_CAP_0"],
                "target_only": [],
            },
        }
        self._assert_divergence_inventory("EU", expected)

    def test_jp_source_only_divergence_inventory_is_exact(self):
        expected = {
            "LocalFlags1": {
                "remapped": 238,
                "baseline_only": ["KS_A06", "KS_B18", "KS_C21", "KS_C25"],
                "target_only": ["HIKYOU_00_T0", "HIKYOU_00_H00", "SOUGEN_07_H00"],
            },
            "LocalFlags2": {
                "remapped": 0,
                "baseline_only": ["KS_A02", "KS_A09", "KS_A18", "KS_B07", "KS_B16"],
                "target_only": [],
            },
            "LocalFlags3": {
                "remapped": 0,
                "baseline_only": ["KS_C02"],
                "target_only": [],
            },
            "LocalFlags4": {
                "remapped": 0,
                "baseline_only": [
                    "KS_B06",
                    "KS_B15",
                    "KS_B01",
                    "KS_B12",
                    "KS_C12",
                    "KS_C37",
                ],
                "target_only": [],
            },
            "LocalFlags5": {
                "remapped": 0,
                "baseline_only": ["LV1_12_CAP_0"],
                "target_only": [],
            },
        }
        self._assert_divergence_inventory("JP_SOURCE_ONLY", expected)

    def test_cloud_tops_semantic_mapping_is_usa_243_to_eu_240(self):
        eu_bank1 = self.tables["EU"][0]
        jp_bank1 = self.tables["JP_SOURCE_ONLY"][0]
        self.assertEqual(eu_bank1[243], 240)
        self.assertEqual(jp_bank1[243], 240)

        comparison = self.comparisons["EU"]["LocalFlags1"]
        cloud_flag = next(
            entry for entry in comparison["remapped"] if entry["name"] == "KUMOUE_02_00"
        )
        self.assertEqual(
            cloud_flag,
            {"name": "KUMOUE_02_00", "baseline": 243, "target": 240},
        )

    def test_every_common_semantic_name_maps_to_its_target_ordinal(self):
        for target in flags.RUNTIME_TARGETS:
            for bank_index, enum_name in enumerate(flags.LOCAL_ENUM_NAMES):
                baseline = flags._value_map(self.inventory["USA"][enum_name])
                target_values = flags._value_map(self.inventory[target][enum_name])
                table = self.tables[target][bank_index]
                for name in baseline.keys() & target_values.keys():
                    with self.subTest(target=target, enum=enum_name, flag=name):
                        self.assertEqual(table[baseline[name]], target_values[name])

    def test_unresolved_baseline_only_names_are_never_claimed_as_mapped(self):
        for target in flags.RUNTIME_TARGETS:
            for bank_index, enum_name in enumerate(flags.LOCAL_ENUM_NAMES):
                table = self.tables[target][bank_index]
                valid = self.validity[target][bank_index]
                for entry in self.comparisons[target][enum_name]["baseline_only"]:
                    with self.subTest(target=target, enum=enum_name, flag=entry["name"]):
                        self.assertEqual(table[entry["baseline"]], entry["baseline"])
                        self.assertEqual(valid[entry["baseline"]], 0)

    def test_validity_tables_are_exact_and_exhaustive(self):
        expected_valid_counts = {
            "EU": [249, 205, 199, 135, 86, 69, 74, 164, 136, 151, 1, 1],
            "JP_SOURCE_ONLY": [249, 207, 200, 135, 86, 69, 74, 164, 136, 151, 1, 1],
        }
        for target in flags.RUNTIME_TARGETS:
            for bank_index, enum_name in enumerate(flags.LOCAL_ENUM_NAMES):
                baseline = flags._value_map(
                    self.inventory["USA"][enum_name], include_markers=True
                )
                target_names = set(
                    flags._value_map(
                        self.inventory[target][enum_name], include_markers=True
                    )
                )
                valid = self.validity[target][bank_index]
                expected = [0] * flags.TABLE_WIDTH
                for name, ordinal in baseline.items():
                    expected[ordinal] = int(
                        name in target_names
                        and (not flags._is_marker(name) or name.startswith("BEGIN"))
                    )
                with self.subTest(target=target, enum=enum_name):
                    self.assertEqual(valid, expected)
                    self.assertEqual(
                        sum(valid), expected_valid_counts[target][bank_index]
                    )
                    self.assertEqual(valid[baseline["BEGIN_%d" % (bank_index + 1)]], 1)
                    self.assertEqual(valid[baseline["END_%d" % (bank_index + 1)]], 0)
                    last_declared = max(baseline.values())
                    if last_declared + 1 < flags.TABLE_WIDTH:
                        self.assertTrue(
                            all(value == 0 for value in valid[last_declared + 1 :])
                        )

    def test_public_report_rows_exhaust_every_usa_eu_name(self):
        rows = flags.build_report_rows(self.inventory)
        actual = {(row["namespace"], row["name"]) for row in rows}
        expected = set()
        for enum_name in flags.ENUM_NAMES:
            expected.update(
                (enum_name, entry["name"])
                for entry in self.inventory["USA"][enum_name]
            )
            expected.update(
                (enum_name, entry["name"])
                for entry in self.inventory["EU"][enum_name]
            )
        self.assertEqual(len(rows), 1607)
        self.assertEqual(len(actual), len(rows))
        self.assertEqual(actual, expected)

        by_key = {(row["namespace"], row["name"]): row for row in rows}
        self.assertEqual(
            by_key[("LocalFlags1", "KUMOUE_02_00")],
            {
                "namespace": "LocalFlags1",
                "name": "KUMOUE_02_00",
                "usa": 243,
                "eu": 240,
                "status": "semantic remap required; valid",
            },
        )
        self.assertEqual(
            by_key[("LocalFlags2", "SHOP00_BOMBBAG")]["status"],
            "USA-only; invalid (no EU semantic equivalent)",
        )
        self.assertEqual(
            by_key[("LocalFlags1", "HIKYOU_00_T0")]["status"],
            "EU-only; no USA baseline ordinal",
        )
        self.assertEqual(
            by_key[("LocalFlags1", "END_1")]["status"],
            "END boundary marker; invalid (different terminal ordinal)",
        )

    def test_checked_in_generated_files_are_current(self):
        comparisons, tables, validity, header, source = flags._generated_outputs(
            self.inventory
        )
        self.assertEqual(comparisons, self.comparisons)
        self.assertEqual(tables, self.tables)
        self.assertEqual(validity, self.validity)
        self.assertEqual(flags.GENERATED_HEADER.read_text(encoding="utf-8"), header)
        self.assertEqual(flags.GENERATED_SOURCE.read_text(encoding="utf-8"), source)
        self.assertEqual(
            flags.GENERATED_REPORT.read_text(encoding="utf-8"),
            flags.render_report(self.inventory, self.comparisons),
        )

    def _assert_divergence_inventory(self, target, expected):
        for enum_name, expected_values in expected.items():
            comparison = self.comparisons[target][enum_name]
            with self.subTest(target=target, enum=enum_name):
                self.assertEqual(len(comparison["remapped"]), expected_values["remapped"])
                self.assertEqual(
                    [entry["name"] for entry in comparison["baseline_only"]],
                    expected_values["baseline_only"],
                )
                self.assertEqual(
                    [entry["name"] for entry in comparison["target_only"]],
                    expected_values["target_only"],
                )


if __name__ == "__main__":
    unittest.main()
