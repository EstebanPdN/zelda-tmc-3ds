#include "port_second_screen_quest.h"

#include "item.h"
#include "itemMetaData.h"
#include "kinstone.h" /* KinstoneWorldEvent: a piece's OBJ palette and tile block */
#include "subtask.h"  /* gUnk_08128D70: the sword techniques' own table */
#include "port_rom.h"
#include "port_second_screen_render.h"
#include "port_second_screen_theme.h"
#include "region.h"

#include <string.h>

/*
 * The pause menu's QUEST STATUS screen, rebuilt from ROM so the panel's tab
 * is the screen pressing START shows rather than a paraphrase of it.
 *
 * Which screen, exactly: PauseMenuScreen_2 (src/menu/pauseMenu.c's
 * PauseMenu2 — the one src/menu/pauseMenu.c:123 calls "the QUEST STATUS
 * screen"). gUnk_08128A38[2].unk0 = 1 selects gUnk_08128AD8[1] =
 * { paletteGroup 183, gfxGroup 91, dispcnt 0x400, bg1Control 0x1C05,
 * bg2Control 0x1D03 } (src/data/figurineMenuData.c), which sub_080A4DB8
 * applies after sub_080A4D34 has loaded the shared pause chrome. That makes
 * the screen, in the order the GBA composes it:
 *
 *   BG3  parchment backdrop: gfx group 86 tiles @ 0x06008000 (charBase 2)
 *        plus group 86's EWRAM tilemap. NOT rebuilt here — the panel already
 *        paints the same art through Port_SecondScreenTheme_DrawBackdrop,
 *        which stamps the doodle lattice continuously instead of restarting
 *        it at every screen edge.
 *   BG2  the carved slab with the quest wells: group 91 tiles @ 0x06000000
 *        (charBase 0) plus group 91's EWRAM tilemap copy (gBG2Buffer).
 *   OBJ  sub_080A5128 draws the title banner and the tab arrows (sprite
 *        SPRITE_PAUSE_MISC frames 0/1/2); sub_080A57F4 draws one frame per
 *        occupied slot of gUnk_08128C94 (gUnk_08128C14 on JP), the slots
 *        being filled by sub_080A5594.
 *   BG1  off on this screen (dispcnt bit 9 clear); BG0 is the menu's live
 *        text layer, which a passive panel has nothing to put in.
 *
 *   Palettes load 11, 12 (LoadGfxGroups), 181 (sub_080A4D34), 183
 *   (sub_080A4DB8) — later loads win, like the shared gPaletteBuffer.
 *
 * Static vs live. Everything that does not depend on the save — slab, banner,
 * arrows, and the two always-present SLEEP / SAVE plates (sub_080A5594 fills
 * their slots through gGenericMenu.unk14/unk15, which alias slots 4 and 5) —
 * composes ONCE into a private 240x160 RGBA layer that is published only when
 * complete. Per call that layer is copied, the save-dependent slots are
 * stamped on top at their own table positions, and the result is scaled into
 * the caller's rect.
 *
 * What the snapshot fills. sub_080A5594 fills sixteen slots, and every one
 * of them is reachable from the published snapshot:
 *   slot 0     kinstone bag tier, or the Tingle trophy once it replaces the
 *              bag in that well   <- tingleTrophy / kinstoneBagOwned / kinstoneBag
 *   slot 1     heart pieces       <- heartPieces (drawn as heartPieces + 1)
 *   slot 2     sword techniques   <- swordSkills, one fixed frame plus the count
 *   slot 3     Carlov medal, else the shell counter <- carlovMedal / shellsOwned / shells
 *   slots 4-5  SLEEP / SAVE plates — save-independent, part of the static layer
 *   slots 6-8  the carried quest items <- questItems[], already in the order
 *              sub_080A5594's rolling counter drops them into the tray
 *   slots 9-12 four elements     <- elements
 *   slots 13-15 grip ring, bracelets, flippers <- passives
 * Everything but the tray goes to its well through the same
 * gItemMetaData[item].menuSlot the menu uses; the tray's three items share
 * one menu slot, so their order is the snapshot's, not the metadata's.
 *
 * Deliberately absent: the blinking slot cursor. It marks the pause menu's
 * selection, and a panel that cannot be navigated has none.
 *
 * Threading: the layer is built lazily by whoever draws first — by design
 * only the second-screen render thread — into a private buffer that is
 * published through one pointer-sized store once complete, and immutable
 * afterwards. Same contract (and same caveat about adding a second reader
 * thread) as port_second_screen_worldmap.c's image.
 */

/* src/common.c (appended accessors — ROM-const reads only), the same faces
 * the other second-screen art modules decode through. */
extern const u8* Port_ResolveGfxGroupVram(u32 group, u32 vramAddr, u32* outAvail);
extern const u8* Port_GetRawPaletteGroupEntryData(u32 group, u32 entryIdx, u32* outNumColors,
                                                  u32* outDestPaletteNum);
extern const u8* Port_GetRawPaletteGroupBankData(u32 group, u32 destPaletteNum, u32* outNumColors);
extern u32 Port_GetKinstonePieceTiles(u32 gfxOffset, u8* out, u32 outBytes);

/* src/affine.c — frame OBJ piece list for (sprite, frame). */
extern void* sub_080AD8F0(u32 sprite, u32 frame);

/* Resolved ROM group table (port_rom.c) — for the EWRAM-destined tilemap
 * record, which Port_ResolveGfxGroupVram's VRAM remit does not cover. */
extern const void* gGfxGroups[];
extern const u8* gGlobalGfxAndPalettes;

/* Quest-screen slot table (data/const/subtask.s): 16 entries of
 * { up, down, left, right, cursorFrameBase, itemFrameBase, x, y }.
 * gUnk_08128C14 is the language-0 table and gUnk_08128C94 every other
 * language's; sub_080A57F4 picks between them on gSaveHeader->language, which
 * is save state — the ROM-const equivalent is the region, since language 0
 * only exists on the JP cart. */
extern const u8 gUnk_08128C14[];
extern const u8 gUnk_08128C94[];

#define QUEST_W 240 /* GBA LCD: menu screens always compose the native canvas */
#define QUEST_H 160

#define QUEST_GFX_GROUP 91u          /* pause screen 2's own tiles + tilemap */
#define QUEST_TILES_DEST 0x06000000u /* bg2Control 0x1D03 -> charBase 0 */
#define OBJ_VRAM_BASE 0x06010000u

/* Palette groups applied on the way into the quest screen, in load order. */
static const u8 kQuestPaletteGroups[] = { 11u, 12u, 181u, 183u };
/* OBJ VRAM state at that point, latest load first. */
static const u8 kQuestVramGroups[] = { 91u, 86u, 23u, 16u };

/* Where one OBJ frame's tiles and colours come from. The quest screen and
 * the two lists it opens are different pause screens and each loads its own
 * groups (gUnk_08128AD8, indexed by the screen's entry in gUnk_08128A38), so
 * the renderer takes the load state rather than assuming the quest one.
 * paletteGroup 0 means "ask the theme", which walks the pause menu's own
 * palette chain — right for every screen that doesn't replace it. */
typedef struct {
    const u8* vramGroups;
    int vramGroupCount;
    u32 paletteGroup;
    /* An override tile source: the kinstone list streams a piece's tiles into
     * OBJ VRAM per row rather than loading them with a group, so its frames
     * are drawn against this buffer instead of a group lookup. NULL when the
     * groups above are the whole story. */
    const u8* tiles;
    u32 tilesLen;
} ObjSource;

static const ObjSource kQuestSource = { kQuestVramGroups, (int)sizeof(kQuestVramGroups), 0u, NULL,
                                        0u };

/* Pause screen 8 (SWORD TECHNIQUES) and 7 (KINSTONE PIECES): gUnk_08128AD8
 * rows 6 and 5 — palette groups 205 / 204 over gfx groups 126 / 125, with
 * the shared chrome groups still underneath. */
static const u8 kTechVramGroups[] = { 126u, 86u, 23u, 16u };
static const u8 kKinstoneVramGroups[] = { 125u, 86u, 23u, 16u };
static const ObjSource kTechSource = { kTechVramGroups, (int)sizeof(kTechVramGroups), 205u, NULL,
                                       0u };

#define SPRITE_PAUSE_MISC (REGION_IS_EU ? 0x1FAu : 0x1FBu)

/* sub_080A5128's fixed draws for a normal screen (its `default` arm): the
 * title banner at (0x40, 0x10) and the tab arrows at (0x10, 0x48) and
 * (0xE0, 0x48), all with gOamCmd._8 = 0x400. */
#define BANNER_X 0x40
#define BANNER_Y 0x10
#define ARROW_L_X 0x10
#define ARROW_R_X 0xE0
#define ARROW_Y 0x48
#define CHROME_OAM_EXTRA 0x400u

/* sub_080A57F4's slot draws: gOamCmd._8 = 0xE800 for the frames that come
 * straight out of OBJ VRAM (the counters and the two button plates). The
 * item icons get slot * 8 + 0xEB80 instead — a per-slot tile base this
 * module has no use for (it reads the sprite sheet directly, like the DMA
 * that fills those tiles does), but the same palette bank 14, which is the
 * one the element crystal's piece keeps. */
#define SLOT_OAM_EXTRA 0xE800u
#define ITEM_CMD_PAL_BANK 0xEu

#define SLOT_KINSTONE_BAG 0
#define SLOT_HEART_PIECES 1
#define SLOT_SWORD_SKILLS 2
#define SLOT_SHELLS 3
#define SLOT_SLEEP 4
#define SLOT_SAVE 5
#define SLOT_QUEST_ITEM 6 /* 6, 7, 8 — sub_080A5594's carried-item tray */
#define QUEST_TRAY_SLOTS 3
#define SLOT_BUTTON_VALUE 1 /* both button slots hold 1 */
#define QUEST_SLOT_COUNT 16

/* Frame id for a slot's contents, per sub_080A57F4: value + 9 + unk5. The
 * technique slot is the one exception — it draws one fixed frame whatever
 * the count is, and prints the count beside it as a digit. */
#define SLOT_FRAME(entry, value) ((u32)(value) + 9u + (u32)(entry)[5])
#define SKILL_FRAME(entry) ((u32)(entry)[5] + 10u)

/* The counter digits: DrawDirect(0, 1) with gOamCmd._8 = digit + 0x800, the
 * glyphs being consecutive tiles from the frame's own base. Both counters
 * sit at a fixed offset from their slot's table position, and the shell one
 * prints three digits right to left, leading zeros included. */
#define DIGIT_SPRITE 0u
#define DIGIT_FRAME 1u
#define DIGIT_OAM_EXTRA 0x800u
#define SKILL_DIGIT_DX 9
#define SKILL_DIGIT_DY 7
#define SHELL_DIGIT_DX 8
#define SHELL_DIGIT_DY 8
#define SHELL_DIGIT_COUNT 3

/* sub_080A57F4 nudges two of the item icons down inside their well. */
#define TROPHY_ICON_DY 0xD
#define MEDAL_ICON_DY 8

/* The icon renderer maps a 16x16 box origin onto the game's OAM command
 * position: the body piece sits at (-8, -13) of it. */
#define ICON_BOX_DX 8
#define ICON_BOX_DY 13

/* Standard GBA OBJ dimensions by (shape, size) — hardware constants. */
static const u8 kObjW[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 kObjH[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

static uint32_t sStatic[QUEST_W * QUEST_H];
static uint32_t sFrame[QUEST_W * QUEST_H];
static const uint32_t* volatile sPublished = NULL;

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static const u8* SlotEntry(int slot) {
    const u8* table = REGION_IS_JP ? gUnk_08128C14 : gUnk_08128C94;
    if (table == NULL || slot < 0 || slot >= QUEST_SLOT_COUNT) {
        return NULL;
    }
    return table + (size_t)slot * 8u;
}

/* First EWRAM-destined record of a gfx group — the menu screens stage their
 * BG tilemaps through buffers whose absolute address is a per-region link
 * detail, so the record is matched by destination region (the same walk
 * port_second_screen_worldmap.c does for the map screen). */
static const u8* QuestTilemap(u32 group, u32* outLen) {
    const u8* rec;
    int i;

    *outLen = 0;
    if (group >= 133u || gGlobalGfxAndPalettes == NULL) { /* GFX_GROUPS_COUNT_MAX */
        return NULL;
    }
    rec = (const u8*)gGfxGroups[group];
    if (rec == NULL) {
        return NULL;
    }
    for (i = 0; i < 32; i++, rec += 12) {
        u32 raw0 = Port_ReadU32(rec);
        u32 dest = Port_ReadU32(rec + 4);
        s32 size = (s32)Port_ReadU32(rec + 8);
        if (((raw0 >> 24) & 0xFu) == 0xDu) {
            return NULL;
        }
        if ((dest >> 24) == 0x02u && size > 0) {
            *outLen = (u32)size;
            return gGlobalGfxAndPalettes + (raw0 & 0xFFFFFFu);
        }
        if (((raw0 >> 24) & 0x80u) == 0) {
            return NULL;
        }
    }
    return NULL;
}

/* LoadPaletteGroup's effect on the BG half of gPaletteBuffer. */
static void ApplyPaletteGroup(u32 group, uint16_t* bgPal) {
    u32 e;
    for (e = 0; e < 8; e++) {
        u32 numColors = 0, destBank = 0, c;
        const u8* p = Port_GetRawPaletteGroupEntryData(group, e, &numColors, &destBank);
        if (p == NULL) {
            return;
        }
        if (destBank >= 16) {
            continue; /* OBJ bank — the BG layer only wants the BG half */
        }
        for (c = 0; c < numColors && destBank * 16u + c < 256u; c++) {
            bgPal[destBank * 16u + c] = (uint16_t)(p[c * 2] | (p[c * 2 + 1] << 8));
        }
    }
}

/* Paints the 32x20 BG2 tilemap into the layer. Color 0 stays transparent, so
 * the caller's backdrop shows through the slab's rounded outline. */
static void DrawBgLayer(const u8* tilemap, u32 mapLen, const u8* tiles, u32 tilesLen,
                        const uint16_t* bgPal) {
    u32 tileCount = tilesLen / 32u;
    int32_t x, y;
    for (y = 0; y < QUEST_H; y++) {
        u32 tileRow = ((u32)y >> 3) & 31u;
        for (x = 0; x < QUEST_W; x++) {
            u32 tileCol = ((u32)x >> 3) & 31u;
            u32 entryOff = (tileRow * 32u + tileCol) * 2u;
            u16 entry = (entryOff + 2u <= mapLen) ? Port_ReadU16(tilemap + entryOff) : 0;
            u32 tileId = entry & 0x3FFu;
            int32_t inX = x & 7, inY = y & 7;
            u8 packed, colorIndex;
            if (tileId >= tileCount) {
                continue;
            }
            if (entry & 0x400u) inX = 7 - inX;
            if (entry & 0x800u) inY = 7 - inY;
            packed = tiles[tileId * 32u + (u32)inY * 4u + ((u32)inX >> 1)];
            colorIndex = (inX & 1) ? (u8)(packed >> 4) : (u8)(packed & 0xFu);
            if (colorIndex == 0) {
                continue;
            }
            sStatic[(size_t)y * QUEST_W + (size_t)x] =
                Rgb555ToRgba8888(bgPal[(((u32)entry >> 12) & 0xFu) * 16u + colorIndex]);
        }
    }
}

/* One OBJ palette bank, from the screen's own palette group when it names
 * one and from the pause menu's loaded chain otherwise. OBJ banks are the
 * upper half of palette RAM, hence the 16. */
static const uint16_t* ObjBank(const ObjSource* src, u32 bank) {
    if (src->paletteGroup != 0u) {
        u32 numColors = 0;
        const u8* p = Port_GetRawPaletteGroupBankData(src->paletteGroup, 16u + (bank & 15u), &numColors);
        if (p != NULL && numColors >= 16) {
            return (const uint16_t*)p;
        }
    }
    return Port_SecondScreenTheme_ObjPalette(bank);
}

/* One DrawDirect frame, tiles resolved out of the gfx groups the screen keeps
 * loaded. Piece format and attr2 math per RenderSpritePieces (port_draw.c);
 * pieces are drawn in reverse so the first (topmost OAM) wins overlaps. */
static void DrawObjFrameFrom(const ObjSource* src, uint32_t* layer, int32_t canvasW, int32_t canvasH,
                             u32 sprite, u32 frame, int32_t cmdX, int32_t cmdY, u32 oamExtra) {
    const u8* frameData = (const u8*)sub_080AD8F0(sprite, frame);
    u32 count, baseTile = oamExtra & 0x3FFu, basePal = (oamExtra >> 12) & 0xFu;
    int32_t i;

    if (frameData == NULL) {
        return;
    }
    count = frameData[0];
    if (count == 0 || count > 16) {
        return;
    }
    for (i = (int32_t)count - 1; i >= 0; i--) {
        const u8* p = frameData + 1 + i * 5;
        u32 shape = (p[2] >> 6) & 3u, size = (p[2] >> 4) & 3u;
        int hflip = (p[2] & 0x04u) != 0, vflip = (p[2] & 0x08u) != 0;
        u32 tileIdx = baseTile + (u32)p[3] + (((u32)p[4] & 3u) << 8);
        u32 palBank = ((((p[2] & 1u) ? 0u : basePal)) + ((u32)p[4] >> 4)) & 15u;
        const uint16_t* pal = ObjBank(src, palBank);
        const u8* tiles = NULL;
        int32_t pw, ph, wTiles, hTiles, tx, ty, yy, xx;
        int g;
        u32 avail = 0;

        if (shape == 3 || pal == NULL) {
            continue;
        }
        pw = kObjW[shape][size];
        ph = kObjH[shape][size];
        wTiles = pw / 8;
        hTiles = ph / 8;
        if (src->tiles != NULL) {
            /* Streamed tiles: the frame's own indices are relative to the
             * block that was streamed, so tile 0 is the start of it. */
            u32 need = (u32)(wTiles * hTiles) * 32u, off = tileIdx * 32u;
            if (off < src->tilesLen && src->tilesLen - off >= need) {
                tiles = src->tiles + off;
            }
        }
        for (g = 0; g < src->vramGroupCount && tiles == NULL; g++) {
            tiles = Port_ResolveGfxGroupVram(src->vramGroups[g], OBJ_VRAM_BASE + tileIdx * 32u, &avail);
            if (tiles != NULL && avail < (u32)(wTiles * hTiles) * 32u) {
                tiles = NULL;
            }
        }
        if (tiles == NULL) {
            continue;
        }
        for (ty = 0; ty < hTiles; ty++) {
            for (tx = 0; tx < wTiles; tx++) {
                const u8* tile = tiles + (ty * wTiles + tx) * 32;
                for (yy = 0; yy < 8; yy++) {
                    for (xx = 0; xx < 8; xx++) {
                        u8 packed = tile[yy * 4 + xx / 2];
                        u8 idx = (xx & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
                        int32_t dx = tx * 8 + xx, dy = ty * 8 + yy;
                        if (idx == 0) {
                            continue;
                        }
                        if (hflip) dx = pw - 1 - dx;
                        if (vflip) dy = ph - 1 - dy;
                        dx += cmdX + (int32_t)(int8_t)p[0];
                        dy += cmdY + (int32_t)(int8_t)p[1];
                        if (dx < 0 || dy < 0 || dx >= canvasW || dy >= canvasH) {
                            continue;
                        }
                        layer[(size_t)dy * (size_t)canvasW + (size_t)dx] = Rgb555ToRgba8888(pal[idx]);
                    }
                }
            }
        }
    }
}

/* The quest screen's own load state, which is what nearly every call wants. */
static void DrawObjFrame(uint32_t* layer, int32_t canvasW, int32_t canvasH, u32 sprite, u32 frame,
                         int32_t cmdX, int32_t cmdY, u32 oamExtra) {
    DrawObjFrameFrom(&kQuestSource, layer, canvasW, canvasH, sprite, frame, cmdX, cmdY, oamExtra);
}


/* ---------------------------------------------------------------------- *
 * Panel layout                                                            *
 *                                                                         *
 * The GBA screen packs sixteen wells into 240x160 around navigation the   *
 * panel has no use for. Reproducing that arrangement on a near-square     *
 * touch panel left it an island of tiny art in a large empty rect, so the *
 * ART is kept and the ARRANGEMENT is not: the same decoded icons, plates  *
 * and fonts, regrouped into labelled sections sized for this screen —     *
 * the idiom the panel's ITEMS tab already uses. The pause menu's own      *
 * SLEEP / SAVE buttons, L/R tab arrows and title banner are dropped with  *
 * the arrangement; they are navigation furniture for a menu you can move  *
 * a cursor around, which this panel is not.                               *
 * ---------------------------------------------------------------------- */

/* Scratch big enough for the largest OBJ frame plus its piece offsets.
 * Elements render here at 1x, then blit scaled into their well — one path
 * for item icons and pause-screen frames alike, so every cell can be sized
 * independently of the art's native size. Single-caller (the second-screen
 * render thread), same contract as the module's other statics. */
#define CELL_SRC 64
#define CELL_ORIGIN 24
static uint32_t sCell[CELL_SRC * CELL_SRC];

typedef struct {
    int32_t x, y, w, h; /* ink bounds inside sCell; w == 0 when nothing drew */
} CellInk;

static CellInk CellBounds(void) {
    CellInk b;
    int32_t x, y, minX = CELL_SRC, minY = CELL_SRC, maxX = -1, maxY = -1;
    for (y = 0; y < CELL_SRC; y++) {
        for (x = 0; x < CELL_SRC; x++) {
            if ((sCell[(size_t)y * CELL_SRC + (size_t)x] >> 24) != 0) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }
    b.x = minX;
    b.y = minY;
    b.w = maxX >= minX ? maxX - minX + 1 : 0;
    b.h = maxY >= minY ? maxY - minY + 1 : 0;
    return b;
}

/* One of the pause screen's own OBJ frames (bag tiers, heart-piece states,
 * the technique scroll) rendered to the scratch. */
static CellInk RenderFrameCell(u32 frame) {
    memset(sCell, 0, sizeof(sCell));
    DrawObjFrame(sCell, CELL_SRC, CELL_SRC, SPRITE_PAUSE_MISC, frame, CELL_ORIGIN, CELL_ORIGIN,
                 SLOT_OAM_EXTRA);
    return CellBounds();
}

/* The technique scroll exactly as the pause screen composes it: the slot's
 * own frame — which already carries the little multiplication sign — with
 * the count stamped beside it as the game's own boxed digit, at the offset
 * sub_080A57F4 uses. Composing both into one scratch cell means the whole
 * "scroll x N" group scales into the well as a unit, so the count grows
 * with the art instead of sitting under it in panel text. */
static CellInk RenderSkillCell(u32 frame, int32_t count) {
    memset(sCell, 0, sizeof(sCell));
    DrawObjFrame(sCell, CELL_SRC, CELL_SRC, SPRITE_PAUSE_MISC, frame, CELL_ORIGIN, CELL_ORIGIN,
                 SLOT_OAM_EXTRA);
    if (count >= 0) {
        /* Frame 1 of sprite 0 is a single digit tile and the digit chooses
         * its own tile: base tile N draws the glyph for N, which is why the
         * game passes the value straight through as the OAM tile base. */
        DrawObjFrame(sCell, CELL_SRC, CELL_SRC, DIGIT_SPRITE, DIGIT_FRAME,
                     CELL_ORIGIN + SKILL_DIGIT_DX, CELL_ORIGIN + SKILL_DIGIT_DY,
                     DIGIT_OAM_EXTRA + (u32)(count % 10));
    }
    return CellBounds();
}

/* An inventory item's own icon, through the same sprite-322 path the item
 * grid uses (bank 14, the bank sub_080A57F4 commands on this screen). */
static CellInk RenderItemCell(u32 item) {
    memset(sCell, 0, sizeof(sCell));
    Port_SecondScreenRender_DrawItemIconBank(sCell, CELL_SRC, CELL_SRC, CELL_SRC, CELL_ORIGIN,
                                             CELL_ORIGIN, 1, (uint8_t)item, ITEM_CMD_PAL_BANK);
    return CellBounds();
}

/* Blits whatever is in the scratch into a well, at the largest integer scale
 * that fits with a margin, centered. Integer only: every piece of this art is
 * one-pixel detail and a fractional step eats whole rows of it. */
static void StampCell(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, CellInk ink,
                      int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    int32_t scale, drawW, drawH, ox, oy, x, y;

    if (ink.w <= 0 || ink.h <= 0 || cw <= 0 || ch <= 0) {
        return;
    }
    scale = cw / ink.w;
    if (ch / ink.h < scale) {
        scale = ch / ink.h;
    }
    if (scale < 1) {
        scale = 1;
    }
    drawW = ink.w * scale;
    drawH = ink.h * scale;
    ox = cx + (cw - drawW) / 2;
    oy = cy + (ch - drawH) / 2;
    for (y = 0; y < drawH; y++) {
        int32_t py = oy + y;
        const uint32_t* row = sCell + (size_t)(ink.y + y / scale) * CELL_SRC;
        if (py < 0 || py >= bufH) {
            continue;
        }
        for (x = 0; x < drawW; x++) {
            int32_t px = ox + x;
            uint32_t c = row[ink.x + x / scale];
            if ((c >> 24) == 0 || px < 0 || px >= bufW) {
                continue;
            }
            pixels[(size_t)py * (size_t)stride + (size_t)px] = c;
        }
    }
}

/* What one well holds: an item icon, one of the screen's own frames, or
 * nothing (an empty well, exactly as the real screen shows an uncollected
 * entry). `count` is drawn under the art when non-negative; `skill` marks
 * the technique well, whose count is composed into the art instead. */
typedef struct {
    u32 item;
    u32 frame;
    int32_t count;
    int skill;
} QuestCell;

static QuestCell CellNone(void) {
    QuestCell c;
    c.item = 0;
    c.frame = 0;
    c.count = -1;
    c.skill = 0;
    return c;
}

static QuestCell CellItem(u32 item) {
    QuestCell c = CellNone();
    c.item = item;
    return c;
}

static QuestCell CellFrame(u32 frame) {
    QuestCell c = CellNone();
    c.frame = frame;
    return c;
}

/* Kinstone bag tier, the ladder sub_080A5594 walks over the bag's contents
 * once the bag itself is owned (without it the well stays empty however many
 * pieces the save happens to hold). */
static u32 KinstoneBagTier(const SecondScreenSnapshot* snap) {
    if (!snap->kinstoneBagOwned) {
        return 0;
    }
    if (snap->kinstoneBag >= 0x50) {
        return 4;
    }
    if (snap->kinstoneBag >= 0x28) {
        return 3;
    }
    if (snap->kinstoneBag >= 10) {
        return 2;
    }
    return 1;
}

/* The collection row, the only cells whose contents need the save's own
 * rules (sub_080A5594's arms, kept intact — the trophy displaces the bag,
 * the medal displaces the shell counter). */
static QuestCell CollectionCell(int idx, const SecondScreenSnapshot* snap) {
    const u8* entry;
    QuestCell c = CellNone();

    switch (idx) {
        case 0: /* kinstone bag, or the trophy once it is won */
            if (snap->tingleTrophy == 1) {
                return CellItem((u32)ITEM_QST_TINGLE_TROPHY);
            }
            if (snap->tingleTrophy == 0) {
                u32 tier = KinstoneBagTier(snap);
                entry = SlotEntry(SLOT_KINSTONE_BAG);
                if (tier != 0 && entry != NULL) {
                    /* No count: the bag's own art already says how full it
                     * is — that is what the four tiers are — and the exact
                     * number belongs on the pieces screen behind it, which
                     * is where the pause menu puts it too. */
                    c = CellFrame(SLOT_FRAME(entry, tier));
                }
            }
            return c;
        case 1: /* heart pieces — its well is never empty */
            entry = SlotEntry(SLOT_HEART_PIECES);
            if (entry != NULL) {
                c = CellFrame(SLOT_FRAME(entry, snap->heartPieces + 1u));
            }
            return c;
        case 2: /* sword techniques, one scroll plus the count */
            entry = SlotEntry(SLOT_SWORD_SKILLS);
            if (entry != NULL && snap->swordSkills != 0) {
                c = CellFrame(SKILL_FRAME(entry));
                c.count = (int32_t)snap->swordSkills;
                c.skill = 1;
            }
            return c;
        default: /* Carlov medal, else the shell count */
            if (snap->carlovMedal == 1) {
                return CellItem((u32)ITEM_QST_CARLOV_MEDAL);
            }
            if (snap->shellsOwned == 1 || (snap->carlovMedal == 0 && snap->shellsOwned != 0)) {
                c = CellItem((u32)ITEM_SHELLS);
                c.count = (int32_t)snap->shells;
            }
            return c;
    }
}

static QuestCell SectionCell(int section, int idx, const SecondScreenSnapshot* snap) {
    switch (section) {
        case 0:
            return (snap->elements & (1u << idx)) ? CellItem((u32)ITEM_EARTH_ELEMENT + (u32)idx)
                                                  : CellNone();
        case 1:
            return snap->questItems[idx] != 0 ? CellItem(snap->questItems[idx]) : CellNone();
        case 2:
            return (snap->passives & (1u << idx)) ? CellItem((u32)ITEM_GRIP_RING + (u32)idx)
                                                  : CellNone();
        default:
            return CollectionCell(idx, snap);
    }
}

static void FormatCount(char* dst, size_t cap, int32_t value) {
    size_t i = 0, j;
    char tmp[8];
    if (value < 0) {
        dst[0] = 0;
        return;
    }
    do {
        tmp[i++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && i < sizeof(tmp));
    for (j = 0; j < i && j + 1 < cap; j++) {
        dst[j] = tmp[i - 1 - j];
    }
    dst[j] = 0;
}

/* One group of wells: the game's own slots, kept in the arrangement the game
 * gives them (the elements' diamond, the collection's 2x2, the tray's row) but
 * mapped into a panel-sized box. */
typedef struct {
    int section;    /* which SectionCell family the slots belong to */
    const u8 slots[4]; /* quest-screen slot ids, in section-index order */
    int count;
} QuestGroup;

/* Turns one axis of slot-table coordinates into lane indices: coordinates
 * within kLaneSlack of each other are the same lane. The table's own numbers
 * are icon origins, not a grid — the collection's two columns sit at x 53 and
 * 54, its two rows at y 56 and 57 — so a plain sort would read four lanes
 * where the screen shows two. Returns how many lanes there are. */
#define kLaneSlack 8
static int32_t LaneRanks(const int32_t* v, int n, int32_t* outLane, int32_t* outPitch) {
    int32_t rep[4];
    int32_t lanes = 0;
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; j < lanes; j++) {
            if (v[i] - rep[j] < kLaneSlack && rep[j] - v[i] < kLaneSlack) {
                break;
            }
        }
        if (j == lanes) {
            rep[lanes++] = v[i];
        }
    }
    /* insertion sort, so lane 0 is the leftmost/topmost */
    for (i = 1; i < lanes; i++) {
        int32_t key = rep[i];
        for (j = i - 1; j >= 0 && rep[j] > key; j--) {
            rep[j + 1] = rep[j];
        }
        rep[j + 1] = key;
    }
    for (i = 0; i < n; i++) {
        outLane[i] = 0;
        for (j = 0; j < lanes; j++) {
            if (v[i] - rep[j] < kLaneSlack && rep[j] - v[i] < kLaneSlack) {
                outLane[i] = j;
                break;
            }
        }
    }
    /* The tightest gap between neighbouring lanes: the cluster's own pitch on
     * this axis, which is how far apart the game spaces these slots. */
    *outPitch = 0;
    for (i = 1; i < lanes; i++) {
        if (*outPitch == 0 || rep[i] - rep[i - 1] < *outPitch) {
            *outPitch = rep[i] - rep[i - 1];
        }
    }
    return lanes;
}

/* A cluster's shape, measured off the game's own slot positions once and then
 * used both to size the screen and to place the wells. Lane steps are in
 * quarter cells: a solid grid steps a whole well at a time, but the elements'
 * diamond does not — the game spaces those four 24px apart across and only
 * 13px down, which is what makes it a diamond rather than a plus. */
#define QCELL 8 /* sub-cell units: one full-size well is QCELL of them */
#define DIAMOND_WELL 6 /* a diamond's wells, in those units — the game draws the
                        * elements smaller than the collection's plates too */
typedef struct {
    int32_t cols, rows; /* lanes the cluster's slots fall into */
    int32_t col[4], row[4];
    int32_t well;           /* well size, in QCELL units */
    int32_t stepX, stepY;   /* units between adjacent lanes */
    int32_t quartW, quartH; /* footprint, in the same units */
} GroupShape;

static void MeasureGroup(const QuestGroup* g, GroupShape* out) {
    int32_t sx[4], sy[4], pitchX = 0, pitchY = 0, base;
    int solid;
    int i;

    for (i = 0; i < g->count; i++) {
        const u8* entry = SlotEntry(g->slots[i]);
        sx[i] = entry != NULL ? (int32_t)entry[6] : i * 24;
        sy[i] = entry != NULL ? (int32_t)entry[7] : 0;
    }
    out->cols = LaneRanks(sx, g->count, out->col, &pitchX);
    out->rows = LaneRanks(sy, g->count, out->row, &pitchY);

    /* A cluster with a slot at (nearly) every lane crossing is a solid grid —
     * the collection's 2x2, the two trays' rows — and gets full-size wells a
     * full well apart. A sparse one is a diamond: four slots over nine lane
     * crossings, spaced 24 across but only 13 down, which is what makes it a
     * diamond rather than a plus. Those keep the game's proportions on smaller
     * wells, sized so the widest step is exactly one well and no two touch. */
    solid = out->cols * out->rows <= g->count;
    base = pitchX > pitchY ? pitchX : pitchY;
    if (base <= 0) {
        base = 1;
    }
    out->well = solid ? QCELL : DIAMOND_WELL;
    out->stepX = solid ? QCELL : (pitchX * DIAMOND_WELL + base / 2) / base;
    out->stepY = solid ? QCELL : (pitchY * DIAMOND_WELL + base / 2) / base;
    if (out->stepX < 2) out->stepX = 2;
    if (out->stepY < 2) out->stepY = 2;
    out->quartW = out->well + (out->cols - 1) * out->stepX;
    out->quartH = out->well + (out->rows - 1) * out->stepY;
}

/* Draws a measured cluster with its top-left footprint corner at (bx, by).
 * `cell` is the one full-size well the whole screen is built from, so the
 * clusters read as one set of plates the way the items grid does. */
static void DrawGroup(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                      const QuestGroup* g, const GroupShape* shape,
                      const SecondScreenSnapshot* snap, int32_t bx, int32_t by, int32_t cell,
                      int32_t scale, SecondScreenQuestHotspots* outHot) {
    int32_t well = shape->well * cell / QCELL;
    int32_t gap = well / 14;
    int i;

    for (i = 0; i < g->count; i++) {
        QuestCell content = SectionCell(g->section, i, snap);
        int32_t cx = bx + shape->col[i] * shape->stepX * cell / QCELL + gap;
        int32_t cy = by + shape->row[i] * shape->stepY * cell / QCELL + gap;
        int32_t side = well - 2 * gap;
        int32_t artH = side;
        int32_t inset = 3 * scale;
        CellInk ink;

        /* The two collection wells that open a list of their own, reported
         * whether or not they are filled — an empty bag well still has
         * nothing behind it, so only a filled one becomes tappable. */
        if (outHot != NULL && g->section == 3 && (i == 0 || i == 2)) {
            SecondScreenQuestRect* r = (i == 0) ? &outHot->bag : &outHot->skill;
            int occupied = (i == 0) ? (content.frame != 0) : (content.skill != 0);
            r->x = cx;
            r->y = cy;
            r->w = occupied ? side : 0;
            r->h = side;
        }

        Port_SecondScreenTheme_DrawWell(pixels, bufW, bufH, stride, cx, cy, side, side, scale);
        if (content.count >= 0 && !content.skill) {
            artH = side - 12 * scale; /* the count gets its own line inside the well */
        }
        if (content.skill) {
            ink = RenderSkillCell(content.frame, content.count);
        } else if (content.item != 0) {
            ink = RenderItemCell(content.item);
        } else if (content.frame != 0) {
            ink = RenderFrameCell(content.frame);
        } else {
            continue; /* empty well, as the real screen shows an uncollected entry */
        }
        StampCell(pixels, bufW, bufH, stride, ink, cx + inset, cy + inset, side - 2 * inset,
                  artH - 2 * inset);
        if (content.count >= 0 && !content.skill) {
            char buf[8];
            int32_t tw;
            FormatCount(buf, sizeof(buf), content.count);
            tw = Port_SecondScreenTheme_TextWidth(buf, scale);
            Port_SecondScreenTheme_DrawText(pixels, bufW, bufH, stride, cx + (side - tw) / 2,
                                            cy + side - 14 * scale, scale, SS_TEXT_INK, buf);
        }
    }
}

int Port_SecondScreenQuest_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                const SecondScreenSnapshot* snap, uint32_t tick,
                                SecondScreenQuestHotspots* outHot) {
    /* The screen's four clusters, each keeping the slot ids the game groups
     * them into: what Link has GATHERED (2x2), the ELEMENTS (their diamond),
     * what he CARRIES (the tray) and what he WEARS. Laid out the way the game
     * lays them out — gathered above carried on the left, elements above worn
     * on the right — just at panel size. */
    static const QuestGroup kGroups[4] = {
        { 3, { SLOT_KINSTONE_BAG, SLOT_HEART_PIECES, SLOT_SWORD_SKILLS, SLOT_SHELLS }, 4 },
        { 0, { 9, 10, 11, 12 }, 4 },
        { 1, { SLOT_QUEST_ITEM, SLOT_QUEST_ITEM + 1, SLOT_QUEST_ITEM + 2, 0 }, 3 },
        { 2, { 13, 14, 15, 0 }, 3 },
    };
    GroupShape shape[4];
    int32_t scale, pad, innerX, innerY, innerW, innerH;
    int32_t quartW, quartH, cell, gridW, gridH, ox, oy, i;

    (void)tick; /* the screen's only animation was its selection cursor */

    if (outHot != NULL) {
        memset(outHot, 0, sizeof(*outHot));
    }
    if (pixels == NULL || snap == NULL || dstW <= 0 || dstH <= 0) {
        return 0;
    }
    if (!Port_SecondScreenTheme_Ready()) {
        return 0; /* plate, wells and font all come from the theme */
    }
    /* Readiness probe: if the screen's own OBJ tiles aren't resolved yet every
     * cell would come out empty, which reads as a fresh save rather than as
     * "not loaded" — better to leave the caller on its fallback. */
    {
        const u8* probe = SlotEntry(SLOT_HEART_PIECES);
        if (probe == NULL || RenderFrameCell(SLOT_FRAME(probe, 1u)).w == 0) {
            return 0;
        }
    }

    /* Art scale for the nine-sliced plate, the wells and the font. Derived
     * from the panel's short side against the GBA screen's, so the carved
     * detail grows with the panel instead of staying at its 1x thickness. */
    scale = (dstW < dstH ? dstW : dstH) / 200;
    if (scale < 1) {
        scale = 1;
    }
    pad = 10 * scale;

    Port_SecondScreenTheme_DrawBackdrop(pixels, bufW, bufH, stride, dstX, dstY, dstX + dstW,
                                        dstY + dstH, scale);
    Port_SecondScreenTheme_DrawPlate(pixels, bufW, bufH, stride, dstX, dstY, dstW, dstH, scale);

    /* Inside the plate's carved border. The wells are what the player reads,
     * so the inset is the border's own thickness plus a hair, not a margin
     * chosen for looks — every pixel spent here comes off the well size. */
    innerX = dstX + 3 * pad / 2;
    innerY = dstY + 3 * pad / 2;
    innerW = dstW - 3 * pad;
    innerH = dstH - 3 * pad;

    for (i = 0; i < 4; i++) {
        MeasureGroup(&kGroups[i], &shape[i]);
    }

    /* Three bands stacked down the panel, all sharing one well size:
     *
     *     gathered | elements     the game's own top row, side by side
     *       carried tray          its wide tray, centred under them
     *        worn gear            the row it puts beside the tray
     *
     * The GBA screen is 3:2 and puts the two trays side by side; this panel is
     * close to square, so they stack instead — same clusters, same reading
     * order, no dead band down the middle. Band widths are measured in half
     * cells so the diamond's half-beat lanes count properly. */
#define BAND_GAP 3 /* sub-cell units of air between bands */
    {
        int32_t topH = shape[0].quartH > shape[1].quartH ? shape[0].quartH : shape[1].quartH;
        int32_t pairW = shape[0].quartW + BAND_GAP + shape[1].quartW;

        quartW = pairW;
        if (shape[2].quartW > quartW) quartW = shape[2].quartW;
        if (shape[3].quartW > quartW) quartW = shape[3].quartW;
        quartH = topH + BAND_GAP + shape[2].quartH + BAND_GAP + shape[3].quartH;

        cell = QCELL * innerW / quartW;
        if (QCELL * innerH / quartH < cell) {
            cell = QCELL * innerH / quartH;
        }
        if (cell < 16) {
            return 0; /* nothing legible would come of it — leave the fallback up */
        }
        gridW = quartW * cell / QCELL;
        gridH = quartH * cell / QCELL;
        ox = innerX + (innerW - gridW) / 2;
        oy = innerY + (innerH - gridH) / 2;

        {
            /* The clusters are as wide as the panel lets them get, so on a
             * squarish panel there is height left over. A quarter of it goes
             * into each seam and the rest stays as margin: enough to keep the
             * three bands from stacking up top-heavy, not so much that the
             * screen reads as three unrelated strips. */
            int32_t slack = (innerH - gridH) / 4;
            int32_t pairX = ox + (gridW - pairW * cell / QCELL) / 2;
            int32_t trayY, wornY;

            if (slack < 0) {
                slack = 0;
            }
            oy = innerY + (innerH - gridH - 2 * slack) / 2;
            trayY = oy + (topH + BAND_GAP) * cell / QCELL + slack;
            wornY = trayY + (shape[2].quartH + BAND_GAP) * cell / QCELL + slack;

            DrawGroup(pixels, bufW, bufH, stride, &kGroups[0], &shape[0], snap, pairX,
                      oy + (topH - shape[0].quartH) * cell / (2 * QCELL), cell, scale, outHot);
            DrawGroup(pixels, bufW, bufH, stride, &kGroups[1], &shape[1], snap,
                      pairX + (shape[0].quartW + BAND_GAP) * cell / QCELL,
                      oy + (topH - shape[1].quartH) * cell / (2 * QCELL), cell, scale, NULL);
            DrawGroup(pixels, bufW, bufH, stride, &kGroups[2], &shape[2], snap,
                      ox + (gridW - shape[2].quartW * cell / QCELL) / 2, trayY, cell, scale, NULL);
            DrawGroup(pixels, bufW, bufH, stride, &kGroups[3], &shape[3], snap,
                      ox + (gridW - shape[3].quartW * cell / QCELL) / 2, wornY, cell, scale, NULL);
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------- *
 * The two lists the quest screen opens                                    *
 *                                                                         *
 * Pause screens 7 and 8. Both are the same shape — a grid of wells over    *
 * the same plate, one entry per well — so they share a layout pass and     *
 * differ only in what fills a cell.                                        *
 * ---------------------------------------------------------------------- */

/* Lays out a grid of `count` wells over the plate and hands each one back
 * to the caller to fill. Columns are chosen to keep the cells as square and
 * as large as the rect allows, which on this panel means 3 or 4 across. */
typedef void (*ListCellFn)(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, int32_t idx,
                           int32_t cx, int32_t cy, int32_t side, int32_t scale, const void* ctx);

static int DrawList(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, int32_t dstX,
                    int32_t dstY, int32_t dstW, int32_t dstH, int32_t count, ListCellFn fill,
                    const void* ctx) {
    int32_t scale, pad, innerX, innerY, innerW, innerH;
    int32_t cols, rows, cell, gap, gridW, gridH, ox, oy, i;

    if (count <= 0) {
        return 0;
    }
    scale = (dstW < dstH ? dstW : dstH) / 200;
    if (scale < 1) {
        scale = 1;
    }
    pad = 10 * scale;
    Port_SecondScreenTheme_DrawBackdrop(pixels, bufW, bufH, stride, dstX, dstY, dstX + dstW,
                                        dstY + dstH, scale);
    Port_SecondScreenTheme_DrawPlate(pixels, bufW, bufH, stride, dstX, dstY, dstW, dstH, scale);
    innerX = dstX + 3 * pad / 2;
    innerY = dstY + 3 * pad / 2;
    innerW = dstW - 3 * pad;
    innerH = dstH - 3 * pad;

    /* Widest grid whose cells still fit the rect: more columns means smaller
     * cells, so walk up from one and keep the last that beats the previous. */
    cols = 1;
    cell = 0;
    for (i = 1; i <= 6 && i <= count; i++) {
        int32_t r = (count + i - 1) / i;
        int32_t c = innerW / i;
        if (innerH / r < c) {
            c = innerH / r;
        }
        if (c > cell) {
            cell = c;
            cols = i;
        }
    }
    if (cell < 16) {
        return 0;
    }
    rows = (count + cols - 1) / cols;
    gap = cell / 12;
    gridW = cols * cell;
    gridH = rows * cell;
    ox = innerX + (innerW - gridW) / 2;
    oy = innerY + (innerH - gridH) / 2;

    for (i = 0; i < count; i++) {
        int32_t cx = ox + (i % cols) * cell + gap;
        int32_t cy = oy + (i / cols) * cell + gap;
        int32_t side = cell - 2 * gap;
        Port_SecondScreenTheme_DrawWell(pixels, bufW, bufH, stride, cx, cy, side, side, scale);
        fill(pixels, bufW, bufH, stride, i, cx, cy, side, scale, ctx);
    }
    return 1;
}

/* --- KINSTONE PIECES (pause screen 7) --- */

/* The bag's rows, packed from the front and terminated by a zero type —
 * sub_080A6044's own walk, so an empty bag draws no wells at all. */
static int32_t KinstoneRowCount(const SecondScreenSnapshot* snap) {
    int32_t n = 0;
    while (n < 19 && snap->kinstoneTypes[n] != 0) {
        n++;
    }
    return n;
}

#define KINSTONE_PIECE_TILE_BYTES 512u

static void KinstoneListCell(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                             int32_t idx, int32_t cx, int32_t cy, int32_t side, int32_t scale,
                             const void* ctx) {
    const SecondScreenSnapshot* snap = (const SecondScreenSnapshot*)ctx;
    const KinstoneWorldEvent* events = gKinstoneWorldEvents;
    /* One row's tiles at a time, same single-render-thread contract as the
     * module's other scratch (sCell): the game reuses a VRAM slot per row
     * here too, so nothing outlives the cell it was decoded for. */
    static u8 sPieceTiles[KINSTONE_PIECE_TILE_BYTES];
    ObjSource src = { kKinstoneVramGroups, (int)sizeof(kKinstoneVramGroups), 204u, NULL, 0u };
    CellInk ink;
    u32 id = snap->kinstoneTypes[idx];
    int32_t artW = side * 3 / 5; /* the piece left, its count right, as the game pairs them */
    char buf[8];
    int32_t tw;

    if (REGION_IS_EU) {
        events = gKinstoneWorldEvents_eu;
    } else if (REGION_IS_JP) {
        events = gKinstoneWorldEvents_jp;
    }
    if (events == NULL || id == 0) {
        return;
    }
    if (Port_GetKinstonePieceTiles(events[id].gfxOffsetPiece, sPieceTiles, sizeof(sPieceTiles)) == 0) {
        return;
    }
    src.tiles = sPieceTiles;
    src.tilesLen = sizeof(sPieceTiles);

    /* sub_080A42E0 draws the piece as sprite 0 frame 3 with the event's own
     * OBJ palette over the tiles it just streamed — the tile base it builds
     * is the VRAM slot it picked, which is tile 0 of that stream here. */
    memset(sCell, 0, sizeof(sCell));
    DrawObjFrameFrom(&src, sCell, CELL_SRC, CELL_SRC, 0u, 3u, CELL_ORIGIN, CELL_ORIGIN,
                     ((u32)events[id].objPalette << 12) | 0x800u);
    ink = CellBounds();
    StampCell(pixels, bufW, bufH, stride, ink, cx + 2 * scale, cy + 2 * scale, artW - 4 * scale,
              side - 4 * scale);

    FormatCount(buf, sizeof(buf), (int32_t)snap->kinstoneAmounts[idx]);
    tw = Port_SecondScreenTheme_TextWidth(buf, scale);
    Port_SecondScreenTheme_DrawText(pixels, bufW, bufH, stride, cx + artW + (side - artW - tw) / 2,
                                    cy + (side - 8 * scale) / 2, scale, SS_TEXT_INK, buf);
}

int Port_SecondScreenQuest_DrawKinstones(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                         const SecondScreenSnapshot* snap) {
    if (pixels == NULL || snap == NULL || dstW <= 0 || dstH <= 0 || !Port_SecondScreenTheme_Ready()) {
        return 0;
    }
    return DrawList(pixels, bufW, bufH, stride, dstX, dstY, dstW, dstH, KinstoneRowCount(snap),
                    KinstoneListCell, snap);
}

/* --- SWORD TECHNIQUES (pause screen 8) --- */

#define TECHNIQUE_COUNT 8
#define TECHNIQUE_SPRITE (DRAW_DIRECT_SPRITE_INDEX + 1u)
#define TECHNIQUE_SCROLL_FRAME 10u
#define TECHNIQUE_OAM_EXTRA 0xC00u

static void TechniqueListCell(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                              int32_t idx, int32_t cx, int32_t cy, int32_t side, int32_t scale,
                              const void* ctx) {
    const SecondScreenSnapshot* snap = (const SecondScreenSnapshot*)ctx;
    CellInk ink;

    /* An unlearned technique keeps its well and shows nothing, which is what
     * the pause screen does with the scrolls Link hasn't been taught. */
    if ((snap->swordSkillBits & (1u << idx)) == 0) {
        return;
    }
    /* sub_080A617C's own row draw: one shared scroll frame, recoloured per
     * technique by the palette bank its table entry names. */
    memset(sCell, 0, sizeof(sCell));
    DrawObjFrameFrom(&kTechSource, sCell, CELL_SRC, CELL_SRC, TECHNIQUE_SPRITE,
                     TECHNIQUE_SCROLL_FRAME, CELL_ORIGIN, CELL_ORIGIN,
                     ((u32)gUnk_08128D70[idx].unk1 << 12) | TECHNIQUE_OAM_EXTRA);
    ink = CellBounds();
    StampCell(pixels, bufW, bufH, stride, ink, cx + 3 * scale, cy + 3 * scale, side - 6 * scale,
              side - 6 * scale);
}

int Port_SecondScreenQuest_DrawTechniques(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                          int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                          const SecondScreenSnapshot* snap) {
    if (pixels == NULL || snap == NULL || dstW <= 0 || dstH <= 0 || !Port_SecondScreenTheme_Ready()) {
        return 0;
    }
    return DrawList(pixels, bufW, bufH, stride, dstX, dstY, dstW, dstH, TECHNIQUE_COUNT,
                    TechniqueListCell, snap);
}
