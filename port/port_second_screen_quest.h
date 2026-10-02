#ifndef PORT_SECOND_SCREEN_QUEST_H
#define PORT_SECOND_SCREEN_QUEST_H

/*
 * Quest-status screen for the second screen: the pause menu's own quest
 * screen (elements, kinstones, figurines, dungeon items) rebuilt from ROM
 * at runtime so the panel's tab matches what pressing START shows, rather
 * than paraphrasing it in invented rows.
 *
 * Same contract as the other art modules (see port_second_screen_worldmap.h):
 * ROM-constant data only — every live value arrives through the snapshot the
 * caller hands in, and any not-ready path returns 0 so the compositor can
 * fall back and retry next frame.
 */

#include <stdint.h>

#include "port_second_screen_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A rect in the destination buffer's own pixels. w == 0 means "not showing",
 * which is how a caller tells an absent well from one at the origin. */
typedef struct {
    int32_t x, y, w, h;
} SecondScreenQuestRect;

/* The wells on the main screen that open a list of their own, reported so
 * the compositor can hang tap targets on them without duplicating the
 * layout. The pause menu opens the same two with A on the same two slots. */
typedef struct {
    SecondScreenQuestRect bag;   /* -> the kinstone pieces list */
    SecondScreenQuestRect skill; /* -> the sword techniques list */
} SecondScreenQuestHotspots;

/* Draws the quest-status screen fitted into the destination rect of an
 * RGBA8888 buffer (stride in pixels), scaled nearest-neighbor. `tick` drives
 * whatever the real screen animates. `outHot` may be NULL; when given it
 * receives the tappable wells' rects. Returns 1 when the authentic screen was
 * drawn, 0 while its ROM data isn't decodable yet. */
int Port_SecondScreenQuest_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                const SecondScreenSnapshot* snap, uint32_t tick,
                                SecondScreenQuestHotspots* outHot);

/* The two lists the quest screen opens, drawn into the same kind of rect:
 * KINSTONE PIECES (pause screen 7) — every kinstone type in the bag with its
 * count — and SWORD TECHNIQUES (pause screen 8) — all eight scrolls, the
 * learned ones in their own colours and the rest as empty wells.
 *
 * Both are read-only overviews: the pause menu lets a cursor walk them to
 * read a description, which needs the message engine and a keypad this panel
 * has neither of, so what crosses over is the inventory each one shows.
 *
 * Return 1 when drawn, 0 while their ROM data isn't decodable — the caller
 * keeps the main screen up rather than showing an empty slab. */
int Port_SecondScreenQuest_DrawKinstones(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                         const SecondScreenSnapshot* snap);

int Port_SecondScreenQuest_DrawTechniques(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                          int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                          const SecondScreenSnapshot* snap);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_QUEST_H */
