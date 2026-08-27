#!/usr/bin/env python3
"""Source-level regression for the Goron Kinstone script callback registry."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = (ROOT / "port/port_script_funcs.c").read_text(encoding="utf-8")
WORLD_EVENT = (ROOT / "src/worldEvent/worldEvent17.c").read_text(encoding="utf-8")
FINAL_SCRIPT = (ROOT / "data/scripts/kinstoneFusion/script_Goron2Kinstone6.inc").read_text(encoding="utf-8")

EXPECTED = {
    "sub_08054EB8": ("0x08054EB9", "0x08054A39"),
    "sub_08054EFC": ("0x08054EFD", "0x08054A7D"),
    "sub_08054F64": ("0x08054F65", "0x08054AE5"),
}


def fail(message: str) -> None:
    print(f"port_goron_script_registry_test: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


for function, addresses in EXPECTED.items():
    if f"void {function}(" not in WORLD_EVENT:
        fail(f"{function} implementation is missing")
    if f"extern void {function}();" not in REGISTRY:
        fail(f"{function} declaration is missing from the registry")
    for address in addresses:
        entry = f"{{ {address}, (void (*)(void)){function} }}"
        if entry not in REGISTRY:
            fail(f"{function} is not registered at retail Thumb address {address}")

if FINAL_SCRIPT.count("Call sub_08054F64") != 3:
    fail("the final Goron choreography no longer contains its three synchronization calls")

tables = re.findall(
    r"static const ScriptFuncEntry sScriptFuncTable(?:_EU|_JP)?\[\] = \{(.*?)\n\};",
    REGISTRY,
    flags=re.DOTALL,
)
if len(tables) != 3:
    fail("could not identify all three regional script tables")
for table in tables:
    addresses = [int(value, 16) for value in re.findall(r"\{ (0x[0-9A-Fa-f]+),", table)]
    if addresses != sorted(addresses):
        fail("a regional script table is no longer sorted for binary lookup")

print("port_goron_script_registry_test: ALL PASS")
