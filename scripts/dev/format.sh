#!/usr/bin/env bash

set -euo pipefail

readonly format_opts=(-i -style=file)
readonly tidy_opts=(-p . --fix --fix-errors)
readonly compiler_opts=(-fno-builtin -std=gnu90 -Iinclude -Isrc -D_LANGUAGE_C -DNON_MATCHING)

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_root"

for tool in clang-format clang-tidy; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Required tool not found: ${tool}" >&2
        exit 1
    fi
done

ensure_final_newline() {
    local file
    for file in "$@"; do
        [[ -s $file ]] || continue
        if [[ $(tail -c 1 "$file" | wc -l) -eq 0 ]]; then
            printf '\n' >> "$file"
        fi
    done
}

format_files() {
    local files=("$@")
    ((${#files[@]} > 0)) || return 0

    clang-format "${format_opts[@]}" "${files[@]}"
    clang-tidy "${tidy_opts[@]}" "${files[@]}" -- "${compiler_opts[@]}" >/dev/null
    ensure_final_newline "${files[@]}"
}

if (($# > 0)); then
    format_files "$@"
    exit 0
fi

source_files=()
while IFS= read -r file; do
    source_files+=("$file")
done < <(find src include -type f \( -name '*.c' -o -name '*.h' \) -print | LC_ALL=C sort)
format_files "${source_files[@]}"
