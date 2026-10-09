/**
 * @file port_ua.h
 * @brief Ukrainian localization (tloz-tmc-ua) hooks for the native port.
 *
 * The Ukrainian translation is distributed as a modified USA ROM (game code
 * BZME) whose data layout is byte-identical to the retail USA ROM. Dialogue,
 * the font, the title logo and the GAME OVER letter art are all ROM data, so the
 * port picks them up through its normal ROM/asset pipeline with no code changes.
 *
 * Only behaviour that lives in *code* in the original game needs a hook here.
 * Every hook is gated on Port_IsUkrainianRom(), so English/EU/JP ROMs run
 * exactly the upstream code paths.
 *
 * ROM marker (written by tloz-tmc-ua/build-port-rom.py):
 *   offset 0xFFFFF0, 8 bytes: "TMC-UA" '\0' <format version>
 * The area is 0xFF padding in the retail ROM and is never read by the game.
 *
 * Keep all Ukrainian-specific logic in this header so upstream merges only
 * ever touch the small, clearly marked call sites in src/.
 */
#ifndef PORT_UA_H
#define PORT_UA_H

#ifdef PC_PORT

#include <string.h>

#include "gba/types.h"
#include "port_rom.h"

#include "port_ua_marker.h"

/** True when the loaded ROM is the tloz-tmc-ua Ukrainian build. */
static inline bool32 Port_IsUkrainianRom(void) {
    return gRomData != NULL && gRomSize >= PORT_UA_MARKER_OFFSET + PORT_UA_MARKER_LEN &&
           memcmp(gRomData + PORT_UA_MARKER_OFFSET, PORT_UA_MARKER, PORT_UA_MARKER_LEN) == 0;
}

/**
 * Bottom tab bar, see port/port_second_screen.c PaintTabBar().
 *
 * The QUEST tab reads СТАТИСТИКА (as the game's own pause menu), which is
 * 96 px in the banner font — wider than an equal third of the bar (~89 px of
 * label room). Borrow 30u (10 px on 3DS) from the MAP tab (МАПА, 43 px).
 */
static inline float Port_UA_QuestTabExtra(float u) {
    return Port_IsUkrainianRom() ? 30.0f * u : 0.0f;
}

/**
 * Red/dark name chips (panel headers, settings values), see
 * port/port_second_screen.c DrawPanelHeaderChip() / DrawSettingsValueRow().
 * The Ukrainian banner letters are wider than the English ones and taller
 * (ink rows 1..15 instead of 2..14), so they ran over the chip's white inner
 * ring. Header chips get 6u (2 px on 3DS) more on each side and 2 px more
 * above and below; settings-value chips sit in rows too short for the ring,
 * so they are drawn as a flat rounded fill without it.
 */
static inline float Port_UA_ChipPad(float u) {
    return Port_IsUkrainianRom() ? 6.0f * u : 0.0f;
}

/**
 * GAME OVER screen ("КІНЕЦЬ ГРИ"), see src/gameOverTask.c DrawGameOverText().
 *
 * The Ukrainian ROM redraws the eight letter sprites of "GAME OVER" (sprite
 * 0x1fd, frames 0..7) as the Ukrainian phrase. It needs different x positions,
 * and frames 3 and 6 carry no letter and must not be drawn.
 * Mirrors tloz-tmc-ua/tmc/src/gameOverTask.c.
 *
 * @param i  letter slot 0..7
 * @param x  in: upstream x position, out: x position to use
 * @return   FALSE when this slot must be skipped
 */
static inline bool32 Port_UA_GameOverLetter(u32 i, s16* x) {
    static const u8 sUaOffsets[] = {
        40, 72, 104, 108, 136, 168, 174, 200,
    };

    if (!Port_IsUkrainianRom() || i >= sizeof(sUaOffsets)) {
        return TRUE;
    }
    if (i == 3 || i == 6) {
        return FALSE;
    }
    *x = sUaOffsets[i];
    return TRUE;
}

/**
 * Kinstone fusion menu, see src/menu/kinstoneMenu.c KinstoneMenu_080A4494():
 * the Ukrainian ROM prints "Лінк" instead of the save-file name (the name
 * entry screen has no Cyrillic). Bytes are Ukrainian font codes.
 * Mirrors tloz-tmc-ua/tmc/src/menu/kinstoneMenu.c.
 */
static inline u8* Port_UA_KinstoneFuserName(u8* saveName) {
    static u8 sUaLink[] = { 0x50, 0x6c, 0x72, 0x6f, ' ', ' ', 0 }; /* "Лінк  " */
    return Port_IsUkrainianRom() ? sUaLink : saveName;
}

/**
 * Pause menu, kinstone pieces screen (src/menu/pauseMenu.c sub_080A5128):
 * the Ukrainian header "УЛАМКИ ДИВОКАМЕНІВ" is wider, so it starts further right.
 * Mirrors tloz-tmc-ua/tmc/src/menu/pauseMenu.c.
 */
static inline int Port_UA_KinstoneHeaderX(int upstreamX) {
    return Port_IsUkrainianRom() ? 0x66 : upstreamX;
}

#endif /* PC_PORT */

#endif /* PORT_UA_H */
