#!/usr/bin/env bash
# Formatting never runs clang-tidy --fix: structural/code changes need review.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
exec python3 "$root/scripts/dev/format.py" "$@"
