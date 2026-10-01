#!/usr/bin/env bash
# tloz-tmc-ua: quick host-side check of the Ukrainian patch set.
# Run after every `git merge upstream/main`:   bash ua/check.sh
# Needs only a host gcc (no devkitPro, no ROM).
set -euo pipefail
cd "$(dirname "$0")/.."

fail() { echo "FAIL: $*" >&2; exit 1; }

# 1. The call sites must survive merges.
grep -q 'port_ua.h' src/gameOverTask.c || fail "src/gameOverTask.c lost #include \"port_ua.h\""
grep -q 'Port_UA_GameOverLetter' src/gameOverTask.c || fail "src/gameOverTask.c lost the Port_UA_GameOverLetter() hook"

# 2. Compile the touched game file with the 3DS build's defines (see platform/3ds/CMakeLists.txt).
DEFS="-DPC_PORT -DNON_MATCHING -DUSE_HDMA -DTMC_3DS -DMULTI_REGION -DUSA -DENGLISH -DREVISION=0 -DMODE1_GBA_WIDTH=400 -DMODE1_GBA_HEIGHT=240"
INC="-I. -Iinclude -Iport -Iport/ppu/include -Ibuild/USA"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
${CC:-gcc} -std=gnu11 -fsyntax-only -include region.h $DEFS $INC -Wall -Wno-unused -Wno-multichar -Wno-pointer-sign \
    src/gameOverTask.c
echo "PASS compile src/gameOverTask.c"

# 3. Behavioural test: English/EU unchanged, Ukrainian layout only with the TMC-UA marker.
${CC:-gcc} -std=gnu11 -O1 -w -include region.h $DEFS $INC ua/test_gameover.c src/gameOverTask.c \
    -o "$OUT/test_gameover" -Wl,--unresolved-symbols=ignore-all 2>/dev/null
"$OUT/test_gameover"
echo "All tloz-tmc-ua checks passed."
