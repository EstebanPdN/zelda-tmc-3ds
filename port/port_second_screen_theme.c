#include "port_second_screen_theme.h"

#include "port_rom.h"
#include "region.h"
#include "structures.h"

#include <math.h>
#include <string.h>

/*
 * Decode map (all runtime, zero baked pixels — see header):
 *
 *   window chrome   gUnk_081092AC[0] border shapes + gUnk_081094CE color
 *                   LUT + gUnk_0810926C fill patterns — the exact pipeline
 *                   sub_0805F918 (src/text.c) runs to build the message
 *                   window's 7 tiles (fill all seven with the fill_type's
 *                   nibble pattern, then write the border columns through
 *                   the LUT), replayed into private RGBA buffers. Tile
 *                   arrangement comes from DispMessageFrame
 *                   (src/message.c) — corner + edge tiles, flips
 *                   mirroring. border_type 0 / fill_type 0 is the plain
 *                   in-game dialog (MsgOpen passes message.unk3/bgColor,
 *                   both 0 by default): black interior (bank 15 color 15),
 *                   silver/white rounded double border.
 *   hearts/rupee/   gfx group 16 (the HUD tile load InitUI performs to BG0
 *   key icons       char base 3, src/ui.c) via Port_ResolveGfxGroupVram;
 *                   tile ids from DrawHearts/DrawRupees/DrawKeys. Heart
 *                   row truth is gUnk_080C8F2C's tilemap entries: tile
 *                   0x15 = full, 0x11 = empty, 0x12..0x14 the quarter
 *                   fills DrawHearts writes as (health&3)+0x11.
 *   digit fonts     gUnk_085C4620 (a u32[] blob — offsets below in bytes):
 *                   +0x000 small ammo digits (tens tiles 0..9, ones tiles
 *                   10..19 — RenderDigits' sibling sub_0801C2F0 DMAs
 *                   gUnk_085C4620 + digit*8 in u32 units = digit*32
 *                   bytes), +0x280 white 8x16 counters, +0x500 yellow
 *                   (maxed) variants.
 *   palettes        BG bank 15 (MESSAGE_PALETTE) via palette group 12 for
 *                   everything the HUD writes with 0xF000 tilemap entries
 *                   (hearts/rupees/keys/counter digits/window chrome).
 *                   OBJ pieces carry a palette bank in their 5th byte
 *                   (RenderSpritePieces: bit0 set = absolute bank, else
 *                   added to the OAM command's bank); banks resolve from
 *                   the palette-group chain state the pause menu leaves
 *                   loaded — group 182 (Items tab, OBJ banks 5..10), then
 *                   181 (pause base, OBJ 0..4), then 11 (gameplay OBJ
 *                   0..4) — most recently loaded group first, exactly
 *                   like the shared gPaletteBuffer. The small ammo digits
 *                   are OBJ tiles whose piece selects bank 4.
 *   A/B/R buttons   fixed UI sprite 505 on USA / 504 on EU, frames 0/1/2
 *                   (gUIElementDefinitions), pieces
 *                   from gFrameObjLists via sub_080AD8F0. The button
 *                   elements have no sprite sheet (definition unk_e = 1,
 *                   so sub_0801CB20 never DMAs one): their tiles sit in
 *                   OBJ VRAM at the element's fixed slot 0x100
 *                   (definition unk_4), loaded there by gfx group 16's
 *                   0x06012000 record. Pieces are slot-relative.
 *   equip cursor    pause-menu sprite 0x1FB (0x1FA on EU) frames 4/5 — the
 *                   blinking gold slot frame of the Items screen
 *                   (gItemMenuTable item slots are type 1; the draw uses
 *                   entry->type + 3/4). Its pieces address OBJ VRAM
 *                   absolutely (gOamCmd._8 = 0x800, tile base 0); tile
 *                   0x0E lives in gfx group 86's 0x06010000 record (the
 *                   pause chrome group sub_080A4D34 loads over the
 *                   gameplay tiles), so VRAM resolves latest-load-first:
 *                   group 90, 86, then the gameplay 23/16.
 */

/* src/common.c (appended accessors — ROM-const reads only). */
extern const u8* Port_GetRawPaletteGroupData(u32 group, u32* outNumColors);
extern const u8* Port_GetRawPaletteGroupBankData(u32 group, u32 destPaletteNum, u32* outNumColors);
extern const u8* Port_GetRawPaletteGroupEntryData(u32 group, u32 entryIdx, u32* outNumColors,
                                                  u32* outDestPaletteNum);
extern const u8* Port_ResolveGfxGroupVram(u32 group, u32 vramAddr, u32* outAvail);

/* src/affine.c — frame OBJ piece list for (sprite, frame); PC path is
 * bounds-checked and returns NULL when out of range. */
extern void* sub_080AD8F0(u32 sprite, u32 frame);

/* port/port_text_render.c — 64 packed bytes -> 128 pixel bytes (8x16). */
extern void UnpackTextNibbles(void* src_ptr, u8* dest);

/* ROM-const text/border tables, resolved from the loaded ROM at boot
 * (port_rom.c). Same externs src/text.c uses. */
extern void* gUnk_081092AC[]; /* border shape data per border_type */
extern u8 gUnk_081094CE[];    /* color LUTs, 0xC0 bytes per fill_type */
extern u32 gUnk_0810926C[];   /* window fill patterns, u32 each */
extern u8 gUnk_0810942E[];    /* color-table head (first 160 logical bytes) */
extern void* gUnk_08109248[]; /* message font glyph banks (bank 0 = latin) */

/* Resolved ROM group tables (port_rom.c) — for the group-record walk the
 * pause-screen tilemap fetch needs (EWRAM-destined records fall outside
 * Port_ResolveGfxGroupVram's remit on non-USA link maps, so they are
 * matched by destination region instead; mirrors the walk
 * port_second_screen_worldmap.c does for the map screen). */
extern const void* gGfxGroups[];
extern const u8* gGlobalGfxAndPalettes;

/* HUD digit gfx blob (data_const_stubs.c / ROM) — same symbol src/ui.c
 * reads at runtime for every region. */
extern const u8 gUnk_085C4620[];

#define HUD_GFX_GROUP 16u
#define HUD_PALETTE_GROUP 12u       /* -> BG bank 15, MESSAGE_PALETTE */
#define HUD_BG_CHARBASE 0x0600C000u /* BG0 char base 3 (control 0x1f0c) */
#define OBJ_VRAM_BASE 0x06010000u

/*
 * Pause ITEM screen (the light theme's ground truth). PauseMenuScreen_1
 * selects gUnk_08128AD8[0] = { paletteGroup 182, gfxGroup 90, dispcnt,
 * bg1Control 0x1C05, bg2Control 0x1D03 } (src/data/figurineMenuData.c,
 * applied by sub_080A4DB8); sub_080A4D34 loads the shared pause chrome
 * first (gfx group 86, BG3 control 0x1E0B) plus palette group 181 over
 * the gameplay groups 11/12. That makes the screen:
 *
 *   BG3  Ezlo-doodle parchment backdrop: group 86 tiles @ 0x06008000
 *        (charBase 2) + its EWRAM tilemap copy (gBG3Buffer).
 *   BG2  the carved stone slab with the item wells: group 90 tiles @
 *        0x06000000 (charBase 0) + its EWRAM tilemap copy (gBG2Buffer).
 *   BG1  off at entry (dispcnt bit 9 clear), BG0 live text — neither is
 *        part of the static dressing.
 *
 * Both layers compose here into private 240x160 RGBA images the draw
 * calls slice at runtime: the backdrop pattern re-stamped on its own
 * doodle lattice, the slab nine-sliced for panels, the bottle tray
 * nine-sliced for wells/rows. Palette load order 11, 12, 181, 182 —
 * later loads win, like the shared gPaletteBuffer.
 */
#define MENU_GFX_GROUP 90u
#define MENU_PALETTE_GROUP 182u
#define MENU_TILES_DEST 0x06000000u   /* bg2Control 0x1D03 -> charBase 0 */
#define CHROME_TILES_DEST 0x06008000u /* bg3Control 0x1E0B -> charBase 2 */
#define CHROME_GFX_GROUP 86u
#define MENU_SCREEN_W 240
#define MENU_SCREEN_H 160

/* OBJ palette state at pause time, most recently loaded group first —
 * 182 (Items tab: OBJ banks 5..10), 181 (pause base: OBJ 0..4), 11
 * (gameplay: OBJ 0..4). Later loads win on the GBA, so earlier in this
 * list wins here. 183 (Quest tab) sits at the tail because it is the only
 * group that carries OBJ bank 14, the bank that tab's item icons fall back
 * to; on banks 5..10 — the only others it defines — it is byte-identical
 * to 182, so where it sits in the list changes nothing else. */
static const u8 kObjPaletteGroups[] = { 182u, 181u, 11u, 183u };

/* OBJ VRAM state, same latest-load-first rule. Buttons live in the HUD
 * element block gfx group 16 loads at 0x06012000 (group 86 carries an
 * identical copy for the pause screens). The equip cursor's tile 0x0E is
 * group 86's 0x06010000 block, loaded over the gameplay tiles when the
 * pause menu opens — 23/16 stay as the base layers underneath. */
static const u8 kHudObjGfxGroups[] = { 16u, 86u };
static const u8 kPauseObjGfxGroups[] = { 90u, 86u, 23u, 16u };
/* Same rule for the QUEST STATUS screen (pause screen 2, gfx group 91): the
 * SLEEP/SAVE button plates sit in group 91's 0x06015800 block, the selection
 * brackets in the shared chrome group's 0x06010000 block. */
static const u8 kQuestObjGfxGroups[] = { 91u, 86u, 23u, 16u };

/* HUD tile ids (BG0 char base 3) — from DrawHearts / DrawRupees /
 * DrawKeys in src/ui.c and gWalletSizes in src/itemUtils.c. The heart
 * series runs empty -> full: 0x11 empty, 0x12..0x14 quarter fills
 * ((health & 3) + 0x11), 0x15 full (gUnk_080C8F2C's DMA'd entries). */
#define TILE_HEART_EMPTY 0x11
#define TILE_HEART_FULL 0x15
#define TILE_KEY 0x1C       /* 2x2 */
#define TILE_RUPEE_W0 0x60  /* 2x2, +4 per wallet tier */

/* OBJ palette bank the ammo-digit pieces select (sprite 322's digit
 * pieces carry palette 4 with the absolute bit — see the frame OBJ data
 * for items with counters, e.g. bombs). */
#define SMALL_DIGIT_OBJ_BANK 4u

/* Sprite indices. */
#define SPRITE_HUD_BUTTONS_RAW 505u
#define SPRITE_PAUSE_MISC (REGION_IS_EU ? 0x1FAu : 0x1FBu)
#define BUTTON_VRAM_SLOT 0x100u /* gUIElementDefinitions[BUTTON_*].unk_4 */
#define CURSOR_FRAME_0 4u
#define CURSOR_FRAME_1 5u
#define BUTTON_R_FRAME 2u /* ButtonUIElement_Action0: frame == element->type */

/* Contextual R-prompt labels (SPEAK / READ / LIFT / ...): sprite 322, the
 * same sheet the item icons come from — gUIElementDefinitions[TEXT_R] gives
 * spriteIndex 322 and OBJ slot 0x10E, and TextUIElement picks the frame from
 * gHUD.buttonText[2] plus the language offset (the snapshot publishes that
 * sum, so frameId arrives final). Pieces index tiles relative to the frame's
 * firstTileIndex, exactly like sub_0801CB20's DMA. */
#define SPRITE_UI_LABELS_RAW 322u

/* Pause-menu labelled button (the SLEEP / SAVE plates of the quest-status
 * screen, drawn by sub_080A57F4 as sprite SPRITE_PAUSE_MISC frames
 * 1 + 9 + slotTable[slot].unk5 for slots 4 and 5). Frame 34 is SAVE and
 * 35 SLEEP; either carries the same plate, so the plate art is nine-sliced
 * out of one of them and the label re-lettered in the stylized font.
 * MENU_BRACKET_FRAME is the selection bracket those two slots use
 * (their unk4 + 9 + 1), reused as the button's own pressed/active mark: 0x1A
 * in the non-JP slot table, 8 in the language-0 one. */
#define MENU_BUTTON_FRAME 34u
#define MENU_BRACKET_FRAME (REGION_IS_JP ? 18u : 36u)
#define MENU_BUTTON_CORNER 2 /* the plate's rounded rim is 2 px a side */

static int sBuilt = 0;

/* All cached element pixels live in one arena so failure cleanup is just
 * "leave the slot NULL" — no per-element allocation bookkeeping. Sized
 * with headroom for the largest composites (~160 KB static, one-time). */
static uint32_t sArena[40000];
static int32_t sArenaUsed = 0;

/* ---- pause-menu layer caches (see MENU_* block above) ---- */
static uint32_t sMenuBg3[MENU_SCREEN_W * MENU_SCREEN_H]; /* parchment, opaque */
static uint32_t sMenuBg2[MENU_SCREEN_W * MENU_SCREEN_H]; /* slab, 0 = clear */
static int sMenuOk = 0;
static int sSlabOk = 0, sTrayOk = 0, sDoodleOk = 0, sFontOk = 0;
static int32_t sSlabX0, sSlabY0, sSlabX1, sSlabY1; /* slab bbox, exclusive max */

/* One full Ezlo doodle cut from the backdrop layer, and the diagonal
 * lattice every doodle of the pattern sits on: SRC + i*A + j*B (verified
 * against the composed layer — the pattern is not a straight tile grid,
 * so the backdrop is re-stamped instead of wrap-tiled). */
#define DOODLE_W 34
#define DOODLE_H 39
#define DOODLE_SRC_X 139 /* a doodle fully inside the 240x160 screen */
#define DOODLE_SRC_Y 13
#define DOODLE_AX (-56)
#define DOODLE_AY 16
#define DOODLE_BX 24
#define DOODLE_BY 48
static uint32_t sDoodle[DOODLE_W * DOODLE_H]; /* 0 = transparent */

/* The bottle tray (the slab's one isolated wide well) — nine-slice source
 * for every recessed well; its rounded rim fits in 8 px corners. Copied
 * out before the slab is cleaned (below). */
#define TRAY_X0 48
#define TRAY_Y0 104
#define TRAY_X1 156
#define TRAY_Y1 132
#define TRAY_W (TRAY_X1 - TRAY_X0)
#define TRAY_H (TRAY_Y1 - TRAY_Y0)
#define WELL_CORNER 8
static uint32_t sTrayPx[TRAY_W * TRAY_H];

/* Slab slicing: 26x44 corner regions keep the triforce blocks intact.
 * The slab is a PANEL source, not a texture — its interior carries the
 * item wells, so after the tray/well art is copied out, the interior
 * (inside the carved side/top/bottom bands) is re-paved with the slab's
 * own plain stone (the clean speckled patch right of the well grid).
 * Slices then tile nothing but stone and carvings. */
#define SLAB_CORNER_W 26
#define SLAB_CORNER_H 44
#define SLAB_SIDE_W 28  /* carved side columns end before x 44 / after 196 */
#define SLAB_TOP_H 15   /* top triangle band rows 17..31 */
#define SLAB_BOTTOM_H 18
#define STONE_PATCH_X 190 /* clean speckled stone, right of the well grid */
#define STONE_PATCH_Y 56
#define STONE_PATCH_W 8
#define STONE_PATCH_H 64

static SecondScreenThemeSprite sSprites[SST_COUNT];
static uint32_t sColors[SSC_COUNT];

/* Selected panel backdrop style (SS_BACKDROP_*). Not part of the lazy
 * build — it is a live user setting, pushed in by the panel each frame
 * from the one thread that paints, so it needs no publication either. */
static int sBackdropStyle = SS_BACKDROP_PARCHMENT;

/* Window chrome: 6 border tiles in sub_0805F918's strip order plus the
 * solid fill tile — index meanings per src/message.c's tile enum. */
enum { CHROME_CORNER, CHROME_H_CORNER, CHROME_H_STRAIGHT, CHROME_V_CORNER, CHROME_V_STRAIGHT, CHROME_CURSOR,
       CHROME_FILL, CHROME_TILE_COUNT };
static uint32_t sChromeTiles[CHROME_TILE_COUNT][64];
static int sChromeOk = 0;

/* Chip tile sets: the rounded border_type-9 frame in each SS_CHIP_* fill
 * scheme, built by the same sub_0805F918 replay as the dialog chrome. */
static uint32_t sChipTiles[SS_CHIP_STYLE_COUNT][CHROME_TILE_COUNT][64];
static int sChipTilesOk[SS_CHIP_STYLE_COUNT];

/* Message font: glyph bank 0 (latin, 64 bytes per 8x16 glyph, row 0 is a
 * metrics row) plus the game's text color LUT pair per SS_TEXT_* style
 * and the message palette (BG bank 15) as RGBA. */
static const u8* sFontGlyphs = NULL;
static u8 sTextLut[SS_TEXT_STYLE_COUNT][32]; /* [0..15] even cols, [16..31] odd */
static uint32_t sMsgPal[16];

/* Stylized banner font: glyph bank 8 — the fat lettering of the area-name
 * banners ("South Hyrule Field"). Each glyph is TWO 8x16 cells (128 bytes,
 * banks > 4 double the cell offset in sub_0805F25C), each cell with its
 * own row-0 metrics; ShowTextBox's stylized path advances by both cell
 * widths minus one so adjacent glyphs share an outline column. The banner
 * Font (gUnk_08128FC0) renders it with the identity color LUT (fill 0 /
 * color 0) through BG palette bank 1 (its tilemap entries start at
 * gfx_src 0x1080 -> palette nibble 1), which is where the navy-outline /
 * silver-shade / white-body scheme lives. Glyph pixel values used by the
 * art: 1 navy outline, 10..13 shades, 14 body, 15 black rim. Styles other
 * than the authentic white recolor those roles through the message
 * palette (sBigPal below). */
static const u8* sBigFontGlyphs = NULL;
static int sBigFontOk = 0;
static uint32_t sBigPal[SS_TEXT_STYLE_COUNT][16]; /* value -> RGBA, 0 = skip */

/* Menu button: the composed SAVE plate, the sub-rect of it the plate itself
 * occupies (the composite's bbox is the OBJ pieces', which is taller), the
 * three tones sampled out of that art, and a stylized-font color table that
 * re-letters arbitrary labels in the plate's own blue. Plus the selection
 * bracket composite, cut into four corners at draw time. */
static SecondScreenThemeSprite sBtnPlate;
static int32_t sBtnX0, sBtnY0, sBtnW, sBtnH;
static uint32_t sBtnFill, sBtnInk, sBtnHi;
static int32_t sBtnCleanCol, sBtnCleanRow; /* plate columns/rows free of lettering */
static uint32_t sBtnPal[16];
static SecondScreenThemeSprite sBtnBracket;
static int sBtnOk = 0;

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static uint32_t* ArenaAlloc(int32_t count) {
    uint32_t* p;
    if (count <= 0 || sArenaUsed + count > (int32_t)(sizeof(sArena) / sizeof(sArena[0]))) {
        return NULL;
    }
    p = &sArena[sArenaUsed];
    sArenaUsed += count;
    memset(p, 0, (size_t)count * 4u);
    return p;
}

/* One 4bpp GBA tile (32 bytes) -> 64 RGBA pixels; color 0 stays 0
 * (transparent), matching how these tiles sit over the backdrop. */
static void DecodeTile4bpp(const u8* tile, const uint16_t* pal16, u32 numColors, uint32_t* out64) {
    int32_t py, px;
    for (py = 0; py < 8; py++) {
        for (px = 0; px < 8; px++) {
            u8 packed = tile[py * 4 + px / 2];
            u8 idx = (px & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
            out64[py * 8 + px] = (idx == 0 || idx >= numColors) ? 0u : Rgb555ToRgba8888(pal16[idx]);
        }
    }
}

static const u8* HudTileBytes(u32 tile, u32 tileCount) {
    u32 avail = 0;
    const u8* p = Port_ResolveGfxGroupVram(HUD_GFX_GROUP, HUD_BG_CHARBASE + tile * 32u, &avail);
    if (p == NULL || avail < tileCount * 32u) {
        return NULL;
    }
    return p;
}

/* Decodes `tileCount` consecutive tiles as a 2-tiles-wide icon block
 * (row1: t, t+1 / row2: t+2, t+3 — the exact cell order DrawRupees and
 * DrawKeys write into the tilemap). */
static const uint32_t* DecodeIcon2x2(const u8* tiles, const uint16_t* pal16, u32 numColors) {
    uint32_t* out = ArenaAlloc(16 * 16);
    uint32_t tmp[64];
    int32_t t, py, px;
    if (out == NULL) {
        return NULL;
    }
    for (t = 0; t < 4; t++) {
        int32_t ox = (t & 1) * 8;
        int32_t oy = (t >> 1) * 8;
        DecodeTile4bpp(tiles + t * 32, pal16, numColors, tmp);
        for (py = 0; py < 8; py++) {
            for (px = 0; px < 8; px++) {
                out[(oy + py) * 16 + ox + px] = tmp[py * 8 + px];
            }
        }
    }
    return out;
}

/* -------------------------------------------------------------------- */
/*  Window chrome (message-frame pipeline replay)                        */
/* -------------------------------------------------------------------- */

static void BuildChrome(const uint16_t* hudPal, u32 hudColors) {
    /* fill_type 0 — the plain in-game dialog window's color scheme.
     * Border LUT: sub_0805F918 indexes gUnk_081094CE + fill_type * 0xC0
     * directly (the tail table, not the head/tail-split text-color view);
     * even columns read [pixel], odd columns [0x10 + pixel] pre-shifted
     * <<4 (sub_080026C4's two halves). Fill pattern: the head-sentinel
     * byte at logical offset 0xAA of the split table — 0xAA lands past
     * the 160-byte gUnk_0810942E head, i.e. gUnk_081094CE[0x0A] — indexes
     * gUnk_0810926C (16 fill words, one nibble per pixel). */
    const u8* lutEven = &gUnk_081094CE[0x00];
    const u8* lutOdd = &gUnk_081094CE[0x10];
    u32 fillWord = gUnk_0810926C[gUnk_081094CE[0x0A] & 0x0Fu];
    u8 unpacked[128];
    int32_t border, block, py, px;

    sChromeOk = 0;

    /* border_type 0 is the standard dialog frame; if its data is missing
     * or decodes to nothing visible, try the next few types before giving
     * up — all of them are the game's own window frames. */
    for (border = 0; border < 4 && !sChromeOk; border++) {
        const u8* shapes = (const u8*)gUnk_081092AC[border];
        int32_t opaque = 0;
        if (shapes == NULL) {
            continue;
        }
        /* Three 0x40-byte blocks, each an 8x16 strip = two stacked tiles:
         * (corner, h-corner), (h-straight, v-corner), (v-straight, cursor).
         * sub_0805F918 fills the whole strip with the fill pattern first,
         * then writes every border pixel through the LUT unconditionally —
         * LUT output 0 = BG color 0 = transparent (the window's outside),
         * everything else opaque, interior pixels the fill color. */
        for (block = 0; block < 3; block++) {
            UnpackTextNibbles((void*)(shapes + block * 0x40), unpacked);
            for (py = 0; py < 16; py++) {
                for (px = 0; px < 8; px++) {
                    u32 pix = unpacked[py * 8 + px] & 0x0Fu;
                    u32 colorIdx = (px & 1) ? (u32)(lutOdd[pix] >> 4) : (u32)(lutEven[pix] & 0x0Fu);
                    uint32_t rgba = (colorIdx == 0 || colorIdx >= hudColors)
                                        ? 0u
                                        : Rgb555ToRgba8888(hudPal[colorIdx]);
                    sChromeTiles[block * 2 + (py >> 3)][(py & 7) * 8 + px] = rgba;
                    if (block == 0 && py < 8 && rgba != 0) {
                        opaque++; /* corner-tile visibility check */
                    }
                }
            }
        }
        sChromeOk = opaque >= 8;
    }

    /* Fill tile (MSG_BACKGROUND, the strip's 7th tile MemFill32 leaves as
     * the pure pattern). The six border tiles need no fill pass of their
     * own: the column writer overdraws every pixel, and the border art
     * already encodes "window interior" as the pixel value whose LUT
     * entry is the fill index (0xA -> 15 for fill_type 0), so the replay
     * above lands interior/outside pixels exactly like the game. */
    {
        u32 fillIdx = fillWord & 0xFu; /* one nibble per pixel; pattern words repeat it */
        uint32_t fill = (fillIdx != 0 && fillIdx < hudColors) ? Rgb555ToRgba8888(hudPal[fillIdx])
                                                              : sColors[SSC_WINDOW_FILL];
        for (py = 0; py < 64; py++) {
            sChromeTiles[CHROME_FILL][py] = fill;
        }
        sColors[SSC_WINDOW_FILL] = fill;
    }
}

/* -------------------------------------------------------------------- */
/*  Sprite-frame composites (buttons, equip cursor)                      */
/* -------------------------------------------------------------------- */

/* Standard GBA OBJ dimensions by (shape, size) — hardware constants. */
static const u8 kObjW[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 kObjH[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

/* OBJ palette bank -> raw RGB555 colors, resolved through the palette
 * group chain state the pause menu leaves loaded (kObjPaletteGroups,
 * latest load first). */
static const uint16_t* ObjPalBank(u32 bank) {
    u32 g;
    for (g = 0; g < sizeof(kObjPaletteGroups); g++) {
        u32 numColors = 0;
        const u8* p = Port_GetRawPaletteGroupBankData(kObjPaletteGroups[g], 16u + (bank & 15u), &numColors);
        if (p != NULL && numColors >= 16) {
            return (const uint16_t*)p;
        }
    }
    return NULL;
}

/* Composites one sprite frame (all its OBJ pieces) into a bbox-cropped
 * RGBA buffer. Piece format is RenderSpritePieces' (port_draw.c): count
 * byte, then 5 bytes per piece {s8 x, s8 y, u8 shape/size/flip/palmode,
 * u8 tile low, u8 tile high (tile bits 8-9 low, palette bank top
 * nibble)}. attr2 semantics per the engine (arm sub_080B2874): tile =
 * baseTile + tileLow + tileHighLow<<8; palette = piece bank, added to
 * the OAM command's bank unless piece bit0 clears it (all the sprites
 * here carry bit0, i.e. absolute banks — basePal is the command bank of
 * the game's own draw for fidelity when a piece doesn't). Tiles are
 * absolute OBJ VRAM, resolved through the gfx groups the relevant screen
 * keeps loaded (vramGroups, latest load first). Pieces are drawn in
 * reverse so the first (topmost OAM) piece wins overlaps. */
static int BuildSpriteComposite(u32 spriteIdx, u32 frameIdx, u32 baseTile, u32 basePal, const u8* vramGroups,
                                u32 numVramGroups, SecondScreenThemeSprite* outSprite) {
    const u8* frameData = (const u8*)sub_080AD8F0(spriteIdx, frameIdx);
    u32 count;
    int32_t minX = 0x7FFF, minY = 0x7FFF, maxX = -0x7FFF, maxY = -0x7FFF;
    int32_t w, h, i;
    uint32_t* out;

    if (frameData == NULL) {
        return 0;
    }
    count = frameData[0];
    if (count == 0 || count > 16) {
        return 0;
    }

    for (i = 0; i < (int32_t)count; i++) {
        const u8* p = frameData + 1 + i * 5;
        u32 shape = (p[2] >> 6) & 3;
        u32 size = (p[2] >> 4) & 3;
        int32_t px = (s8)p[0];
        int32_t py = (s8)p[1];
        if (shape == 3) {
            return 0;
        }
        if (px < minX) minX = px;
        if (py < minY) minY = py;
        if (px + kObjW[shape][size] > maxX) maxX = px + kObjW[shape][size];
        if (py + kObjH[shape][size] > maxY) maxY = py + kObjH[shape][size];
    }
    w = maxX - minX;
    h = maxY - minY;
    if (w <= 0 || h <= 0 || w > 96 || h > 96) {
        return 0;
    }
    out = ArenaAlloc(w * h);
    if (out == NULL) {
        return 0;
    }

    /* All-or-nothing: a composite missing pieces would read as broken
     * art, worse than the caller's clean fallback — so any unresolvable
     * piece rejects the whole sprite (the arena bump is a bounded
     * one-time cost; nothing partial is ever published). */
    for (i = (int32_t)count - 1; i >= 0; i--) {
        const u8* p = frameData + 1 + i * 5;
        u32 shape = (p[2] >> 6) & 3;
        u32 size = (p[2] >> 4) & 3;
        int hflip = (p[2] & 0x04) != 0;
        int vflip = (p[2] & 0x08) != 0;
        u32 tileIdx = baseTile + (u32)p[3] + (((u32)p[4] & 3u) << 8);
        u32 palBank = (((p[2] & 1u) ? 0u : basePal) + ((u32)p[4] >> 4)) & 15u;
        int32_t px = (int32_t)(s8)p[0] - minX;
        int32_t py = (int32_t)(s8)p[1] - minY;
        int32_t pw = kObjW[shape][size];
        int32_t ph = kObjH[shape][size];
        int32_t wTiles = pw / 8, hTiles = ph / 8;
        const uint16_t* pal = ObjPalBank(palBank);
        const u8* tiles;
        int32_t tx, ty, yy, xx;

        if (pal == NULL) {
            return 0;
        }
        {
            u32 avail = 0;
            u32 g;
            tiles = NULL;
            for (g = 0; g < numVramGroups && tiles == NULL; g++) {
                tiles = Port_ResolveGfxGroupVram(vramGroups[g], OBJ_VRAM_BASE + tileIdx * 32u, &avail);
                if (tiles != NULL && avail < (u32)(wTiles * hTiles) * 32u) {
                    tiles = NULL;
                }
            }
            if (tiles == NULL) {
                return 0;
            }
        }

        /* 1D OBJ mapping (the game runs with DISPCNT bit 6 set): a
         * piece's tiles are consecutive, row-major across its width. */
        for (ty = 0; ty < hTiles; ty++) {
            for (tx = 0; tx < wTiles; tx++) {
                const u8* tile = tiles + (ty * wTiles + tx) * 32;
                for (yy = 0; yy < 8; yy++) {
                    for (xx = 0; xx < 8; xx++) {
                        u8 packed = tile[yy * 4 + xx / 2];
                        u8 idx = (xx & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
                        int32_t dx, dy;
                        if (idx == 0) {
                            continue;
                        }
                        dx = tx * 8 + xx;
                        dy = ty * 8 + yy;
                        if (hflip) dx = pw - 1 - dx;
                        if (vflip) dy = ph - 1 - dy;
                        out[(py + dy) * w + px + dx] = Rgb555ToRgba8888(pal[idx]);
                    }
                }
            }
        }
    }

    outSprite->px = out;
    outSprite->w = w;
    outSprite->h = h;
    return 1;
}

/* -------------------------------------------------------------------- */
/*  Derived colors                                                       */
/* -------------------------------------------------------------------- */

static u32 Luma(uint32_t c) {
    return 2 * (c & 0xFF) + 5 * ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF);
}

static uint32_t BrightestPixel(const SecondScreenThemeSprite* s, uint32_t fallback) {
    uint32_t best = fallback;
    u32 bestLuma = 0;
    int32_t i;
    if (s == NULL || s->px == NULL) {
        return fallback;
    }
    for (i = 0; i < s->w * s->h; i++) {
        if (s->px[i] != 0 && Luma(s->px[i]) > bestLuma) {
            bestLuma = Luma(s->px[i]);
            best = s->px[i];
        }
    }
    return best;
}

static uint32_t CenterPixel(const SecondScreenThemeSprite* s, uint32_t fallback) {
    if (s == NULL || s->px == NULL) {
        return fallback;
    }
    {
        uint32_t c = s->px[(s->h / 2) * s->w + s->w / 2];
        return c != 0 ? c : fallback;
    }
}

/* Most chromatic pixel by a hue score — outlines/shadows (dark) and
 * sparkles (white) score ~0, so the pick lands on the art's actual body
 * color regardless of how many shades it spans. */
static uint32_t MostChromatic(const SecondScreenThemeSprite* s, int32_t (*score)(uint32_t), uint32_t fallback) {
    uint32_t best = fallback;
    int32_t bestScore = 0;
    int32_t i;
    if (s == NULL || s->px == NULL) {
        return fallback;
    }
    for (i = 0; i < s->w * s->h; i++) {
        uint32_t c = s->px[i];
        if (c != 0 && score(c) > bestScore) {
            bestScore = score(c);
            best = c;
        }
    }
    return best;
}

static int32_t GoldScore(uint32_t c) { /* warm-bright: high R+G, low B */
    return (int32_t)(c & 0xFF) + (int32_t)((c >> 8) & 0xFF) - 2 * (int32_t)((c >> 16) & 0xFF);
}

static int32_t GreenScore(uint32_t c) {
    return 2 * (int32_t)((c >> 8) & 0xFF) - (int32_t)(c & 0xFF) - (int32_t)((c >> 16) & 0xFF);
}

static void DeriveColors(void) {
    int32_t i;
    /* Border light/dark from the corner tile's real colors — used by the
     * procedural bits (cell plates, fallback frames) so they share the
     * window's own palette instead of guessing hues. */
    if (sChromeOk) {
        uint32_t light = sColors[SSC_BORDER_LIGHT], dark = sColors[SSC_BORDER_DARK];
        u32 lightLuma = 0, darkLuma = 0xFFFFFFFFu;
        for (i = 0; i < 64; i++) {
            uint32_t c = sChromeTiles[CHROME_CORNER][i];
            if (c == 0) {
                continue;
            }
            if (Luma(c) > lightLuma) {
                lightLuma = Luma(c);
                light = c;
            }
            if (Luma(c) < darkLuma) {
                darkLuma = Luma(c);
                dark = c;
            }
        }
        sColors[SSC_BORDER_LIGHT] = light;
        sColors[SSC_BORDER_DARK] = dark;
    }
    /* Gold from the maxed-counter digit font (the HUD's own gold text),
     * not the key sprite — the key's brightest pixel is its white
     * sparkle. */
    sColors[SSC_GOLD] = MostChromatic(&sSprites[SST_DIGIT_YELLOW_0 + 8], GoldScore, sColors[SSC_GOLD]);
    sColors[SSC_HEART_RED] = CenterPixel(&sSprites[SST_HEART_FULL], sColors[SSC_HEART_RED]);
    sColors[SSC_RUPEE_GREEN] = MostChromatic(&sSprites[SST_RUPEE_WALLET0], GreenScore, sColors[SSC_RUPEE_GREEN]);
    sColors[SSC_TEXT_LIGHT] = BrightestPixel(&sSprites[SST_DIGIT_WHITE_0], sColors[SSC_TEXT_LIGHT]);
}

/* -------------------------------------------------------------------- */
/*  Pause-menu layers (light theme)                                      */
/* -------------------------------------------------------------------- */

/* First EWRAM-destined record of a gfx group (the menu screens stage
 * their BG tilemaps through EWRAM buffers whose absolute address is a
 * per-region link detail, so records are matched by destination region —
 * the same rule port_second_screen_worldmap.c applies). Uncompressed
 * records only: both menu groups ship plain data on every known ROM. */
static const u8* MenuEwramBlob(u32 group, u32* outLen) {
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
        u32 ctrl = (raw0 >> 24) & 0xF;
        u32 dest = Port_ReadU32(rec + 4);
        s32 size = (s32)Port_ReadU32(rec + 8);
        if (ctrl == 0xD) {
            return NULL;
        }
        if ((dest >> 24) == 0x02u && size > 0) {
            *outLen = (u32)size;
            return gGlobalGfxAndPalettes + (raw0 & 0xFFFFFF);
        }
        if (((raw0 >> 24) & 0x80) == 0) {
            return NULL;
        }
    }
    return NULL;
}

/* Applies one palette group to a 16-bank BG palette (RGB555), skipping
 * OBJ banks — LoadPaletteGroup's effect on the BG half of gPaletteBuffer,
 * through the port's chain accessor. */
static void MenuApplyPaletteGroup(u32 group, uint16_t* bgPal) {
    u32 e;
    for (e = 0; e < 8; e++) {
        u32 numColors = 0, destBank = 0, c;
        const u8* p = Port_GetRawPaletteGroupEntryData(group, e, &numColors, &destBank);
        if (p == NULL) {
            return;
        }
        if (destBank >= 16) {
            continue; /* OBJ bank — the static layers are BG-only */
        }
        for (c = 0; c < numColors && destBank * 16u + c < 256u; c++) {
            bgPal[destBank * 16u + c] = (uint16_t)(p[c * 2] | (p[c * 2 + 1] << 8));
        }
    }
}

/* Paints one 32x20 text-BG tilemap into a 240x160 RGBA image. Color 0 is
 * skipped so the caller controls layering (backdrop color under BG3, BG3
 * under BG2 — both menu BGs sit at priority 3 with no scroll). */
static void MenuDrawLayer(uint32_t* dst, const u8* tilemap, u32 mapLen, const u8* tiles, u32 tilesLen,
                          const uint16_t* bgPal) {
    u32 tileCount = tilesLen / 32u;
    int32_t x, y;
    for (y = 0; y < MENU_SCREEN_H; y++) {
        u32 tileRow = ((u32)y >> 3) & 31u;
        int32_t rowInTile = y & 7;
        for (x = 0; x < MENU_SCREEN_W; x++) {
            u32 tileCol = ((u32)x >> 3) & 31u;
            u32 entryOff = (tileRow * 32u + tileCol) * 2u;
            u16 entry = (entryOff + 2u <= mapLen) ? Port_ReadU16(tilemap + entryOff) : 0;
            u32 tileId = entry & 0x3FFu;
            int32_t inX = x & 7, inY = rowInTile;
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
            dst[(size_t)y * MENU_SCREEN_W + (size_t)x] =
                Rgb555ToRgba8888(bgPal[(((u32)entry >> 12) & 0xFu) * 16u + colorIndex]);
        }
    }
}

/* Most common color of an image region (RGB444 buckets — plenty for GBA
 * color depth), used to sample the parchment/stone/well tones from the
 * composed layers instead of hardcoding them. */
static uint32_t ModeColor(const uint32_t* img, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                          uint32_t fallback) {
    static uint16_t hist[4096]; /* one-time build helper; zeroed per use */
    uint32_t bestBucket = 0xFFFFFFFFu;
    uint32_t bestCount = 0;
    int32_t x, y;
    memset(hist, 0, sizeof(hist));
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            uint32_t c = img[y * MENU_SCREEN_W + x];
            uint32_t b;
            if (c == 0) {
                continue;
            }
            b = ((c >> 4) & 0xF) | ((c >> 8) & 0xF0) | ((c >> 12) & 0xF00);
            if ((uint32_t)++hist[b] > bestCount) {
                bestCount = hist[b];
                bestBucket = b;
            }
        }
    }
    if (bestBucket == 0xFFFFFFFFu) {
        return fallback;
    }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            uint32_t c = img[y * MENU_SCREEN_W + x];
            uint32_t b = ((c >> 4) & 0xF) | ((c >> 8) & 0xF0) | ((c >> 12) & 0xF00);
            if (c != 0 && b == bestBucket) {
                return c;
            }
        }
    }
    return fallback;
}

/* Text color LUT for (fill_type, charColor) — GetTextColorTablePtr's
 * head/tail split (src/text.c): logical offsets below 160 read the
 * gUnk_0810942E head, the rest the gUnk_081094CE tail. 32 bytes: even
 * columns [0..15] (low nibble), odd columns [16..31] (pre-shifted). */
static const u8* TextLutPtr(u32 fillType, u32 charColor) {
    u32 logical = fillType * 0xC0u + charColor * 0x20u;
    return (logical < 160u) ? &gUnk_0810942E[logical] : &gUnk_081094CE[logical - 160u];
}

/* Replays sub_0805F918 for one border/fill scheme into a chip tile set —
 * identical pipeline to BuildChrome, but through the WINDOW color table
 * (gUnk_081094CE + fill*0xC0 directly, sub_0805F918's own indexing) so
 * outside pixels stay transparent and the interior takes the scheme's
 * fill color. Returns 1 when the corner tile decoded something. */
static int BuildChipSet(u32 borderType, u32 fillType, const uint16_t* pal, u32 numColors,
                        uint32_t out[CHROME_TILE_COUNT][64]) {
    const u8* shapes = (const u8*)gUnk_081092AC[borderType];
    const u8* lutE = &gUnk_081094CE[fillType * 0xC0u];
    const u8* lutO = lutE + 0x10;
    u32 sentinel = fillType * 0xC0u + 0xAAu;
    u8 head = (sentinel < 160u) ? gUnk_0810942E[sentinel] : gUnk_081094CE[sentinel - 160u];
    u32 fillIdx = gUnk_0810926C[head & 0x0Fu] & 0xFu;
    uint32_t fill = (fillIdx != 0 && fillIdx < numColors) ? Rgb555ToRgba8888(pal[fillIdx]) : 0xFF000000u;
    u8 unpacked[128];
    int32_t block, py, px, opaque = 0;

    if (shapes == NULL) {
        return 0;
    }
    for (block = 0; block < 3; block++) {
        UnpackTextNibbles((void*)(shapes + block * 0x40), unpacked);
        for (py = 0; py < 16; py++) {
            for (px = 0; px < 8; px++) {
                u32 pix = unpacked[py * 8 + px] & 0x0Fu;
                u32 colorIdx = (px & 1) ? (u32)(lutO[pix] >> 4) : (u32)(lutE[pix] & 0x0Fu);
                uint32_t rgba =
                    (colorIdx == 0 || colorIdx >= numColors) ? 0u : Rgb555ToRgba8888(pal[colorIdx]);
                out[block * 2 + (py >> 3)][(py & 7) * 8 + px] = rgba;
                if (block == 0 && py < 8 && rgba != 0) {
                    opaque++;
                }
            }
        }
    }
    for (py = 0; py < 64; py++) {
        out[CHROME_FILL][py] = fill;
    }
    return opaque >= 8;
}

/* Composes the pause item screen's two static layers and derives every
 * light-theme ingredient from them (slab bbox, doodle template, tones).
 * Each ingredient validates independently and degrades on its own. */
static void BuildMenuTheme(void) {
    uint16_t bgPal[16 * 16];
    /* LoadGfxGroups (11, 12) -> sub_080A4D34 (181) -> sub_080A4DB8 (182):
     * the exact load order on the way into the item screen. */
    static const u8 kMenuPaletteGroups[] = { 11u, 12u, 181u, MENU_PALETTE_GROUP };
    u32 slabTilesLen = 0, slabMapLen = 0, backTilesLen = 0, backMapLen = 0;
    const u8* slabTiles = Port_ResolveGfxGroupVram(MENU_GFX_GROUP, MENU_TILES_DEST, &slabTilesLen);
    const u8* slabMap = MenuEwramBlob(MENU_GFX_GROUP, &slabMapLen);
    const u8* backTiles = Port_ResolveGfxGroupVram(CHROME_GFX_GROUP, CHROME_TILES_DEST, &backTilesLen);
    const u8* backMap = MenuEwramBlob(CHROME_GFX_GROUP, &backMapLen);
    uint32_t backdrop;
    size_t i;
    int32_t x, y;

    sMenuOk = 0;
    if (slabTiles == NULL || slabMap == NULL || backTiles == NULL || backMap == NULL ||
        slabTilesLen < 32 || backTilesLen < 32) {
        return;
    }

    memset(bgPal, 0, sizeof(bgPal));
    for (i = 0; i < sizeof(kMenuPaletteGroups); i++) {
        MenuApplyPaletteGroup(kMenuPaletteGroups[i], bgPal);
    }

    /* Parchment layer: backdrop color 0 under the pattern (the pattern
     * tiles are fully opaque in practice; color 0 never shows). */
    backdrop = Rgb555ToRgba8888(bgPal[0]);
    for (i = 0; i < (size_t)(MENU_SCREEN_W * MENU_SCREEN_H); i++) {
        sMenuBg3[i] = backdrop;
        sMenuBg2[i] = 0;
    }
    MenuDrawLayer(sMenuBg3, backMap, backMapLen, backTiles, backTilesLen, bgPal);
    MenuDrawLayer(sMenuBg2, slabMap, slabMapLen, slabTiles, slabTilesLen, bgPal);
    sMenuOk = 1;

    /* Slab bbox from the layer's own opacity. */
    {
        int32_t minX = MENU_SCREEN_W, minY = MENU_SCREEN_H, maxX = -1, maxY = -1;
        for (y = 0; y < MENU_SCREEN_H; y++) {
            for (x = 0; x < MENU_SCREEN_W; x++) {
                if (sMenuBg2[y * MENU_SCREEN_W + x] != 0) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        sSlabX0 = minX;
        sSlabY0 = minY;
        sSlabX1 = maxX + 1;
        sSlabY1 = maxY + 1;
        sSlabOk = (maxX - minX) >= 160 && (maxY - minY) >= 96;
    }

    /* Colors first (the doodle/tray checks compare against them). */
    sColors[SSC_MENU_CREAM] = ModeColor(sMenuBg3, 0, 0, MENU_SCREEN_W, MENU_SCREEN_H,
                                        sColors[SSC_MENU_CREAM]);
    if (sSlabOk) {
        sColors[SSC_MENU_STONE] =
            ModeColor(sMenuBg2, sSlabX0, sSlabY0, sSlabX1, sSlabY1, sColors[SSC_MENU_STONE]);
    }
    sColors[SSC_MENU_STONE_DARK] = ModeColor(sMenuBg2, TRAY_X0 + WELL_CORNER, TRAY_Y0 + WELL_CORNER,
                                             TRAY_X1 - WELL_CORNER, TRAY_Y1 - WELL_CORNER,
                                             sColors[SSC_MENU_STONE_DARK]);

    /* Bottle-tray well: all four rim corners plus the center must be
     * opaque slab pixels, or wells fall back to flat tones. The tray is
     * copied out now because the slab interior is re-paved next. */
    sTrayOk = sSlabOk && TRAY_X0 >= sSlabX0 && TRAY_X1 <= sSlabX1 && TRAY_Y0 >= sSlabY0 &&
              TRAY_Y1 <= sSlabY1 && sMenuBg2[TRAY_Y0 * MENU_SCREEN_W + TRAY_X0] != 0 &&
              sMenuBg2[TRAY_Y0 * MENU_SCREEN_W + TRAY_X1 - 1] != 0 &&
              sMenuBg2[(TRAY_Y1 - 1) * MENU_SCREEN_W + TRAY_X0] != 0 &&
              sMenuBg2[(TRAY_Y1 - 1) * MENU_SCREEN_W + TRAY_X1 - 1] != 0 &&
              sMenuBg2[((TRAY_Y0 + TRAY_Y1) / 2) * MENU_SCREEN_W + (TRAY_X0 + TRAY_X1) / 2] != 0;
    for (y = 0; y < TRAY_H; y++) {
        for (x = 0; x < TRAY_W; x++) {
            sTrayPx[y * TRAY_W + x] = sMenuBg2[(TRAY_Y0 + y) * MENU_SCREEN_W + TRAY_X0 + x];
        }
    }

    /* Re-pave the slab interior with its own plain stone so plate slices
     * never repeat the wells: tile the clean patch over everything inside
     * the carved bands. The patch must be pure stone (no rim/ink pixels)
     * or a flat stone fill substitutes. */
    if (sSlabOk) {
        uint32_t patch[STONE_PATCH_W * STONE_PATCH_H];
        int patchOk = 1;
        u32 stoneLuma = Luma(sColors[SSC_MENU_STONE]);
        for (y = 0; y < STONE_PATCH_H; y++) {
            for (x = 0; x < STONE_PATCH_W; x++) {
                uint32_t c = sMenuBg2[(STONE_PATCH_Y + y) * MENU_SCREEN_W + STONE_PATCH_X + x];
                patch[y * STONE_PATCH_W + x] = c;
                if (c == 0 || Luma(c) * 4 < stoneLuma * 3) {
                    patchOk = 0; /* hit a rim/outline — not plain stone */
                }
            }
        }
        for (y = sSlabY0 + SLAB_TOP_H; y < sSlabY1 - SLAB_BOTTOM_H; y++) {
            for (x = sSlabX0 + SLAB_SIDE_W; x < sSlabX1 - SLAB_SIDE_W; x++) {
                sMenuBg2[y * MENU_SCREEN_W + x] =
                    patchOk ? patch[(y % STONE_PATCH_H) * STONE_PATCH_W + (x % STONE_PATCH_W)]
                            : sColors[SSC_MENU_STONE];
            }
        }
    }

    /* Doodle template: cut the known full doodle off the parchment; its
     * outer ring must be (nearly) flat cream and the cut must contain a
     * plausible amount of ink, otherwise the backdrop stays plain. */
    {
        uint32_t cream = sColors[SSC_MENU_CREAM];
        int32_t inked = 0, ringInk = 0;
        for (y = 0; y < DOODLE_H; y++) {
            for (x = 0; x < DOODLE_W; x++) {
                uint32_t c = sMenuBg3[(DOODLE_SRC_Y + y) * MENU_SCREEN_W + DOODLE_SRC_X + x];
                sDoodle[y * DOODLE_W + x] = (c == cream) ? 0u : c;
                if (c != cream) {
                    inked++;
                }
            }
        }
        for (x = -1; x <= DOODLE_W; x++) {
            if (sMenuBg3[(DOODLE_SRC_Y - 1) * MENU_SCREEN_W + DOODLE_SRC_X + x] != cream) ringInk++;
            if (sMenuBg3[(DOODLE_SRC_Y + DOODLE_H) * MENU_SCREEN_W + DOODLE_SRC_X + x] != cream) ringInk++;
        }
        for (y = 0; y < DOODLE_H; y++) {
            if (sMenuBg3[(DOODLE_SRC_Y + y) * MENU_SCREEN_W + DOODLE_SRC_X - 1] != cream) ringInk++;
            if (sMenuBg3[(DOODLE_SRC_Y + y) * MENU_SCREEN_W + DOODLE_SRC_X + DOODLE_W] != cream) ringInk++;
        }
        sDoodleOk = inked >= 300 && inked <= 1000 && ringInk <= 8;
    }

    /* Chips + text colors come from the message palette (BG bank 15). */
    {
        u32 numColors = 0;
        const u8* raw = Port_GetRawPaletteGroupData(HUD_PALETTE_GROUP, &numColors);
        if (raw != NULL && numColors >= 16) {
            const uint16_t* pal = (const uint16_t*)raw;
            for (i = 0; i < 16; i++) {
                sMsgPal[i] = Rgb555ToRgba8888(((const u8*)raw)[i * 2] | ((const u8*)raw)[i * 2 + 1] << 8);
            }
            sChipTilesOk[SS_CHIP_DARK] = BuildChipSet(9, 0, pal, 16, sChipTiles[SS_CHIP_DARK]);
            sChipTilesOk[SS_CHIP_RED] = BuildChipSet(9, 1, pal, 16, sChipTiles[SS_CHIP_RED]);
            /* Ink/black/white/red: the exact colors the text LUT styles
             * resolve to (fill 7 body = index 11, black 15, white 14,
             * red body = index 8 — see the tail table's charColor maps). */
            sColors[SSC_MENU_INK] = sMsgPal[11];
            sColors[SSC_MENU_BLACK] = sMsgPal[15];
            sColors[SSC_MENU_WHITE] = sMsgPal[14];
            sColors[SSC_MENU_RED] = sMsgPal[8];
        }
    }

    /* Message font: glyph bank 0 plus the style LUT pairs. Bank 0 is
     * the latin/ASCII face on USA and EU (sub_0805F25C routes plain A-Z
     * there for every EU language); the JP ROM's bank 0 is its own script,
     * so JP keeps the procedural label fallback instead of mojibake. */
    {
        static const u8 kStyleFill[SS_TEXT_STYLE_COUNT] = { 7, 5, 5, 5, 7 };
        static const u8 kStyleColor[SS_TEXT_STYLE_COUNT] = { 0, 0, 1, 2, 0 };
        sFontGlyphs = (const u8*)gUnk_08109248[0];
        for (i = 0; i < SS_TEXT_STYLE_COUNT; i++) {
            memcpy(sTextLut[i], TextLutPtr(kStyleFill[i], kStyleColor[i]), 32);
        }
        sFontOk = sFontGlyphs != NULL && !REGION_IS_JP;
    }

    /* Stylized banner font (bank 8) + its per-style color tables. The
     * authentic scheme is the identity map through BG bank 1 as composed
     * above (same bank state on the item and map screens — group 181's
     * span covers it); the other styles recolor the glyph value roles
     * (see the sBigFontGlyphs comment) with message-palette colors so the
     * same letterforms read as ink/red/green/navy on light surfaces.
     * Same JP guard as bank 0: the JP stylized bank is kana. */
    {
        uint32_t bank1[16];
        for (i = 0; i < 16; i++) {
            bank1[i] = Rgb555ToRgba8888(bgPal[16 + i]);
        }
        memset(sBigPal, 0, sizeof(sBigPal));
        for (i = 1; i < 16; i++) {
            sBigPal[SS_TEXT_WHITE][i] = bank1[i]; /* the banner's own colors */
        }
        /* Role slots: 14 body, 12/13 (+10/11) shades, 1 outline, 15 rim. */
        sBigPal[SS_TEXT_INK][14] = sMsgPal[15];
        sBigPal[SS_TEXT_INK][12] = sBigPal[SS_TEXT_INK][13] = sBigPal[SS_TEXT_INK][10] =
            sBigPal[SS_TEXT_INK][11] = sMsgPal[11];
        sBigPal[SS_TEXT_RED][14] = sMsgPal[8];
        sBigPal[SS_TEXT_RED][12] = sBigPal[SS_TEXT_RED][13] = sBigPal[SS_TEXT_RED][10] =
            sBigPal[SS_TEXT_RED][11] = sMsgPal[7];
        sBigPal[SS_TEXT_GREEN][14] = sMsgPal[2];
        sBigPal[SS_TEXT_GREEN][12] = sBigPal[SS_TEXT_GREEN][13] = sBigPal[SS_TEXT_GREEN][10] =
            sBigPal[SS_TEXT_GREEN][11] = sMsgPal[1];
        sBigPal[SS_TEXT_NAVY][14] = bank1[1];
        sBigPal[SS_TEXT_NAVY][12] = sBigPal[SS_TEXT_NAVY][13] = sBigPal[SS_TEXT_NAVY][10] =
            sBigPal[SS_TEXT_NAVY][11] = bank1[2];
        sColors[SSC_BANNER_NAVY] = bank1[1];
        sBigFontGlyphs = (const u8*)gUnk_08109248[8];
        sBigFontOk = sBigFontGlyphs != NULL && !REGION_IS_JP;
    }
}

/* -------------------------------------------------------------------- */
/*  Menu button (the quest screen's SLEEP / SAVE plate)                  */
/* -------------------------------------------------------------------- */

/* The three tones of the plate, read out of the art rather than named by
 * palette index: over the plate's interior the fill (near-white) is by far
 * the most common color, the letter body next, and the letters' highlight
 * after that. Ordering the two letter tones by luma tells body from
 * highlight without assuming which palette slot either lives in. */
static void SampleButtonTones(const uint32_t* px, int32_t stride, int32_t x0, int32_t y0, int32_t w,
                              int32_t h) {
    uint32_t colors[24];
    uint32_t counts[24];
    int32_t n = 0, x, y, i, best[3] = { -1, -1, -1 };

    for (y = y0; y < y0 + h; y++) {
        for (x = x0; x < x0 + w; x++) {
            uint32_t c = px[(size_t)y * (size_t)stride + x];
            if ((c >> 24) == 0) {
                continue;
            }
            for (i = 0; i < n; i++) {
                if (colors[i] == c) {
                    counts[i]++;
                    break;
                }
            }
            if (i == n && n < (int32_t)(sizeof(colors) / sizeof(colors[0]))) {
                colors[n] = c;
                counts[n] = 1;
                n++;
            }
        }
    }
    for (i = 0; i < 3; i++) {
        int32_t k, pick = -1;
        for (k = 0; k < n; k++) {
            if (k == best[0] || k == best[1]) {
                continue;
            }
            if (pick < 0 || counts[k] > counts[pick]) {
                pick = k;
            }
        }
        best[i] = pick;
    }
    if (best[0] >= 0) {
        sBtnFill = colors[best[0]];
    }
    if (best[1] >= 0 && best[2] >= 0) {
        uint32_t a = colors[best[1]], b = colors[best[2]];
        sBtnInk = (Luma(a) <= Luma(b)) ? a : b;
        sBtnHi = (Luma(a) <= Luma(b)) ? b : a;
    } else if (best[1] >= 0) {
        sBtnInk = sBtnHi = colors[best[1]];
    }
}

/* Composes the plate + its selection bracket and derives the label colors.
 * Everything validates on its own: a missing piece just leaves sBtnOk 0 and
 * DrawMenuButton reports "not authentic" so the caller keeps its fallback. */
static void BuildMenuButton(void) {
    int32_t x, y, minX, minY, maxX, maxY;
    const uint32_t* px;

    sBtnOk = 0;
    if (!BuildSpriteComposite(SPRITE_PAUSE_MISC, MENU_BUTTON_FRAME, 0, 0, kQuestObjGfxGroups,
                              sizeof(kQuestObjGfxGroups), &sBtnPlate)) {
        return;
    }
    px = sBtnPlate.px;

    /* The composite's bbox is the OBJ pieces' (32 px tall); the plate is the
     * opaque part inside it. */
    minX = sBtnPlate.w;
    minY = sBtnPlate.h;
    maxX = -1;
    maxY = -1;
    for (y = 0; y < sBtnPlate.h; y++) {
        for (x = 0; x < sBtnPlate.w; x++) {
            if ((px[(size_t)y * (size_t)sBtnPlate.w + x] >> 24) != 0) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    sBtnX0 = minX;
    sBtnY0 = minY;
    sBtnW = maxX - minX + 1;
    sBtnH = maxY - minY + 1;
    if (sBtnW < 4 * MENU_BUTTON_CORNER || sBtnH < 4 * MENU_BUTTON_CORNER) {
        return;
    }

    sBtnFill = sColors[SSC_MENU_WHITE];
    sBtnInk = sColors[SSC_BANNER_NAVY];
    sBtnHi = sColors[SSC_BORDER_LIGHT];
    SampleButtonTones(px, sBtnPlate.w, sBtnX0 + MENU_BUTTON_CORNER, sBtnY0 + MENU_BUTTON_CORNER,
                      sBtnW - 2 * MENU_BUTTON_CORNER, sBtnH - 2 * MENU_BUTTON_CORNER);

    /* Re-letter arbitrary labels in the plate's own scheme: the stylized
     * font's body role takes the letters' body tone, its shade roles the
     * highlight; the plate's lettering carries no outline, so the outline
     * and rim roles stay transparent (same shape SS_TEXT_INK uses). */
    memset(sBtnPal, 0, sizeof(sBtnPal));
    sBtnPal[14] = sBtnInk;
    sBtnPal[10] = sBtnPal[11] = sBtnPal[12] = sBtnPal[13] = sBtnHi;

    /* The column and row of pure fill the stretched edges repeat — the plate
     * carries its label, so the slice sources have to dodge the lettering. */
    sBtnCleanCol = -1;
    sBtnCleanRow = -1;
    for (x = MENU_BUTTON_CORNER; x < sBtnW - MENU_BUTTON_CORNER && sBtnCleanCol < 0; x++) {
        int clean = 1;
        for (y = MENU_BUTTON_CORNER; y < sBtnH - MENU_BUTTON_CORNER; y++) {
            if (px[(size_t)(sBtnY0 + y) * (size_t)sBtnPlate.w + sBtnX0 + x] != sBtnFill) {
                clean = 0;
                break;
            }
        }
        if (clean) {
            sBtnCleanCol = x;
        }
    }
    for (y = MENU_BUTTON_CORNER; y < sBtnH - MENU_BUTTON_CORNER && sBtnCleanRow < 0; y++) {
        int clean = 1;
        for (x = MENU_BUTTON_CORNER; x < sBtnW - MENU_BUTTON_CORNER; x++) {
            if (px[(size_t)(sBtnY0 + y) * (size_t)sBtnPlate.w + sBtnX0 + x] != sBtnFill) {
                clean = 0;
                break;
            }
        }
        if (clean) {
            sBtnCleanRow = y;
        }
    }
    if (sBtnCleanCol < 0 || sBtnCleanRow < 0) {
        return;
    }

    BuildSpriteComposite(SPRITE_PAUSE_MISC, MENU_BRACKET_FRAME, 0, 0, kQuestObjGfxGroups,
                         sizeof(kQuestObjGfxGroups), &sBtnBracket);
    sBtnOk = 1;
}

/* -------------------------------------------------------------------- */
/*  Build                                                                */
/* -------------------------------------------------------------------- */

static void BuildAll(const uint16_t* hudPal, u32 hudColors) {
    int32_t i;

    BuildChrome(hudPal, hudColors);

    /* Hearts: empty (0x11), quarter fills (0x12..0x14), full (0x15) —
     * the series order DrawHearts' data uses. */
    for (i = 0; i < 5; i++) {
        static const int kIds[5] = { SST_HEART_EMPTY, SST_HEART_Q1, SST_HEART_Q2, SST_HEART_Q3, SST_HEART_FULL };
        static const u32 kTiles[5] = { TILE_HEART_EMPTY, TILE_HEART_EMPTY + 1, TILE_HEART_EMPTY + 2,
                                       TILE_HEART_EMPTY + 3, TILE_HEART_FULL };
        const u8* t = HudTileBytes(kTiles[i], 1);
        uint32_t* out = t ? ArenaAlloc(64) : NULL;
        if (out != NULL) {
            DecodeTile4bpp(t, hudPal, hudColors, out);
            sSprites[kIds[i]].px = out;
            sSprites[kIds[i]].w = 8;
            sSprites[kIds[i]].h = 8;
        }
    }

    /* Rupee icons per wallet tier + the small-key icon (2x2 tiles). */
    for (i = 0; i < 4; i++) {
        const u8* t = HudTileBytes(TILE_RUPEE_W0 + (u32)i * 4u, 4);
        const uint32_t* out = t ? DecodeIcon2x2(t, hudPal, hudColors) : NULL;
        if (out != NULL) {
            sSprites[SST_RUPEE_WALLET0 + i].px = out;
            sSprites[SST_RUPEE_WALLET0 + i].w = 16;
            sSprites[SST_RUPEE_WALLET0 + i].h = 16;
        }
    }
    {
        const u8* t = HudTileBytes(TILE_KEY, 4);
        const uint32_t* out = t ? DecodeIcon2x2(t, hudPal, hudColors) : NULL;
        if (out != NULL) {
            sSprites[SST_KEY].px = out;
            sSprites[SST_KEY].w = 16;
            sSprites[SST_KEY].h = 16;
        }
    }

    /* Counter digits, 8x16 (two stacked tiles per glyph), white + yellow. */
    for (i = 0; i < 20; i++) {
        const u8* glyph = gUnk_085C4620 + ((i < 10) ? 0x280u : 0x500u) + (u32)(i % 10) * 0x40u;
        uint32_t* out = ArenaAlloc(8 * 16);
        if (out != NULL) {
            DecodeTile4bpp(glyph, hudPal, hudColors, out);
            DecodeTile4bpp(glyph + 32, hudPal, hudColors, out + 64);
            sSprites[SST_DIGIT_WHITE_0 + i].px = out;
            sSprites[SST_DIGIT_WHITE_0 + i].w = 8;
            sSprites[SST_DIGIT_WHITE_0 + i].h = 16;
        }
    }

    /* Ammo-count digits, 8x8: tens glyphs [0..9], ones glyphs [10..19].
     * These are OBJ tiles (the counter pieces of sprite 322's ammo item
     * frames), drawn with the OBJ bank the pieces select — not the BG
     * HUD palette. */
    {
        const uint16_t* smallPal = ObjPalBank(SMALL_DIGIT_OBJ_BANK);
        for (i = 0; smallPal != NULL && i < 20; i++) {
            uint32_t* out = ArenaAlloc(64);
            if (out != NULL) {
                DecodeTile4bpp(gUnk_085C4620 + (u32)i * 32u, smallPal, 16, out);
                sSprites[SST_SMALL_TENS_0 + i].px = out;
                sSprites[SST_SMALL_TENS_0 + i].w = 8;
                sSprites[SST_SMALL_TENS_0 + i].h = 8;
            }
        }
    }

    /* HUD A/B button bubbles (slot-relative tiles at the button element's
     * fixed VRAM block) + the pause menu's blinking equip cursor
     * (VRAM-absolute tiles; the game draws it with command bank 0). */
    const u16 hudButtonSprite = Port_RemapSpriteIndex(SPRITE_HUD_BUTTONS_RAW);
    BuildSpriteComposite(hudButtonSprite, 0, BUTTON_VRAM_SLOT, 0, kHudObjGfxGroups,
                         sizeof(kHudObjGfxGroups), &sSprites[SST_BUTTON_A]);
    BuildSpriteComposite(hudButtonSprite, 1, BUTTON_VRAM_SLOT, 0, kHudObjGfxGroups,
                         sizeof(kHudObjGfxGroups), &sSprites[SST_BUTTON_B]);
    BuildSpriteComposite(hudButtonSprite, BUTTON_R_FRAME, BUTTON_VRAM_SLOT, 0, kHudObjGfxGroups,
                         sizeof(kHudObjGfxGroups), &sSprites[SST_BUTTON_R]);
    BuildSpriteComposite(SPRITE_PAUSE_MISC, CURSOR_FRAME_0, 0, 0, kPauseObjGfxGroups,
                         sizeof(kPauseObjGfxGroups), &sSprites[SST_CURSOR_0]);
    BuildSpriteComposite(SPRITE_PAUSE_MISC, CURSOR_FRAME_1, 0, 0, kPauseObjGfxGroups,
                         sizeof(kPauseObjGfxGroups), &sSprites[SST_CURSOR_1]);

    DeriveColors();
    BuildMenuTheme();
    BuildMenuButton();
}

int Port_SecondScreenTheme_Ready(void) {
    u32 hudColors = 0;
    const u8* hudPal;

    if (sBuilt) {
        return 1;
    }
    /* Neutral stand-ins until the ROM tables resolve; overwritten by
     * DeriveColors / BuildMenuTheme. These are design placeholders of
     * ours, not game data — the menu set is a plausible parchment/stone
     * scheme so the panel doesn't flash dark before the decode lands. */
    sColors[SSC_WINDOW_FILL] = 0xFF282420u;
    sColors[SSC_BORDER_LIGHT] = 0xFF78B4C8u;
    sColors[SSC_BORDER_DARK] = 0xFF101820u;
    sColors[SSC_GOLD] = 0xFF40C8E8u;
    sColors[SSC_HEART_RED] = 0xFF3030E8u;
    sColors[SSC_RUPEE_GREEN] = 0xFF58C848u;
    sColors[SSC_TEXT_LIGHT] = 0xFFF0F0F0u;
    sColors[SSC_MENU_CREAM] = 0xFFA8ECF0u;      /* warm parchment */
    sColors[SSC_MENU_STONE] = 0xFFD0CCD4u;      /* pale stone */
    sColors[SSC_MENU_STONE_DARK] = 0xFFC0C0C4u; /* recessed stone */
    sColors[SSC_MENU_INK] = 0xFF383040u;        /* dark warm ink */
    sColors[SSC_MENU_BLACK] = 0xFF000000u;
    sColors[SSC_MENU_WHITE] = 0xFFF8F8F8u;
    sColors[SSC_MENU_RED] = 0xFF2838C8u;        /* brick red */
    sColors[SSC_BANNER_NAVY] = 0xFF903018u;     /* dark navy */

    /* The one ingredient everything needs: the HUD/message palette. If
     * it (or the HUD tiles) aren't resolved yet the ROM isn't ready —
     * report not-ready and retry on a later frame. Callers only ask
     * during gameplay, so in practice this succeeds on the first call. */
    hudPal = Port_GetRawPaletteGroupData(HUD_PALETTE_GROUP, &hudColors);
    if (hudPal == NULL || hudColors < 16 || HudTileBytes(TILE_HEART_FULL, 1) == NULL) {
        return 0;
    }

    BuildAll((const uint16_t*)hudPal, hudColors > 16 ? 16 : hudColors);
    sBuilt = 1;
    return 1;
}

const SecondScreenThemeSprite* Port_SecondScreenTheme_Get(int id) {
    if (!sBuilt || id < 0 || id >= SST_COUNT || sSprites[id].px == NULL) {
        return NULL;
    }
    return &sSprites[id];
}

uint32_t Port_SecondScreenTheme_Color(int id) {
    if (id < 0 || id >= SSC_COUNT) {
        return 0xFF000000u;
    }
    return sColors[id];
}

const uint16_t* Port_SecondScreenTheme_ObjPalette(uint32_t bank) {
    return ObjPalBank(bank);
}

/* -------------------------------------------------------------------- */
/*  Window drawing                                                       */
/* -------------------------------------------------------------------- */

static void FillRectTheme(uint32_t* px, int32_t bufW, int32_t bufH, int32_t stride, int32_t x0, int32_t y0,
                          int32_t x1, int32_t y1, uint32_t color) {
    int32_t x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > bufW) x1 = bufW;
    if (y1 > bufH) y1 = bufH;
    for (y = y0; y < y1; y++) {
        uint32_t* row = px + (size_t)y * (size_t)stride;
        for (x = x0; x < x1; x++) {
            row[x] = color;
        }
    }
}

/* Nearest-neighbor integer-scale blit of one chrome tile with optional
 * mirroring, clipped to [cx0,cy0)x(cx1,cy1) so edge runs can end on a
 * partial tile without ever scaling the art non-integrally. */
static void BlitChromeTile(uint32_t* px, int32_t bufW, int32_t bufH, int32_t stride, int tile, int32_t x,
                           int32_t y, int32_t ts, int hflip, int vflip, int32_t cx0, int32_t cy0, int32_t cx1,
                           int32_t cy1) {
    const uint32_t* src = sChromeTiles[tile];
    int32_t sx, sy, ex, ey;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > bufW) cx1 = bufW;
    if (cy1 > bufH) cy1 = bufH;
    for (sy = 0; sy < 8; sy++) {
        int32_t srcY = vflip ? 7 - sy : sy;
        for (sx = 0; sx < 8; sx++) {
            int32_t srcX = hflip ? 7 - sx : sx;
            uint32_t c = src[srcY * 8 + srcX];
            if (c == 0) {
                continue;
            }
            for (ey = 0; ey < ts; ey++) {
                int32_t dy = y + sy * ts + ey;
                if (dy < cy0 || dy >= cy1) {
                    continue;
                }
                for (ex = 0; ex < ts; ex++) {
                    int32_t dx = x + sx * ts + ex;
                    if (dx < cx0 || dx >= cx1) {
                        continue;
                    }
                    px[(size_t)dy * (size_t)stride + dx] = c;
                }
            }
        }
    }
}

void Port_SecondScreenTheme_DrawWindow(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                       int32_t x, int32_t y, int32_t w, int32_t h, int32_t tileScale) {
    int32_t t, cx;
    if (tileScale < 1) {
        tileScale = 1;
    }
    t = 8 * tileScale;

    if (!sChromeOk || w < 3 * t || h < 3 * t) {
        /* Frame fallback in the window's own (or neutral) colors. */
        uint32_t dark = sColors[SSC_BORDER_DARK], light = sColors[SSC_BORDER_LIGHT];
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + w, y + h, sColors[SSC_WINDOW_FILL]);
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + w, y + tileScale, light);
        FillRectTheme(pixels, bufW, bufH, stride, x, y + h - tileScale, x + w, y + h, light);
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + tileScale, y + h, light);
        FillRectTheme(pixels, bufW, bufH, stride, x + w - tileScale, y, x + w, y + h, light);
        FillRectTheme(pixels, bufW, bufH, stride, x + tileScale, y + tileScale, x + w - tileScale,
                      y + h - tileScale, sColors[SSC_WINDOW_FILL]);
        (void)dark;
        return;
    }

    /* Interior: extend the fill halfway under the border ring so the
     * border art's inner pixels always meet fill, never the backdrop
     * (the corner tiles' outer rounding stays in the untouched half). */
    FillRectTheme(pixels, bufW, bufH, stride, x + t / 2, y + t / 2, x + w - t / 2, y + h - t / 2,
                  sColors[SSC_WINDOW_FILL]);

    /* DispMessageFrame's arrangement: a corner tile plus its adjacent
     * edge tile at each end, straight tiles between (the last straight is
     * clipped, never rescaled), the far ends mirrored via flips exactly
     * like the tilemap's flip flags. */
    /* Top and bottom rows. */
    {
        int32_t xr = x + w - 2 * t; /* mirrored right-end pair starts here */
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_CORNER, x, y, tileScale, 0, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_CORNER, x + w - t, y, tileScale, 1, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_CORNER, x, y + h - t, tileScale, 0, 1, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_CORNER, x + w - t, y + h - t, tileScale, 1, 1, 0, 0,
                       bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_CORNER, x + t, y, tileScale, 0, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_CORNER, xr, y, tileScale, 1, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_CORNER, x + t, y + h - t, tileScale, 0, 1, 0, 0, bufW,
                       bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_CORNER, xr, y + h - t, tileScale, 1, 1, 0, 0, bufW,
                       bufH);
        for (cx = x + 2 * t; cx < xr; cx += t) {
            BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_STRAIGHT, cx, y, tileScale, 0, 0, 0, 0, xr, bufH);
            BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_STRAIGHT, cx, y + h - t, tileScale, 0, 1, 0, 0,
                           xr, bufH);
        }
    }
    /* Left and right columns. */
    {
        int32_t yb = y + h - 2 * t; /* mirrored bottom-end pair starts here */
        int32_t cy;
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_CORNER, x, y + t, tileScale, 0, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_CORNER, x + w - t, y + t, tileScale, 1, 0, 0, 0, bufW,
                       bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_CORNER, x, yb, tileScale, 0, 1, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_CORNER, x + w - t, yb, tileScale, 1, 1, 0, 0, bufW,
                       bufH);
        for (cy = y + 2 * t; cy < yb; cy += t) {
            BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_STRAIGHT, x, cy, tileScale, 0, 0, 0, 0, bufW, yb);
            BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_STRAIGHT, x + w - t, cy, tileScale, 1, 0, 0, 0,
                           bufW, yb);
        }
    }
}

/* -------------------------------------------------------------------- */
/*  Pause-menu drawing (light theme)                                     */
/* -------------------------------------------------------------------- */

int Port_SecondScreenTheme_MenuReady(void) {
    return sBuilt && sMenuOk && sSlabOk;
}

void Port_SecondScreenTheme_SetBackdropStyle(int style) {
    sBackdropStyle =
        (style > SS_BACKDROP_PARCHMENT && style < SS_BACKDROP_COUNT) ? style : SS_BACKDROP_PARCHMENT;
}

/* a -> b by t/255. Per-channel, alpha left at a's — every color here is
 * opaque, and carrying the blend into alpha would only round it down. */
static uint32_t MixRGBA(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t out = a & 0xFF000000u;
    int shift;
    for (shift = 0; shift <= 16; shift += 8) {
        uint32_t ca = (a >> shift) & 0xFFu, cb = (b >> shift) & 0xFFu;
        out |= ((ca * (255u - t) + cb * t) / 255u) << shift;
    }
    return out;
}

uint32_t Port_SecondScreenTheme_BackdropColor(void) {
    /* Cream comes from the decoded BG3 flat (a neutral stand-in before the
     * build); the dark one is ours and fixed, so it is the same near-black
     * whether or not the ROM has been parsed yet. The mid-tones are the
     * chrome's own carving colors, so they track the ROM's palette rather
     * than being literals that could drift away from the plates. */
    switch (sBackdropStyle) {
        case SS_BACKDROP_DARK:  return SS_BACKDROP_DARK_RGBA;
        case SS_BACKDROP_DIM:   return MixRGBA(sColors[SSC_MENU_CREAM], sColors[SSC_MENU_INK],
                                               SS_BACKDROP_DIM_MIX);
        case SS_BACKDROP_STONE: return sColors[SSC_MENU_STONE];
        case SS_BACKDROP_SLATE: return sColors[SSC_MENU_STONE_DARK];
        case SS_BACKDROP_NAVY:  return sColors[SSC_BANNER_NAVY];
        default:                return sColors[SSC_MENU_CREAM];
    }
}

int Port_SecondScreenTheme_BackdropIsDark(void) {
    /* Rec.601 luma on the actual fill, so a style is "dark" because of how
     * bright it is rather than because it was listed as one. The mid-tones
     * straddle this: the cream sits far above it and the near-black far
     * below, so the threshold only has to be sane for the middle — half
     * scale, i.e. ink-on-light above, light-on-ink below. */
    uint32_t c = Port_SecondScreenTheme_BackdropColor();
    uint32_t r = c & 0xFFu, g = (c >> 8) & 0xFFu, b = (c >> 16) & 0xFFu;
    return (299u * r + 587u * g + 114u * b) / 1000u < 128u;
}

void Port_SecondScreenTheme_DrawBackdrop(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t scale) {
    if (scale < 1) {
        scale = 1;
    }
    FillRectTheme(pixels, bufW, bufH, stride, x0, y0, x1, y1, Port_SecondScreenTheme_BackdropColor());
    if ((sBackdropStyle != SS_BACKDROP_PARCHMENT && sBackdropStyle != SS_BACKDROP_DIM) || !sMenuOk ||
        !sDoodleOk) {
        /* Flat fill and done: that IS the CREAM, DARK, STONE, SLATE and
         * NAVY styles, and it is also what the two lattice styles look
         * like until the pattern decodes. */
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > bufW) x1 = bufW;
    if (y1 > bufH) y1 = bufH;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    /* Doodle lattice walk. Solving [dx;dy] = i*A + j*B for the pattern-
     * space rect corners gives the (i, j) window to stamp (det(A,B) =
     * -3072): i = (dy - 2 dx) / 128, j = (2 dx + 7 dy) / 384. Pattern
     * space is surface pixels / scale, anchored at the surface origin so
     * every panel shares one continuous pattern. */
    {
        int dim = (sBackdropStyle == SS_BACKDROP_DIM);
        uint32_t dimTo = sColors[SSC_MENU_INK];
        float px0 = (float)x0 / scale - DOODLE_W, py0 = (float)y0 / scale - DOODLE_H;
        float px1 = (float)x1 / scale + 1, py1 = (float)y1 / scale + 1;
        float corner[4][2] = { { px0, py0 }, { px1, py0 }, { px0, py1 }, { px1, py1 } };
        int32_t iMin = 0x7FFF, iMax = -0x7FFF, jMin = 0x7FFF, jMax = -0x7FFF, i, j;
        int c;
        for (c = 0; c < 4; c++) {
            float dx = corner[c][0] - DOODLE_SRC_X;
            float dy = corner[c][1] - DOODLE_SRC_Y;
            /* floorf, not the int cast: truncation rounds negatives UP
             * and dropped a doodle row/column near the surface origin,
             * which read as a bare seam along the top/left edges. */
            int32_t fi = (int32_t)floorf((dy - 2.0f * dx) / 128.0f);
            int32_t fj = (int32_t)floorf((2.0f * dx + 7.0f * dy) / 384.0f);
            if (fi - 1 < iMin) iMin = fi - 1;
            if (fi + 1 > iMax) iMax = fi + 1;
            if (fj - 1 < jMin) jMin = fj - 1;
            if (fj + 1 > jMax) jMax = fj + 1;
        }
        for (j = jMin; j <= jMax; j++) {
            for (i = iMin; i <= iMax; i++) {
                int32_t dpx = (DOODLE_SRC_X + i * DOODLE_AX + j * DOODLE_BX) * scale;
                int32_t dpy = (DOODLE_SRC_Y + i * DOODLE_AY + j * DOODLE_BY) * scale;
                int32_t sx, sy, ex, ey;
                if (dpx >= x1 || dpy >= y1 || dpx + DOODLE_W * scale <= x0 ||
                    dpy + DOODLE_H * scale <= y0) {
                    continue;
                }
                for (sy = 0; sy < DOODLE_H; sy++) {
                    for (sx = 0; sx < DOODLE_W; sx++) {
                        uint32_t col = sDoodle[sy * DOODLE_W + sx];
                        if (col == 0) {
                            continue;
                        }
                        if (dim) {
                            /* Same pull as the fill under it, so the
                             * lattice keeps its contrast against the cream
                             * instead of standing out as the one thing
                             * that did not dim. Once per source pixel, not
                             * per scaled one. */
                            col = MixRGBA(col, dimTo, SS_BACKDROP_DIM_MIX);
                        }
                        for (ey = 0; ey < scale; ey++) {
                            int32_t dy2 = dpy + sy * scale + ey;
                            if (dy2 < y0 || dy2 >= y1) {
                                continue;
                            }
                            for (ex = 0; ex < scale; ex++) {
                                int32_t dx2 = dpx + sx * scale + ex;
                                if (dx2 >= x0 && dx2 < x1) {
                                    pixels[(size_t)dy2 * (size_t)stride + dx2] = col;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/* Nine-slice source mapping for one axis: exact corners, tiled middle.
 * snap keeps the middle period on the carved art's own rhythm. */
static int32_t SliceMap(int32_t d, int32_t dstLen, int32_t srcLen, int32_t corner, int32_t snap,
                        int32_t scale) {
    int32_t c = corner;
    int32_t sd, se, span, tspan;
    if (c * 2 * scale > dstLen) {
        c = dstLen / (2 * scale);
        if (c < 1) c = 1;
    }
    sd = d / scale;
    se = (dstLen - 1 - d) / scale;
    if (sd < c) {
        return sd;
    }
    if (se < c) {
        return srcLen - 1 - se;
    }
    span = srcLen - 2 * corner;
    if (span <= 0) {
        return corner;
    }
    tspan = (span / snap) * snap;
    if (tspan <= 0) {
        tspan = span;
    }
    return corner + (sd - c) % tspan;
}

/* Shared sliced blit off one of the composed menu sources. Transparent
 * source pixels are skipped (the slab's rounded outline corners). */
static void DrawSliced(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, int32_t x, int32_t y,
                       int32_t w, int32_t h, int32_t scale, const uint32_t* srcImg, int32_t srcStride,
                       int32_t sx0, int32_t sy0, int32_t sx1, int32_t sy1, int32_t cw, int32_t ch,
                       int32_t snap) {
    int32_t dx, dy;
    if (scale < 1) {
        scale = 1;
    }
    for (dy = 0; dy < h; dy++) {
        int32_t py = y + dy;
        int32_t sy;
        uint32_t* row;
        if (py < 0 || py >= bufH) {
            continue;
        }
        sy = sy0 + SliceMap(dy, h, sy1 - sy0, ch, snap, scale);
        row = pixels + (size_t)py * (size_t)stride;
        for (dx = 0; dx < w; dx++) {
            int32_t px = x + dx;
            int32_t sx;
            uint32_t col;
            if (px < 0 || px >= bufW) {
                continue;
            }
            sx = sx0 + SliceMap(dx, w, sx1 - sx0, cw, snap, scale);
            col = srcImg[(size_t)sy * (size_t)srcStride + sx];
            if (col != 0) {
                row[px] = col;
            }
        }
    }
}

void Port_SecondScreenTheme_DrawPlate(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                      int32_t x, int32_t y, int32_t w, int32_t h, int32_t scale) {
    if (!sMenuOk || !sSlabOk) {
        /* Palette-toned stand-in: stone fill, ink outline. */
        uint32_t ink = sColors[SSC_MENU_INK];
        int32_t t = scale > 0 ? 2 * scale : 2;
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + w, y + h, ink);
        FillRectTheme(pixels, bufW, bufH, stride, x + t, y + t, x + w - t, y + h - t,
                      sColors[SSC_MENU_STONE]);
        return;
    }
    DrawSliced(pixels, bufW, bufH, stride, x, y, w, h, scale, sMenuBg2, MENU_SCREEN_W, sSlabX0, sSlabY0,
               sSlabX1, sSlabY1, SLAB_CORNER_W, SLAB_CORNER_H, 16);
}

void Port_SecondScreenTheme_DrawWell(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t x, int32_t y, int32_t w, int32_t h, int32_t scale) {
    if (!sMenuOk || !sTrayOk) {
        uint32_t rim = sColors[SSC_MENU_INK];
        int32_t t = scale > 0 ? scale : 1;
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + w, y + h, rim);
        FillRectTheme(pixels, bufW, bufH, stride, x + t, y + t, x + w - t, y + h - t,
                      sColors[SSC_MENU_STONE_DARK]);
        return;
    }
    DrawSliced(pixels, bufW, bufH, stride, x, y, w, h, scale, sTrayPx, TRAY_W, 0, 0, TRAY_W, TRAY_H,
               WELL_CORNER, WELL_CORNER, 8);
}

/* Chip drawing shares DrawWindow's tile arrangement, just over a chip
 * tile set. Duplicated loop kept tiny by reusing BlitChromeTile via a
 * temporary swap of the active tile pointer would hurt readability more
 * than this small dedicated blitter. */
static void BlitChipTile(uint32_t* px, int32_t bufW, int32_t bufH, int32_t stride, const uint32_t* tile,
                         int32_t x, int32_t y, int32_t ts, int hflip, int vflip, int32_t cx1) {
    int32_t sx, sy, ex, ey;
    for (sy = 0; sy < 8; sy++) {
        int32_t srcY = vflip ? 7 - sy : sy;
        for (sx = 0; sx < 8; sx++) {
            int32_t srcX = hflip ? 7 - sx : sx;
            uint32_t c = tile[srcY * 8 + srcX];
            if (c == 0) {
                continue;
            }
            for (ey = 0; ey < ts; ey++) {
                int32_t dy = y + sy * ts + ey;
                if (dy < 0 || dy >= bufH) {
                    continue;
                }
                for (ex = 0; ex < ts; ex++) {
                    int32_t dx = x + sx * ts + ex;
                    if (dx >= 0 && dx < bufW && dx < cx1) {
                        px[(size_t)dy * (size_t)stride + dx] = c;
                    }
                }
            }
        }
    }
}

void Port_SecondScreenTheme_DrawChip(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t x, int32_t y, int32_t w, int32_t h, int32_t tileScale,
                                     int style) {
    const uint32_t(*tiles)[64];
    int32_t t, cx, cy, xr, yb;
    if (style < 0 || style >= SS_CHIP_STYLE_COUNT) {
        style = SS_CHIP_DARK;
    }
    if (tileScale < 1) {
        tileScale = 1;
    }
    t = 8 * tileScale;
    if (!sBuilt || !sChipTilesOk[style] || w < 2 * t || h < 2 * t) {
        uint32_t fill = style == SS_CHIP_RED ? sColors[SSC_MENU_RED] : sColors[SSC_MENU_BLACK];
        int32_t o = tileScale;
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + w, y + h, sColors[SSC_MENU_WHITE]);
        FillRectTheme(pixels, bufW, bufH, stride, x + o, y + o, x + w - o, y + h - o, fill);
        return;
    }
    tiles = sChipTiles[style];

    /* Interior under the border ring (same half-tile overlap rule as
     * DrawWindow so border art always meets fill). */
    FillRectTheme(pixels, bufW, bufH, stride, x + t / 2, y + t / 2, x + w - t / 2, y + h - t / 2,
                  tiles[CHROME_FILL][0]);

    xr = x + w - 2 * t;
    yb = y + h - 2 * t;
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_CORNER], x, y, tileScale, 0, 0, bufW);
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_CORNER], x + w - t, y, tileScale, 1, 0, bufW);
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_CORNER], x, y + h - t, tileScale, 0, 1, bufW);
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_CORNER], x + w - t, y + h - t, tileScale, 1, 1,
                 bufW);
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_H_CORNER], x + t, y, tileScale, 0, 0, bufW);
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_H_CORNER], xr, y, tileScale, 1, 0, bufW);
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_H_CORNER], x + t, y + h - t, tileScale, 0, 1, bufW);
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_H_CORNER], xr, y + h - t, tileScale, 1, 1, bufW);
    for (cx = x + 2 * t; cx < xr; cx += t) {
        BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_H_STRAIGHT], cx, y, tileScale, 0, 0, xr);
        BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_H_STRAIGHT], cx, y + h - t, tileScale, 0, 1,
                     xr);
    }
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_V_CORNER], x, y + t, tileScale, 0, 0, bufW);
    BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_V_CORNER], x + w - t, y + t, tileScale, 1, 0, bufW);
    if (yb > y + t) {
        BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_V_CORNER], x, yb, tileScale, 0, 1, bufW);
        BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_V_CORNER], x + w - t, yb, tileScale, 1, 1,
                     bufW);
    }
    for (cy = y + 2 * t; cy < yb; cy += t) {
        BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_V_STRAIGHT], x, cy, tileScale, 0, 0, bufW);
        BlitChipTile(pixels, bufW, bufH, stride, tiles[CHROME_V_STRAIGHT], x + w - t, cy, tileScale, 1, 0,
                     bufW);
    }
}

/* -------------------------------------------------------------------- */
/*  Message-font text                                                    */
/* -------------------------------------------------------------------- */

/* Glyph metrics from the 8x16 cell's row 0 (sub_0805F7A0): leading 0xF
 * nibbles give the start column, the following non-0xF span the width —
 * the exact advance the game's text renderer uses. */
static void GlyphMetrics(const u8* glyph, int32_t* outStart, int32_t* outWidth) {
    u32 row0 = Port_ReadU32(glyph);
    u32 mask = 0xFu;
    int32_t i = 0, j;
    for (; i < 8; i++) {
        if ((row0 & mask) != mask) {
            break;
        }
        mask <<= 4;
    }
    j = i;
    for (; i < 8 && (row0 & mask) != mask; mask <<= 4, i++) {}
    *outStart = j;
    *outWidth = i - j;
}

/* The USA/EU script is plain ASCII in bank 0 (charmap.txt); anything the
 * panel never uses falls back to '?' so a stray string cannot index out
 * of the 256-glyph bank. */
static const u8* GlyphData(char c) {
    u8 code = (u8)c;
    if (code < 0x20) {
        code = '?';
    }
    return sFontGlyphs + (size_t)code * 64u;
}

int32_t Port_SecondScreenTheme_TextWidth(const char* str, int32_t scale) {
    int32_t w = 0, gs, gw;
    if (!sBuilt || !sFontOk || str == NULL) {
        return 0;
    }
    for (; *str; str++) {
        GlyphMetrics(GlyphData(*str), &gs, &gw);
        w += gw;
    }
    return w * (scale < 1 ? 1 : scale);
}

int32_t Port_SecondScreenTheme_DrawText(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                        int32_t x, int32_t y, int32_t scale, int style, const char* str) {
    const u8* lutE;
    const u8* lutO;
    int32_t startX = x;
    if (!sBuilt || !sFontOk || str == NULL) {
        return 0;
    }
    if (style < 0 || style >= SS_TEXT_STYLE_COUNT) {
        style = SS_TEXT_INK;
    }
    if (scale < 1) {
        scale = 1;
    }
    lutE = &sTextLut[style][0];
    lutO = &sTextLut[style][16];

    for (; *str; str++) {
        const u8* glyph = GlyphData(*str);
        int32_t gs, gw, col, row2, ex, ey;
        GlyphMetrics(glyph, &gs, &gw);
        /* Rows 1..15 are ink (row 0 is the metrics row); columns clip to
         * the glyph's own [start, start+width) span like sub_0805F820. */
        for (row2 = 1; row2 < 16; row2++) {
            for (col = gs; col < gs + gw && col < 8; col++) {
                u8 packed = glyph[row2 * 4 + (col >> 1)];
                u8 pix = (col & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
                u32 colorIdx = (col & 1) ? (u32)(lutO[pix] >> 4) : (u32)(lutE[pix] & 0x0Fu);
                uint32_t rgba;
                if (colorIdx == 0) {
                    continue;
                }
                rgba = sMsgPal[colorIdx];
                for (ey = 0; ey < scale; ey++) {
                    int32_t dy = y + row2 * scale + ey;
                    if (dy < 0 || dy >= bufH) {
                        continue;
                    }
                    for (ex = 0; ex < scale; ex++) {
                        int32_t dx = x + (col - gs) * scale + ex;
                        if (dx >= 0 && dx < bufW) {
                            pixels[(size_t)dy * (size_t)stride + dx] = rgba;
                        }
                    }
                }
            }
        }
        x += gw * scale;
    }
    return x - startX;
}

/* -------------------------------------------------------------------- */
/*  Stylized banner font (bank 8)                                        */
/* -------------------------------------------------------------------- */

/* ASCII passthrough like the small font — sub_0805F9A0 maps a non-JP
 * character to bank 8 at its own code. Control chars clamp to '?'; the
 * bank's coverage is A-Z a-z 0-9 - . , : ' ! ? (checked on USA — codes
 * like % / ( ) hold kana there, so panel strings avoid them). */
static const u8* BigGlyphData(char c) {
    u8 code = (u8)c;
    if (code < 0x20) {
        code = '?';
    }
    return sBigFontGlyphs + (size_t)code * 128u;
}

#define BIG_SPACE_ADVANCE 8 /* the tokenizer's fixed word gap (case 0xc) */
#define BIG_GLYPH_ROWS 16 /* stylized cell height; ink spans rows 1..15 */
#define BIG_INK_ROWS 13   /* rows the body/shade roles actually cover */

int32_t Port_SecondScreenTheme_BigTextWidth(const char* str, int32_t scale) {
    int32_t w = 0, gs, gw, adv;
    if (!sBuilt || !sBigFontOk || str == NULL) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    for (; *str; str++) {
        if (*str == ' ') {
            w += BIG_SPACE_ADVANCE;
            continue;
        }
        {
            const u8* g = BigGlyphData(*str);
            GlyphMetrics(g, &gs, &gw);
            adv = gw;
            GlyphMetrics(g + 64, &gs, &gw);
            adv += gw;
            if (adv > 1) {
                adv--; /* stylized glyphs share one outline column */
            }
            w += adv;
        }
    }
    return w * scale;
}

/* Shared body of the stylized draw, taking the value->RGBA table directly so
 * the menu button can letter its labels in the plate's own tones without
 * those becoming a public SS_TEXT_* style. */
static int32_t DrawBigTextPal(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, int32_t x,
                              int32_t y, int32_t scale, const uint32_t* pal, const char* str) {
    int32_t startX = x;
    if (!sBuilt || !sBigFontOk || str == NULL) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }

    for (; *str; str++) {
        const u8* glyph;
        int32_t cell, adv = 0;
        if (*str == ' ') {
            x += BIG_SPACE_ADVANCE * scale;
            continue;
        }
        glyph = BigGlyphData(*str);
        /* Two 8x16 cells drawn back to back at their own metric spans —
         * the exact double sub_0805F820 call of sub_0805F7DC. Zero-value
         * pixels are skipped (sub_080026F2's transparent merge), which is
         * also what lets the shared outline columns overlap cleanly. */
        for (cell = 0; cell < 2; cell++) {
            const u8* cp = glyph + cell * 64;
            int32_t gs, gw, col, row2, ex, ey;
            GlyphMetrics(cp, &gs, &gw);
            for (row2 = 1; row2 < 16; row2++) {
                for (col = gs; col < gs + gw && col < 8; col++) {
                    u8 packed = cp[row2 * 4 + (col >> 1)];
                    u8 pix = (col & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
                    uint32_t rgba = pal[pix];
                    if (pix == 0 || rgba == 0) {
                        continue;
                    }
                    for (ey = 0; ey < scale; ey++) {
                        int32_t dy = y + row2 * scale + ey;
                        if (dy < 0 || dy >= bufH) {
                            continue;
                        }
                        for (ex = 0; ex < scale; ex++) {
                            int32_t dx = x + (adv + col - gs) * scale + ex;
                            if (dx >= 0 && dx < bufW) {
                                pixels[(size_t)dy * (size_t)stride + dx] = rgba;
                            }
                        }
                    }
                }
            }
            adv += gw;
        }
        if (adv > 1) {
            adv--; /* next glyph overlaps this one's outline column */
        }
        x += adv * scale;
    }
    return x - startX;
}

/* -------------------------------------------------------------------- */
/*  Menu button / R glyph / action label                                 */
/* -------------------------------------------------------------------- */

int32_t Port_SecondScreenTheme_DrawBigText(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                           int32_t x, int32_t y, int32_t scale, int style,
                                           const char* str) {
    if (style < 0 || style >= SS_TEXT_STYLE_COUNT) {
        style = SS_TEXT_INK;
    }
    return DrawBigTextPal(pixels, bufW, bufH, stride, x, y, scale, sBigPal[style], str);
}

/* Repeats a source patch of the plate over a destination rect at `scale`.
 * Corner patches land exactly (dw == sw*scale); edge and interior patches
 * are 1 px along the stretched axis and simply repeat, which is what the
 * plate's uniform rim and flat fill want. */
static void PlatePatch(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, int32_t sx,
                       int32_t sy, int32_t sw, int32_t sh, int32_t dx, int32_t dy, int32_t dw,
                       int32_t dh, int32_t scale) {
    int32_t i, j;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
        return;
    }
    for (j = 0; j < dh; j++) {
        int32_t py = dy + j;
        int32_t srcY = sy + (j / scale) % sh;
        if (py < 0 || py >= bufH) {
            continue;
        }
        for (i = 0; i < dw; i++) {
            int32_t px = dx + i;
            uint32_t c;
            if (px < 0 || px >= bufW) {
                continue;
            }
            c = sBtnPlate.px[(size_t)srcY * (size_t)sBtnPlate.w + sx + (i / scale) % sw];
            if ((c >> 24) == 0) {
                continue;
            }
            pixels[(size_t)py * (size_t)stride + px] = c;
        }
    }
}

/* Stamps one quadrant of the selection-bracket composite (its art only
 * occupies the composite's corners) at a destination corner. */
static void BracketCorner(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, int right,
                          int bottom, int32_t dx, int32_t dy, int32_t scale) {
    int32_t cw = sBtnBracket.w / 2, ch = sBtnBracket.h / 2;
    int32_t sx = right ? sBtnBracket.w - cw : 0;
    int32_t sy = bottom ? sBtnBracket.h - ch : 0;
    int32_t i, j, ex, ey;
    for (j = 0; j < ch; j++) {
        for (i = 0; i < cw; i++) {
            uint32_t c = sBtnBracket.px[(size_t)(sy + j) * (size_t)sBtnBracket.w + sx + i];
            if ((c >> 24) == 0) {
                continue;
            }
            for (ey = 0; ey < scale; ey++) {
                int32_t py = dy + j * scale + ey;
                if (py < 0 || py >= bufH) {
                    continue;
                }
                for (ex = 0; ex < scale; ex++) {
                    int32_t px = dx + i * scale + ex;
                    if (px >= 0 && px < bufW) {
                        pixels[(size_t)py * (size_t)stride + px] = c;
                    }
                }
            }
        }
    }
}

int Port_SecondScreenTheme_DrawMenuButton(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                          int32_t x, int32_t y, int32_t w, int32_t h, const char* label,
                                          int pressed) {
    int32_t ps, corner, cleanCol = sBtnCleanCol, cleanRow = sBtnCleanRow;
    int32_t ls, textW = 0, tx, ty;

    if (!sBuilt || !sBtnOk || pixels == NULL || w <= 0 || h <= 0) {
        return 0;
    }

    /* Art scale from the height, so the rim keeps the plate's own
     * proportions however wide the caller stretches it. */
    ps = h / sBtnH;
    if (ps < 1) {
        ps = 1;
    }
    corner = MENU_BUTTON_CORNER;
    while (corner * 2 * ps > w || corner * 2 * ps > h) {
        if (ps > 1) {
            ps--;
        } else {
            corner--;
            if (corner < 1) {
                return 0;
            }
        }
    }

    {
        int32_t c = corner * ps;
        int32_t ix = x + c, iy = y + c, iw = w - 2 * c, ih = h - 2 * c;
        /* interior fill first, then the four edges, then the corners */
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0 + cleanCol, sBtnY0 + cleanRow, 1, 1, ix, iy, iw, ih,
                   ps);
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0 + cleanCol, sBtnY0, 1, corner, ix, y, iw, c, ps);
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0 + cleanCol, sBtnY0 + sBtnH - corner, 1, corner, ix,
                   y + h - c, iw, c, ps);
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0, sBtnY0 + cleanRow, corner, 1, x, iy, c, ih, ps);
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0 + sBtnW - corner, sBtnY0 + cleanRow, corner, 1,
                   x + w - c, iy, c, ih, ps);
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0, sBtnY0, corner, corner, x, y, c, c, ps);
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0 + sBtnW - corner, sBtnY0, corner, corner, x + w - c,
                   y, c, c, ps);
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0, sBtnY0 + sBtnH - corner, corner, corner, x,
                   y + h - c, c, c, ps);
        PlatePatch(pixels, bufW, bufH, stride, sBtnX0 + sBtnW - corner, sBtnY0 + sBtnH - corner, corner,
                   corner, x + w - c, y + h - c, c, c, ps);

        /* Label: the stylized font in the plate's own blue, shrunk until it
         * fits the plate's inner box the way the game's own lettering sits
         * inside SLEEP / SAVE. */
        if (label != NULL && label[0] != '\0' && sBigFontOk) {
            int32_t maxW = iw - 2 * ps, maxH = ih - ps;
            for (ls = (maxH > 0 ? maxH / BIG_INK_ROWS : 0); ls >= 1; ls--) {
                textW = Port_SecondScreenTheme_BigTextWidth(label, ls);
                if (textW > 0 && textW <= maxW) {
                    break;
                }
            }
            if (ls >= 1 && textW > 0) {
                tx = x + (w - textW) / 2;
                /* The body/shade roles cover glyph rows 2..14, so the box
                 * top sits two rows above the visible band's center. */
                ty = y + (h - BIG_INK_ROWS * ls) / 2 - 2 * ls;
                DrawBigTextPal(pixels, bufW, bufH, stride, tx, ty, ls, sBtnPal, label);
            }
        }

        /* Pressed/active: the menu's own selection brackets, the mark the
         * quest screen puts on whichever of SLEEP / SAVE is chosen. */
        if (pressed && sBtnBracket.px != NULL) {
            int32_t bw = (sBtnBracket.w / 2) * ps, bh = (sBtnBracket.h / 2) * ps;
            BracketCorner(pixels, bufW, bufH, stride, 0, 0, x, y, ps);
            BracketCorner(pixels, bufW, bufH, stride, 1, 0, x + w - bw, y, ps);
            BracketCorner(pixels, bufW, bufH, stride, 0, 1, x, y + h - bh, ps);
            BracketCorner(pixels, bufW, bufH, stride, 1, 1, x + w - bw, y + h - bh, ps);
        }
    }
    return 1;
}

int Port_SecondScreenTheme_DrawRButton(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                       int32_t x, int32_t y, int32_t scale2) {
    const SecondScreenThemeSprite* s = Port_SecondScreenTheme_Get(SST_BUTTON_R);
    int32_t dw, dh, dx, dy;
    if (s == NULL || pixels == NULL) {
        return 0;
    }
    if (scale2 < 2) {
        scale2 = 2;
    }
    dw = (s->w * scale2 + 1) / 2;
    dh = (s->h * scale2 + 1) / 2;
    for (dy = 0; dy < dh; dy++) {
        int32_t py = y + dy;
        int32_t sy = (dy * 2) / scale2;
        if (py < 0 || py >= bufH) {
            continue;
        }
        for (dx = 0; dx < dw; dx++) {
            int32_t px = x + dx;
            int32_t sx = (dx * 2) / scale2;
            uint32_t c = s->px[(size_t)sy * (size_t)s->w + sx];
            if ((c >> 24) == 0) {
                continue;
            }
            if (px >= 0 && px < bufW) {
                pixels[(size_t)py * (size_t)stride + px] = c;
            }
        }
    }
    return 1;
}

/* One decoded label, cropped to its ink: the sprite pieces are a fixed-size
 * box the words sit inside, so laying an R glyph and a label out side by
 * side wants the ink extent, not the box. */
#define ACTION_LABEL_MAX_W 96
#define ACTION_LABEL_MAX_H 48
static uint32_t sActionScratch[ACTION_LABEL_MAX_W * ACTION_LABEL_MAX_H];

int32_t Port_SecondScreenTheme_DrawActionLabel(uint32_t* pixels, int32_t bufW, int32_t bufH,
                                               int32_t stride, int32_t x, int32_t y, int32_t scale2,
                                               uint8_t frameId) {
    const SpritePtr* sprite;
    const SpriteFrame* frame;
    const u8* slotTiles;
    const u8* frameData;
    u32 count, i;
    int32_t minX = ACTION_LABEL_MAX_W, minY = ACTION_LABEL_MAX_H, maxX = -1, maxY = -1;
    int32_t sx, sy;

    if (frameId == 0 || pixels == NULL) {
        return 0;
    }
    if (scale2 < 2) {
        scale2 = 2;
    }
    sprite = Port_GetSpritePtr(Port_RemapSpriteIndex(SPRITE_UI_LABELS_RAW));
    if (sprite == NULL || sprite->frames == NULL || sprite->ptr == NULL) {
        return 0;
    }
    frame = &sprite->frames[frameId];
    if (frame->numTiles == 0 || frame->firstTileIndex >= 0x4000) {
        return 0; /* the same emptiness/bounds guards sub_0801CB20 applies */
    }
    slotTiles = (const u8*)sprite->ptr + (size_t)frame->firstTileIndex * 32u;
    frameData = (const u8*)sub_080AD8F0(Port_RemapSpriteIndex(SPRITE_UI_LABELS_RAW), frameId);
    if (frameData == NULL) {
        return 0;
    }
    count = frameData[0];
    if (count == 0 || count > 12) {
        return 0;
    }

    memset(sActionScratch, 0, sizeof(sActionScratch));

    /* Pieces drawn back to front so the first (topmost OAM) wins overlaps;
     * tiles are slot-relative, exactly as sub_0801CB20's DMA leaves them,
     * and every label piece carries its own absolute palette bank (the
     * element's command bank is 0 — gHUD.elements[].unk_18 is never set for
     * the text elements). Offsets are biased into the scratch so the OBJ
     * box, which straddles the command position, lands inside it. */
    for (i = count; i-- > 0;) {
        const u8* p = frameData + 1 + i * 5;
        u32 shape = (p[2] >> 6) & 3, size = (p[2] >> 4) & 3;
        int hflip = (p[2] & 0x04) != 0, vflip = (p[2] & 0x08) != 0;
        u32 tileIdx = (u32)p[3] + (((u32)p[4] & 3u) << 8);
        const uint16_t* pal = ObjPalBank(((u32)p[4] >> 4) & 15u);
        int32_t pw, ph, wTiles, hTiles, tx, ty2, yy, xx;
        if (shape == 3 || pal == NULL) {
            continue;
        }
        pw = kObjW[shape][size];
        ph = kObjH[shape][size];
        wTiles = pw / 8;
        hTiles = ph / 8;
        for (ty2 = 0; ty2 < hTiles; ty2++) {
            for (tx = 0; tx < wTiles; tx++) {
                const u8* tile = slotTiles + ((u32)(tileIdx + ty2 * wTiles + tx)) * 32u;
                for (yy = 0; yy < 8; yy++) {
                    for (xx = 0; xx < 8; xx++) {
                        u8 packed = tile[yy * 4 + xx / 2];
                        u8 idx = (xx & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
                        int32_t dx = tx * 8 + xx, dy = ty2 * 8 + yy;
                        if (idx == 0) {
                            continue;
                        }
                        if (hflip) dx = pw - 1 - dx;
                        if (vflip) dy = ph - 1 - dy;
                        dx += (int32_t)(int8_t)p[0] + ACTION_LABEL_MAX_W / 2;
                        dy += (int32_t)(int8_t)p[1] + ACTION_LABEL_MAX_H / 2;
                        if (dx < 0 || dy < 0 || dx >= ACTION_LABEL_MAX_W || dy >= ACTION_LABEL_MAX_H) {
                            continue;
                        }
                        sActionScratch[dy * ACTION_LABEL_MAX_W + dx] = Rgb555ToRgba8888(pal[idx]);
                        if (dx < minX) minX = dx;
                        if (dx > maxX) maxX = dx;
                        if (dy < minY) minY = dy;
                        if (dy > maxY) maxY = dy;
                    }
                }
            }
        }
    }
    if (maxX < minX || maxY < minY) {
        return 0;
    }

    {
        int32_t sw = maxX - minX + 1;
        int32_t sh = maxY - minY + 1;
        int32_t dw = (sw * scale2 + 1) / 2;
        int32_t dh = (sh * scale2 + 1) / 2;
        int32_t drawX = x - dw / 2;
        for (sy = 0; sy < dh; sy++) {
            int32_t py = y + sy;
            int32_t srcY = minY + (sy * 2) / scale2;
            if (py < 0 || py >= bufH) {
                continue;
            }
            for (sx = 0; sx < dw; sx++) {
                int32_t px = drawX + sx;
                int32_t srcX = minX + (sx * 2) / scale2;
                uint32_t c = sActionScratch[(size_t)srcY * ACTION_LABEL_MAX_W + srcX];
                if ((c >> 24) != 0 && px >= 0 && px < bufW) {
                    pixels[(size_t)py * (size_t)stride + px] = c;
                }
            }
        }
        return dw;
    }
}
