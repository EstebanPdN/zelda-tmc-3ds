#ifndef PORT_SECOND_SCREEN_THEME_H
#define PORT_SECOND_SCREEN_THEME_H

/*
 * TMC UI furniture for the second screen, decoded from ROM at runtime —
 * the pause menu's message-window chrome, the HUD's hearts / rupee / key
 * icons and digit fonts, and the pause menu's equip cursor and A/B button
 * bubbles. Zero baked pixels: everything is rebuilt from the same ROM
 * tables the game itself loads (gGfxGroups / gPaletteGroups / the text
 * border data), via the Port_* accessors at the end of src/common.c.
 *
 * Threading/caching contract: everything is built lazily, ONCE, on the
 * first Port_SecondScreenTheme_Ready() call that finds the ROM tables
 * resolved, into private RGBA buffers that are immutable afterwards. Only
 * the second-screen render thread calls into this module, so there is no
 * cross-thread publication to manage — but the build still reads nothing
 * live: ROM-const data only (same policy as port_second_screen_render.c).
 * Per-frame cost after the build is scaled blits of the cached buffers.
 *
 * No Android headers — plain C over caller-provided RGBA8888 buffers, so
 * the file compiles (as dead code) on every platform this port targets.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One cached theme element: RGBA8888, row-major, w*h pixels. Alpha 0 =
 * transparent (GBA color index 0). Pointers stay valid for the process
 * lifetime once Ready() returns 1. */
typedef struct {
    const uint32_t* px;
    int32_t w, h;
} SecondScreenThemeSprite;

enum {
    /* HUD heart tiles (8x8): quarter-heart granularity, exactly the five
     * states DrawHearts (src/ui.c) can put in the tilemap. */
    SST_HEART_EMPTY,
    SST_HEART_Q1,
    SST_HEART_Q2,
    SST_HEART_Q3,
    SST_HEART_FULL,
    /* HUD rupee icon (16x16), one per wallet tier — the HUD swaps the icon
     * with the wallet (gWalletSizes[].iconStartTile). */
    SST_RUPEE_WALLET0,
    SST_RUPEE_WALLET1,
    SST_RUPEE_WALLET2,
    SST_RUPEE_WALLET3,
    /* HUD small-key icon (16x16, tiles 0x1C..0x1F per DrawKeys). */
    SST_KEY,
    /* HUD counter font (8x16): the digits RenderDigits stamps for the
     * rupee/key counters. White = normal, yellow = maxed-out counter. */
    SST_DIGIT_WHITE_0, /* ..._0 + digit, ten consecutive ids */
    SST_DIGIT_YELLOW_0 = SST_DIGIT_WHITE_0 + 10,
    /* HUD ammo-count font (8x8): the tiny bomb/arrow count under the
     * equipped item, tens glyph right-aligned + ones glyph left-aligned
     * (sub_0801C2F0 layout). */
    SST_SMALL_TENS_0 = SST_DIGIT_YELLOW_0 + 10,
    SST_SMALL_ONES_0 = SST_SMALL_TENS_0 + 10,
    /* HUD button bubbles (USA sprite 505 / EU 504, frames 0/1) — the A and B badges. */
    SST_BUTTON_A = SST_SMALL_ONES_0 + 10,
    SST_BUTTON_B,
    /* Pause-menu equip cursor (the gold slot frame), both blink frames. */
    SST_CURSOR_0,
    SST_CURSOR_1,
    /* HUD shoulder-button glyph (USA sprite 505 / EU 504, frame 2) — the R
     * badge the game draws beside its contextual prompts. */
    SST_BUTTON_R,
    SST_COUNT
};

/* Colors sampled/derived from the decoded art (not hardcoded), for the
 * procedural parts of the panel that need to sit in the same palette.
 * Valid (non-fallback) only once Ready() returns 1. */
enum {
    SSC_WINDOW_FILL,   /* message-window interior fill */
    SSC_BORDER_LIGHT,  /* brightest border color */
    SSC_BORDER_DARK,   /* darkest border color */
    SSC_GOLD,          /* key-icon gold (cursor/accent gold) */
    SSC_HEART_RED,     /* full-heart red */
    SSC_RUPEE_GREEN,   /* wallet-0 rupee green */
    SSC_TEXT_LIGHT,    /* HUD digit body color */
    /* Pause-menu screen palette (the light theme): sampled from the
     * composed item-screen layers and the message palette. */
    SSC_MENU_CREAM,      /* backdrop parchment (BG3 flat color) */
    SSC_MENU_STONE,      /* slab plate stone */
    SSC_MENU_STONE_DARK, /* recessed well interior */
    SSC_MENU_INK,        /* menu ink (the dark text body color) */
    SSC_MENU_BLACK,      /* message palette black (outlines) */
    SSC_MENU_WHITE,      /* message palette white */
    SSC_MENU_RED,        /* menu red accent (red text body) */
    SSC_BANNER_NAVY,     /* the banner font's dark outline blue */
    SSC_COUNT
};

/* Text styles for the ROM message font — each is a (fill_type, charColor)
 * pair of the game's own text color tables. */
enum {
    SS_TEXT_INK = 0, /* dark ink on light plates (fill 7 / color 0) */
    SS_TEXT_WHITE,   /* white with silver shading, for dark chips (5 / 0) */
    SS_TEXT_RED,     /* menu red accent (5 / 1) */
    SS_TEXT_GREEN,   /* menu green (5 / 2) */
    SS_TEXT_NAVY,    /* banner-navy body — big font only (small font: ink) */
    SS_TEXT_STYLE_COUNT
};

/* Chip styles: the rounded message chips of border_type 9, in the game's
 * own fill schemes (DispMessageFrame family). */
enum {
    SS_CHIP_DARK = 0, /* black interior — the pause menu's name chips */
    SS_CHIP_RED,      /* brick-red interior — the header/selected chip */
    SS_CHIP_STYLE_COUNT
};

/* Attempts the one-time lazy build (cheap no-op once decided) and returns
 * 1 when the theme is available. Call only while the game is in gameplay
 * (snapshot.inGame) so the ROM tables are guaranteed resolved; before
 * that it returns 0 and the caller uses its neutral fallbacks. */
int Port_SecondScreenTheme_Ready(void);

/* Cached element by SST_* id, or NULL when that element failed to decode
 * (callers fall back per element). */
const SecondScreenThemeSprite* Port_SecondScreenTheme_Get(int id);

/* RGBA color by SSC_* id. Always returns a usable color: a neutral
 * stand-in before Ready(), the palette-derived value after. */
uint32_t Port_SecondScreenTheme_Color(int id);

/* Raw RGB555 colors (16 entries) of one OBJ palette bank as the pause
 * menu leaves it loaded — the bank state sprite OBJ pieces select with
 * their palette bits (group 182's banks 5..10 over 181/11's banks 0..4).
 * Shared with the item-icon renderer so both piece decoders resolve
 * banks identically. Independent of Ready(): NULL only while the ROM
 * palette tables are still unresolved. */
const uint16_t* Port_SecondScreenTheme_ObjPalette(uint32_t bank);

/* Draws a TMC message-style window (DispMessageFrame's exact tile
 * arrangement: corner/edge tiles around a solid fill) covering the given
 * rect, border tiles scaled by tileScale (integer, nearest-neighbor).
 * Falls back to a plain palette-toned frame when the border tiles didn't
 * decode. The rect is clipped to the buffer. */
void Port_SecondScreenTheme_DrawWindow(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                       int32_t x, int32_t y, int32_t w, int32_t h, int32_t tileScale);

/* ------------------------------------------------------------------ *
 * Pause-menu dressing, decoded from the START menu's own screens      *
 * (item screen: gfx groups 86 + 90, palette groups 11/12/181/182 —    *
 * the exact recipe sub_080A4D34 + sub_080A4DB8 run). All built in the *
 * same lazy Ready() pass; every call degrades to palette-toned        *
 * procedural stand-ins until then.                                    *
 * ------------------------------------------------------------------ */

/* 1 once the pause-menu layers decoded (implies Ready()). The light
 * theme's fully-authentic path; callers may branch to simpler dressing
 * while 0 (the colors above still return usable neutral stand-ins). */
int Port_SecondScreenTheme_MenuReady(void);

/* Panel backdrop styles — what the SETTINGS tab's PANEL BACKDROP row
 * cycles. PARCHMENT is the pause menu's own dressing and stays the
 * default; the other two exist because that dressing is busy and bright
 * behind a map and a vitals column. CREAM keeps the parchment tone but
 * drops the Ezlo doodle lattice (the busy part); DARK goes to the panel's
 * idle near-black — the reading-first look the sibling zelda3 port's
 * bottom screen uses, where the backdrop is ~all black and every piece of
 * chrome reads as light-on-dark. */
/* The four after DARK are the middle ground: dimmer than the menu's own
 * dressing without going to near-black. DIM is the parchment itself —
 * lattice and all — blended toward the menu ink, so it stays the same
 * dressing at lower brightness. STONE, SLATE and NAVY are flat fills
 * lifted from colors the chrome is already carved from (the slab plate,
 * the recessed well interior, and the banner font's outline blue), so a
 * plate sitting on one of them reads as the same material rather than as
 * art pasted onto a field. New styles APPEND: the value is what lands in
 * config.json, so renumbering would silently repaint existing configs. */
enum {
    SS_BACKDROP_PARCHMENT = 0,
    SS_BACKDROP_CREAM,
    SS_BACKDROP_DARK,
    SS_BACKDROP_DIM,
    SS_BACKDROP_STONE,
    SS_BACKDROP_SLATE,
    SS_BACKDROP_NAVY,
    SS_BACKDROP_COUNT
};

/* How far DIM pulls the parchment toward the menu ink, 0..255. Picked to
 * knock the glare off the cream while the doodle lattice stays legible as
 * texture — past ~150 the pattern muddies into the fill. */
#define SS_BACKDROP_DIM_MIX 115u

/* The DARK style's flat color as RGBA8888 (A<<24 | B<<16 | G<<8 | R) —
 * deliberately the cinema screen's near-black, so the panel's two dark
 * states are the same black rather than two nearly-equal ones.
 * port_second_screen.c's COL_IDLE_BG is defined from this. */
#define SS_BACKDROP_DARK_RGBA 0xFF050605u

/* Picks the style DrawBackdrop paints and the queries below answer for.
 * Out-of-range values clamp to PARCHMENT (config.json is hand-editable).
 * Written once per frame by the second-screen render thread before it
 * paints — the same single-caller contract as the rest of this module. */
void Port_SecondScreenTheme_SetBackdropStyle(int style);

/* The flat color under the current style (the doodles, when drawn, sit on
 * top of it) and whether that color is dark enough that light-on-dark ink
 * is the readable choice. Anything painted DIRECTLY on the backdrop keys
 * its ink/blend colors off these: the menu's own ink and cream are picked
 * to read on parchment and disappear on near-black.
 *
 * IsDark is a luminance test, not a style comparison — the mid-tones
 * (DIM/STONE/SLATE/NAVY) have to fall on one side or the other of the
 * same threshold, and a per-style flag would leave them answering for a
 * brightness they don't have. */
uint32_t Port_SecondScreenTheme_BackdropColor(void);
int Port_SecondScreenTheme_BackdropIsDark(void);

/* Fills rect with the panel backdrop in the selected style: the pause
 * menu's parchment (flat cream plus the Ezlo doodle pattern on its
 * original diagonal lattice, anchored to the surface origin so panels
 * never shift phase), that cream without the lattice, or the flat
 * near-black. scale is the integer art scale. */
void Port_SecondScreenTheme_DrawBackdrop(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t scale);

/* Draws the menu's carved stone slab (the item screen's big plate) over
 * the rect: nine-sliced from the composed screen so the triforce corners
 * and carved bands stay pixel-authentic; interiors tile. */
void Port_SecondScreenTheme_DrawPlate(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                      int32_t x, int32_t y, int32_t w, int32_t h, int32_t scale);

/* Draws one recessed stone well (the item screen's slot/tray art),
 * nine-sliced from the bottle tray. Used for cells and list rows. */
void Port_SecondScreenTheme_DrawWell(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t x, int32_t y, int32_t w, int32_t h, int32_t scale);

/* Draws a rounded message chip (border_type 9) in the given SS_CHIP_*
 * fill scheme — the pause menu's name/header chips. */
void Port_SecondScreenTheme_DrawChip(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t x, int32_t y, int32_t w, int32_t h, int32_t tileScale,
                                     int style);

/* Text in the game's message font (gUnk_08109248 bank 0, per-glyph widths
 * from the metrics rows), colored through the game's own text LUTs
 * (SS_TEXT_*). Returns the advance in pixels, or 0 when the font is not
 * decoded yet (callers keep a procedural fallback). y is the glyph-box
 * top; visible ink spans rows 1..15 of the 16 px box. */
int32_t Port_SecondScreenTheme_DrawText(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                        int32_t x, int32_t y, int32_t scale, int style, const char* str);

/* Pixel width DrawText would advance (0 when the font is not ready). */
int32_t Port_SecondScreenTheme_TextWidth(const char* str, int32_t scale);

/* Text in the game's STYLIZED banner font — the fat white-on-navy
 * lettering of the area-name banners ("South Hyrule Field"): glyph bank 8
 * of gUnk_08109248, two 8x16 cells per glyph, replayed exactly like
 * ShowTextBox's stylized path (sub_0805F9A0 -> sub_0805F25C banks>4 ->
 * sub_080026F2's transparent-merge column writer, adjacent glyphs
 * overlapping one outline column). Same SS_TEXT_* color schemes; the
 * banners' own scheme is SS_TEXT_WHITE-on-chip / SS_TEXT_INK on plates.
 * y is the glyph-box top (16 rows at `scale`); returns the advance, or 0
 * when the bank isn't decoded (callers keep their fallback). */
int32_t Port_SecondScreenTheme_DrawBigText(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                           int32_t x, int32_t y, int32_t scale, int style, const char* str);

/* Pixel width DrawBigText would advance (0 when the bank is not ready). */
int32_t Port_SecondScreenTheme_BigTextWidth(const char* str, int32_t scale);

/* The pause menu's own labelled button — the pale plate with the blue-grey
 * keyline and blue lettering the SLEEP / SAVE buttons use. Draws the frame
 * fitted to the rect and the label centered in the game's button font.
 * `pressed` draws the button's own active/held state. Returns 1 when the
 * authentic art was used, 0 while it isn't decoded (caller falls back). */
int Port_SecondScreenTheme_DrawMenuButton(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                          int32_t x, int32_t y, int32_t w, int32_t h, const char* label,
                                          int pressed);

/* The HUD's R button glyph, nearest-neighbor scaled in half-pixel units
 * (3 means 1.5x). Returns 1 when drawn, 0 while the art isn't decoded. */
int Port_SecondScreenTheme_DrawRButton(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                       int32_t x, int32_t y, int32_t scale2);

/* One of the HUD's contextual button-prompt labels (SPEAK / READ / LIFT /
 * ...), by the sprite frame id the snapshot carries in rActionFrame —
 * the game's own label art, not re-lettered text. x is the horizontal center.
 * Returns the drawn width in pixels, or 0 when the frame isn't decodable /
 * the id is 0. */
int32_t Port_SecondScreenTheme_DrawActionLabel(uint32_t* pixels, int32_t bufW, int32_t bufH,
                                               int32_t stride, int32_t x, int32_t y, int32_t scale2,
                                               uint8_t frameId);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_THEME_H */
