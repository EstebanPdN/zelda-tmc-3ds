#include "port_second_screen_render.h"

#include "port_gba_mem.h"
#include "port_rom.h"
#include "port_second_screen_theme.h"
#include "sprite.h"
#include "structures.h"

#include <stdio.h>

/*
 * Item icons, decoded the way PauseMenu_ItemMenu_Draw actually draws them:
 * gSpriteAnimations_322[item] picks the sprite-322 frame, whose OBJ piece
 * list (gFrameObjLists via sub_080AD8F0) places one 16x16 body piece and,
 * for some items, small overlay pieces (ammo digits, element glows). Each
 * piece carries its own OBJ palette bank in the 5th byte — the engine's
 * attr2 math (arm sub_080B2874 / RenderSpritePieces in port_draw.c) adds
 * the piece bank to the OAM command's bank unless the piece's bit0 clears
 * the command bank first. Every item-icon piece sets bit0, which is why
 * the icons ignore the menu's `color = 3/4` command bank and always
 * resolve their own banks (body pieces bank 0/1/4, digit pieces bank 4).
 * Bank contents come from the palette-group state the pause menu keeps
 * loaded, via the theme's shared resolver
 * (Port_SecondScreenTheme_ObjPalette: group 182 over 181 over 11).
 *
 * Tiles: the menu DMAs 8 tiles per slot from the sprite-322 sheet
 * (gMoreSpritePtrs[2]) at frames[frame].firstTileIndex (sub_080A5F48) and
 * the pieces index tiles 0..7 relative to that slot — so here the piece
 * tile bits index the sheet relative to firstTileIndex directly.
 *
 * Placement: the OAM size table's sub-table 0 (the pause menu draws with
 * flags 0x400) has zero anchors, so a piece lands at command position +
 * its signed offset. The 16x16 body piece sits at (-8, -13), meaning the
 * caller's (x, y) box origin maps to command position (x + 8, y + 13) —
 * overlay pieces keep their exact in-game offsets relative to that.
 */

/* src/affine.c — frame OBJ piece list for (sprite, frame); PC path is
 * bounds-checked and returns NULL when out of range. */
extern void* sub_080AD8F0(u32 sprite, u32 frame);

/* Mirrors src/menu/pauseMenu.c's GetSpriteAnimation322 (static there, so not
 * directly callable) — gSpriteAnimations_322 is a plain linked global. */
extern Frame* gSpriteAnimations_322[];
#define SPRITE_ANIM_322_COUNT 128

#define ITEM_SPRITE 322u /* decimal, matches gSpriteAnimations_322's naming — NOT 0x322 */

/* Piece offsets are relative to the game's OAM command position for a
 * slot; the body piece's fixed offset maps that position into the
 * caller's 16x16 box. */
#define PIECE_ORIGIN_X 8
#define PIECE_ORIGIN_Y 13

/* Palette bank PauseMenu_ItemMenu_Draw's OAM command carries (its
 * `color = 3`), the default for callers drawing the item screen's grid. */
#define ITEM_SCREEN_CMD_PAL_BANK 3u

/* Standard GBA OBJ dimensions by (shape, size) — hardware constants. */
static const u8 kObjW[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 kObjH[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

/* Draws one OBJ piece (1D-mapped consecutive tiles, like the game's
 * DISPCNT bit 6 mode) scaled into the destination buffer. */
static void DrawPiece(uint32_t* pixels, int32_t bufWidth, int32_t bufHeight, int32_t stride, int32_t boxX,
                      int32_t boxY, int32_t scale, const uint8_t* tiles, const uint16_t* pal16, int32_t pw,
                      int32_t ph, int hflip, int vflip) {
    int32_t wTiles = pw / 8;
    for (int32_t ty = 0; ty < ph / 8; ty++) {
        for (int32_t tx = 0; tx < wTiles; tx++) {
            const uint8_t* tile = tiles + (ty * wTiles + tx) * 32;
            for (int32_t py = 0; py < 8; py++) {
                for (int32_t px = 0; px < 8; px++) {
                    uint8_t packed = tile[py * 4 + px / 2];
                    uint8_t colorIndex = (px & 1) ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0Fu);
                    if (colorIndex == 0) {
                        continue; /* 0 == transparent */
                    }
                    uint32_t rgba = Rgb555ToRgba8888(pal16[colorIndex]);
                    int32_t srcX = tx * 8 + px;
                    int32_t srcY = ty * 8 + py;
                    if (hflip) {
                        srcX = pw - 1 - srcX;
                    }
                    if (vflip) {
                        srcY = ph - 1 - srcY;
                    }
                    /* Nearest-neighbor upscale: each source pixel becomes
                     * a scale x scale block, matching the GBA's own
                     * blocky look instead of smoothing icons into mush. */
                    for (int32_t sy = 0; sy < scale; sy++) {
                        int32_t destY = boxY + srcY * scale + sy;
                        if (destY < 0 || destY >= bufHeight) {
                            continue;
                        }
                        for (int32_t sx = 0; sx < scale; sx++) {
                            int32_t destX = boxX + srcX * scale + sx;
                            if (destX < 0 || destX >= bufWidth) {
                                continue;
                            }
                            pixels[(size_t)destY * (size_t)stride + (size_t)destX] = rgba;
                        }
                    }
                }
            }
        }
    }
}

void Port_SecondScreenRender_DrawItemIcon(uint32_t* pixels, int32_t bufWidth, int32_t bufHeight, int32_t stride,
                                           int32_t x, int32_t y, int32_t scale, uint8_t itemId) {
    Port_SecondScreenRender_DrawItemIconBank(pixels, bufWidth, bufHeight, stride, x, y, scale, itemId,
                                             ITEM_SCREEN_CMD_PAL_BANK);
}

void Port_SecondScreenRender_DrawItemIconBank(uint32_t* pixels, int32_t bufWidth, int32_t bufHeight,
                                              int32_t stride, int32_t x, int32_t y, int32_t scale,
                                              uint8_t itemId, uint32_t cmdPalBank) {
    const u16 itemSpriteIndex = Port_RemapSpriteIndex(ITEM_SPRITE);
    if (scale < 1) {
        scale = 1;
    }
    if (itemId == 0 || itemId >= SPRITE_ANIM_322_COUNT) {
        return;
    }

    Frame* animation = (Frame*)port_resolve_addr((uintptr_t)gSpriteAnimations_322[itemId]);
    if (animation == NULL) {
        return;
    }

    const SpritePtr* sprite = Port_GetSpritePtr(itemSpriteIndex);
    if (sprite == NULL || sprite->frames == NULL || sprite->ptr == NULL) {
        fprintf(stderr, "[second_screen_render] itemId %u: sprite=%p frames=%p ptr=%p\n", itemId, (void*)sprite,
                sprite ? (void*)sprite->frames : NULL, sprite ? sprite->ptr : NULL);
        return;
    }

    const SpriteFrame* frame = &sprite->frames[animation->index];
    if (frame->firstTileIndex >= 0x4000) {
        return; /* same sheet-bounds guard sub_080A5F48 applies */
    }
    const uint8_t* slotTiles = (const uint8_t*)sprite->ptr + (size_t)frame->firstTileIndex * 32u;

    const uint8_t* frameData = (const uint8_t*)sub_080AD8F0(itemSpriteIndex, animation->index);
    if (frameData == NULL) {
        return;
    }
    uint32_t count = frameData[0];
    if (count == 0 || count > 8) {
        return;
    }

    /* Reverse piece order: OAM entry 0 is topmost on hardware, so the
     * last piece is painted first and the first wins overlaps. */
    for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
        const uint8_t* p = frameData + 1 + i * 5;
        uint32_t shape = (p[2] >> 6) & 3;
        uint32_t size = (p[2] >> 4) & 3;
        int hflip = (p[2] & 0x04) != 0;
        int vflip = (p[2] & 0x08) != 0;
        uint32_t tileIdx = (uint32_t)p[3] + (((uint32_t)p[4] & 3u) << 8);
        /* Piece bank, per RenderSpritePieces' attr2 math: bit0 drops the
         * OAM command's bank so the piece's own is absolute, and almost
         * every icon piece sets it. The elements' crystal is the one that
         * does not, which is why the command bank is a parameter. */
        uint32_t palBank = ((((uint32_t)p[2] & 1u) ? 0u : cmdPalBank) + ((uint32_t)p[4] >> 4)) & 15u;
        const uint16_t* pal16 = Port_SecondScreenTheme_ObjPalette(palBank);

        if (shape == 3 || pal16 == NULL) {
            continue;
        }
        DrawPiece(pixels, bufWidth, bufHeight, stride, x + ((int32_t)(int8_t)p[0] + PIECE_ORIGIN_X) * scale,
                  y + ((int32_t)(int8_t)p[1] + PIECE_ORIGIN_Y) * scale, scale, slotTiles + tileIdx * 32u, pal16,
                  kObjW[shape][size], kObjH[shape][size], hflip, vflip);
    }
}
