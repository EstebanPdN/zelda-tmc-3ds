#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "Usage: $0 <start-address> <length> <build-name> <base-name>" >&2
    exit 2
fi

start_address=$1
length=$2
build_name=$3
base_name=$4

integer_pattern='^([0-9]+|0[xX][0-9a-fA-F]+)$'
if [[ ! $start_address =~ $integer_pattern || ! $length =~ $integer_pattern ]]; then
    echo "Start address and length must be decimal or hexadecimal integers." >&2
    exit 2
fi

: "${DEVKITARM:?DEVKITARM must point to the devkitARM installation}"

objdump="${DEVKITARM}/bin/arm-none-eabi-objdump"
if [[ ! -x $objdump ]]; then
    echo "arm-none-eabi-objdump was not found at ${objdump}." >&2
    exit 1
fi

base_rom="${base_name}.gba"
build_rom="${build_name}.gba"
for rom in "$base_rom" "$build_rom"; do
    if [[ ! -f $rom ]]; then
        echo "ROM image not found: ${rom}" >&2
        exit 1
    fi
done

stop_address=$((start_address + length))
base_dump="${base_name}.dump"
build_dump="${build_name}.dump"

"$objdump" -D -b binary -m armv4t -M force-thumb \
    "--start-address=${start_address}" "--stop-address=${stop_address}" \
    "$base_rom" > "$base_dump"
"$objdump" -D -b binary -m armv4t -M force-thumb \
    "--start-address=${start_address}" "--stop-address=${stop_address}" \
    "$build_rom" > "$build_dump"

diff -u "$base_dump" "$build_dump"
