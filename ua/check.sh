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
grep -q 'Port_UA_SplashPath' platform/3ds/source/platform_3ds.c || fail "platform_3ds.c lost the Port_UA_SplashPath() splash hook"
grep -qF 'Update_FormatNotesUtf8(body,&formatted[0][0],UPDATE_LINE' platform/3ds/source/update_ui_3ds.inc || fail "update_ui_3ds.inc lost the Cyrillic changelog hook"
grep -q 'UpdateBodyText(&s,lines\[i\]' platform/3ds/source/update_ui_3ds.inc || fail "update_ui_3ds.inc lost the message-font changelog hook"
grep -q 'Port_UA_QuestTabExtra' port/port_second_screen.c || fail "port_second_screen.c lost the wider СТАТИСТИКА tab hook"
grep -q 'splash-ua.rgb565' platform/3ds/CMakeLists.txt || fail "platform/3ds/CMakeLists.txt no longer copies romfs/splash-ua.rgb565"
[ "$(stat -c %s platform/3ds/romfs/splash-ua.rgb565)" = "192000" ] || fail "romfs/splash-ua.rgb565 must be 400x240 RGB565 (192000 bytes); run ua/make_splash.py"

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
grep -q '^PAGE 2 OF 5	565753552e2032204c4a2035$' "$OUT/big.txt" || fail "PAGE 2 OF 5 should encode as СТОР. 2 ІЗ 5"
grep -q '^CHANNEL: STABLE	4f415241503a20565741424c50c7524b4e$' "$OUT/big.txt" || fail "CHANNEL: STABLE word translation broke"
grep -q '^NEW 3DS	5253434120334656$' "$OUT/big.txt" || fail "NEW 3DS whole-string translation broke"
grep -q '^Version v2.1	8c67f4f66c737420f9322e31$' "$OUT/small.txt" || fail "Latin fallback for the message font broke"
printf 'BACK\n' | "$OUT/test_panel_text" big off | grep -q '^BACK	4241434b$' || fail "non-UA ROM must leave panel text untouched"
printf '«Кінець»\n' | "$OUT/test_panel_text" small | grep -q '	22' || fail "« » must encode as a plain double quote"
echo "PASS panel text translation/encoding"
# 5. Boot splash: only a .gba with the TMC-UA marker selects the Ukrainian logo.
cat > "$OUT/test_splash.c" <<'C'
#include "port_ua_splash.h"
int main(int argc, char** argv) { return Port_UA_FileHasMarker(argv[1]) ? 0 : 1; }
C
${CC:-gcc} -std=gnu11 -Wall -Werror -Iport "$OUT/test_splash.c" -o "$OUT/test_splash"
python3 -c 'import sys; open(sys.argv[1], "wb").write(b"\xff" * 0x1000000)' "$OUT/retail.gba"
python3 -c 'import sys; d = bytearray(b"\xff" * 0x1000000); d[0xFFFFF0:0xFFFFF8] = b"TMC-UA\x00\x01"; open(sys.argv[1], "wb").write(d)' "$OUT/ua.gba"
"$OUT/test_splash" "$OUT/ua.gba" || fail "Ukrainian ROM marker not detected for the splash"
! "$OUT/test_splash" "$OUT/retail.gba" || fail "retail ROM must keep the upstream splash"
! "$OUT/test_splash" "$OUT/missing.gba" || fail "missing ROM must keep the upstream splash"
echo "PASS boot splash selection"
# 6. Changelog: Cyrillic survives only in UTF-8 mode, and wrapping never splits a letter.
if echo '#include <jansson.h>' | ${CC:-gcc} -E -x c - >/dev/null 2>&1; then
    cat > "$OUT/test_notes.c" <<'C'
#include <stdio.h>
#include <string.h>
#include "update_manifest.h"
int main(void) {
    static char lines[16][43], wide[8][129];
    const char* ua = "Українська збірка порту на основі офіційної версії";
    unsigned n = Update_FormatNotes(ua, lines, 16);
    for (unsigned i = 0; i < n; i++)
        for (const char* c = lines[i]; *c; c++) if ((unsigned char)*c >= 0x80) return puts("ASCII mode must drop Cyrillic"), 1;
    n = Update_FormatNotesUtf8(ua, &lines[0][0], 43, 42, 16, true);
    char joined[256] = "";
    for (unsigned i = 0; i < n; i++) {
        size_t len = strlen(lines[i]);
        if (!len || (lines[i][len - 1] & 0xC0) == 0xC0 || (lines[i][0] & 0xC0) == 0x80) return puts("split UTF-8"), 1;
        if (i) strcat(joined, " ");
        strcat(joined, lines[i]);
    }
    if (strcmp(joined, ua)) return printf("lost text: %s\n", joined), 1;
    n = Update_FormatNotesUtf8("ААААААААААААААААААААААААААААААААААААААААААААААААА", &lines[0][0], 43, 42, 16, true);
    for (unsigned i = 0; i < n; i++) if (strlen(lines[i]) % 2) return puts("split UTF-8 without spaces"), 1;
    if (Update_FormatNotesUtf8(ua, &wide[0][0], 129, 128, 8, true) != 1 || strcmp(wide[0], ua))
        return puts("wide rows must hold a whole Cyrillic line"), 1;
    return 0;
}
C
    ${CC:-gcc} -std=gnu11 -Wall -Werror -Iplatform/3ds/source "$OUT/test_notes.c" platform/3ds/source/update_manifest.c \
        -ljansson -o "$OUT/test_notes"
    "$OUT/test_notes" || fail "Cyrillic changelog formatting broke"
    echo "PASS changelog UTF-8 formatting"
else
    echo "SKIP changelog UTF-8 formatting (no jansson headers: apt install libjansson-dev)"
fi
echo "All tloz-tmc-ua checks passed."
