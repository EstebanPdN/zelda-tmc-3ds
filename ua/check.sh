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
[ "$(grep -c 'PORT_UA_PANEL_TEXT' port/port_second_screen_theme.c)" = "4" ] || fail "port/port_second_screen_theme.c must call PORT_UA_PANEL_TEXT in exactly 4 places (TextWidth, DrawText, BigTextWidth, DrawBigTextPal)"
grep -q 'Port_UA_MessageGlyphHigh' port/port_second_screen_theme.c || fail "port/port_second_screen_theme.c lost the bank-2 glyph hook in GlyphData()"
grep -q 'Port_UA_HandleTitlescreen()' src/title.c || fail "src/title.c lost the Port_UA_HandleTitlescreen() hook"
grep -q 'port_ua_title.inc' src/title.c || fail "src/title.c lost #include \"port_ua_title.inc\""
grep -q 'Port_UA_TitleScreenObject(this)' src/object/titleScreenObject.c || fail "titleScreenObject.c lost its tloz-tmc-ua hook"
grep -q 'Port_UA_JapaneseSubtitle(this)' src/object/japaneseSubtitle.c || fail "japaneseSubtitle.c lost its tloz-tmc-ua hook"
grep -q 'Port_UA_KinstoneFuserName' src/menu/kinstoneMenu.c || fail "kinstoneMenu.c lost the Port_UA_KinstoneFuserName hook"
grep -q 'Port_UA_KinstoneHeaderX' src/menu/pauseMenu.c || fail "pauseMenu.c lost the Port_UA_KinstoneHeaderX hook"

# 2. Compile the touched game file with the 3DS build's defines (see platform/3ds/CMakeLists.txt).
DEFS="-DPC_PORT -DNON_MATCHING -DUSE_HDMA -DTMC_3DS -DMULTI_REGION -DUSA -DENGLISH -DREVISION=0 -DMODE1_GBA_WIDTH=400 -DMODE1_GBA_HEIGHT=240"
INC="-I. -Iinclude -Iport -Iport/ppu/include -Ibuild/USA"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
for f in src/gameOverTask.c src/title.c src/object/titleScreenObject.c src/object/japaneseSubtitle.c \
         src/menu/kinstoneMenu.c src/menu/pauseMenu.c; do
    ${CC:-gcc} -std=gnu11 -fsyntax-only -include region.h $DEFS $INC -Wall -Wno-unused -Wno-multichar -Wno-pointer-sign \
        -Wno-int-to-pointer-cast -Wno-sign-compare -Werror=implicit-function-declaration "$f"
    echo "PASS compile $f"
done
${CC:-gcc} -std=gnu11 -fsyntax-only -include region.h $DEFS $INC -Ilibs/agbplay_core -Iplatform/3ds/source \
    -Wall -Wextra -Wno-unused -Wno-unused-parameter -Wno-missing-field-initializers port/port_second_screen_theme.c
echo "PASS compile port/port_second_screen_theme.c"

# 3. Behavioural test: English/EU unchanged, Ukrainian layout only with the TMC-UA marker.
${CC:-gcc} -std=gnu11 -O1 -w -include region.h $DEFS $INC ua/test_gameover.c src/gameOverTask.c \
    -o "$OUT/test_gameover" -Wl,--unresolved-symbols=ignore-all 2>/dev/null
"$OUT/test_gameover"

# 4. Panel text: English labels become Ukrainian font codes only for the UA ROM.
${CC:-gcc} -std=gnu11 -O1 -w -include region.h $DEFS $INC ua/test_panel_text.c -o "$OUT/test_panel_text"
printf 'BACK\nPAGE 2 OF 5\nCHANNEL: STABLE\nNEW 3DS\n' | "$OUT/test_panel_text" big > "$OUT/big.txt"
printf 'Version v2.1\n' | "$OUT/test_panel_text" small > "$OUT/small.txt"
grep -q '^BACK	52414a4146$' "$OUT/big.txt" || fail "BACK should encode as НАЗАД (52414a4146), got: $(grep '^BACK' "$OUT/big.txt")"
grep -q '^PAGE 2 OF 5	565753552e2032204a2035$' "$OUT/big.txt" || fail "PAGE 2 OF 5 should encode as СТОР. 2 З 5"
grep -q '^CHANNEL: STABLE	4f415241503a20565741424c50c7524b4e$' "$OUT/big.txt" || fail "CHANNEL: STABLE word translation broke"
grep -q '^NEW 3DS	5253434120334656$' "$OUT/big.txt" || fail "NEW 3DS whole-string translation broke"
grep -q '^Version v2.1	8c67f4f66c737420f9322e31$' "$OUT/small.txt" || fail "Latin fallback for the message font broke"
printf 'BACK\n' | "$OUT/test_panel_text" big off | grep -q '^BACK	4241434b$' || fail "non-UA ROM must leave panel text untouched"
echo "PASS panel text translation/encoding"
echo "All tloz-tmc-ua checks passed."
