#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_root"

headers=()
while IFS= read -r header; do
    headers+=("$header")
done < <(find include -type f -name '*.h' -print | LC_ALL=C sort)
output=ctx.c

{
    printf '#include "gba/types.h"\n'
    for header in "${headers[@]}"; do
        printf '#include "%s"\n' "${header#include/}"
    done
} | cc -E -nostdinc -Iinclude -Itools/agbcc/include - > "$output"

printf '%s headers, written to %s\n' "${#headers[@]}" "$output"
