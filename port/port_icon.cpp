#include "port_icon.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>

/* Forward-declared C structs — we can't transitively include the project
 * headers because the C decomp uses `this` as a parameter name, which
 * C++ rejects as a reserved word. */
extern "C" {

/* gFigurines table (port/port_figurines.c). Index 5 is Ezlo (Hat) — the
 * figurine-pose sprite the user wanted: green Minish cap on a stand with
 * a yellow beak. 40 tiles = 8 wide × 5 tall × 32 bytes/tile = 1280 (0x500). */
struct Figurine {
    const uint8_t* pal;
    const uint8_t* gfx;
    int size;
    int zero;
};

extern Figurine gFigurines[];
extern uint8_t* gRomData;

} // extern "C"

namespace {

constexpr int kFigurineEzloHat = 4;  /* gFigurines[4] = Ezlo (Hat) figurine (USA) */
constexpr int kFigurineTilesW = 4;   /* 4 tiles wide × ceil(numTiles/4) tall */
constexpr int kFigurineSkipRows = 6; /* skip frame chrome — Ezlo character begins around row 6 */

struct Rgba {
    uint8_t r, g, b, a;
};

constexpr Rgba kBgTransparent{ 0, 0, 0, 0 };

/* ------------------------------------------------------------------- */
/* Placeholder green-hat silhouette                                    */
/* ------------------------------------------------------------------- */

constexpr int kPlaceholderW = 32;
constexpr int kPlaceholderH = 32;

constexpr Rgba kHatGreen{ 66, 148, 64, 255 };
constexpr Rgba kHatGreenDark{ 38, 98, 40, 255 };
constexpr Rgba kBeakYellow{ 248, 200, 64, 255 };
constexpr Rgba kEyeBlack{ 16, 16, 16, 255 };
constexpr Rgba kCheekPink{ 248, 152, 152, 255 };

const char* kPlaceholderBitmap[kPlaceholderH] = {
    "................................", "................................", "..............DD................",
    ".............DGGD...............", "............DGGGGD..............", "...........DGGGGGGD.............",
    "..........DGGGGGGGGD............", ".........DGGGGGGGGGGD...........", "........DGGGGGGGGGGGGD..........",
    ".......DGGGGGGGGGGGGGGD.........", "......DGGGGGGGGGGGGGGGGD........", "......DGGGGGGGGGGGGGGGGD........",
    ".....DGGGGGGGGGGGGGGGGGGD.......", ".....DGGGGGGGGGGGGGGGGGGD.......", "....DGGGGGGGGGGGGGGGGGGGGD......",
    "....DGGGGGGGGGGGGGGGGGGGGD......", "....DGGGKGGGGGGGGGGGKGGGGD......", "....DGGGGGGGGGGGGGGGGGGGGD......",
    "....DGGGGGGGYYYYGGGGGGGGGD......", "....DGGGGGGYYYYYYGGGGGGGGD......", "....DGGGGGGGYYYYGGGGGGGGGD......",
    "....DGGPGGGGGGGGGGGGGGPGGD......", ".....DGGGGGGGGGGGGGGGGGGD.......", "......DGGGGGGGGGGGGGGGGD........",
    ".......DDGGGGGGGGGGGGDD.........", "........DDDDGGGGGGDDDD..........", "...........DDDDDDDD.............",
    "................................", "................................", "................................",
    "................................", "................................",
};

const Rgba& PlaceholderColor(char ch) {
    switch (ch) {
        case 'G':
            return kHatGreen;
        case 'D':
            return kHatGreenDark;
        case 'Y':
            return kBeakYellow;
        case 'K':
            return kEyeBlack;
        case 'P':
            return kCheekPink;
        default:
            return kBgTransparent;
    }
}

SDL_Surface* MakePlaceholderIcon() {
    SDL_Surface* surf = SDL_CreateSurface(kPlaceholderW, kPlaceholderH, SDL_PIXELFORMAT_RGBA32);
    if (!surf)
        return nullptr;
    uint8_t* pixels = static_cast<uint8_t*>(surf->pixels);
    const int pitch = surf->pitch;
    for (int y = 0; y < kPlaceholderH; ++y) {
        Rgba* row = reinterpret_cast<Rgba*>(pixels + y * pitch);
        for (int x = 0; x < kPlaceholderW; ++x) {
            row[x] = PlaceholderColor(kPlaceholderBitmap[y][x]);
        }
    }
    return surf;
}

/* ------------------------------------------------------------------- */
/* Real ROM-extraction path                                            */
/* ------------------------------------------------------------------- */

/* Approximation of Ezlo's cap palette (alpha + 15 colors). Idx 0 is the
 * sprite's transparent index. Order chosen by eye to match the in-game
 * green/yellow Ezlo-cap silhouette; the real palette can be swapped in
 * later via gPaletteGroups[][] resolution. */
constexpr Rgba kEzloApproxPalette[16] = {
    { 0, 0, 0, 0 },         /* 0: transparent */
    { 248, 248, 248, 255 }, /* 1: white highlight */
    { 16, 24, 16, 255 },    /* 2: black outline */
    { 38, 98, 40, 255 },    /* 3: dark green */
    { 66, 148, 64, 255 },   /* 4: mid green */
    { 120, 192, 96, 255 },  /* 5: light green */
    { 184, 224, 144, 255 }, /* 6: very light green */
    { 248, 224, 64, 255 },  /* 7: beak yellow */
    { 200, 152, 32, 255 },  /* 8: beak yellow shadow */
    { 248, 200, 160, 255 }, /* 9: skin highlight */
    { 200, 144, 104, 255 }, /*10: skin mid */
    { 136, 88, 64, 255 },   /*11: skin shadow */
    { 248, 152, 152, 255 }, /*12: cheek pink */
    { 248, 248, 200, 255 }, /*13: eye white */
    { 32, 48, 96, 255 },    /*14: eye blue */
    { 128, 128, 128, 255 }, /*15: fill */
};

/* Decode one 8x8 4bpp tile (32 bytes) into RGBA at (px, py) on the surface. */
void DecodeTile(const uint8_t* tile, SDL_Surface* surf, int px, int py, const Rgba* pal) {
    uint8_t* pixels = static_cast<uint8_t*>(surf->pixels);
    const int pitch = surf->pitch;
    for (int y = 0; y < 8; ++y) {
        Rgba* row = reinterpret_cast<Rgba*>(pixels + (py + y) * pitch);
        for (int xByte = 0; xByte < 4; ++xByte) {
            uint8_t b = tile[y * 4 + xByte];
            uint8_t lo = b & 0x0f;
            uint8_t hi = (b >> 4) & 0x0f;
            /* Palette index 0 = transparent for OBJ sprites */
            if (lo)
                row[px + xByte * 2 + 0] = pal[lo];
            if (hi)
                row[px + xByte * 2 + 1] = pal[hi];
        }
    }
}

/* Decode the 16-color GBA palette pointed to by `pal` (32 bytes RGB555 LE). */
void DecodePaletteGba(const uint8_t* pal, Rgba out[16]) {
    out[0] = { 0, 0, 0, 0 }; /* OBJ palette index 0 is always transparent */
    for (int i = 1; i < 16; ++i) {
        uint16_t c = pal[i * 2] | (uint16_t(pal[i * 2 + 1]) << 8);
        uint8_t r = uint8_t(((c >> 0) & 0x1f) * 255 / 31);
        uint8_t g = uint8_t(((c >> 5) & 0x1f) * 255 / 31);
        uint8_t b = uint8_t(((c >> 10) & 0x1f) * 255 / 31);
        out[i] = { r, g, b, 255 };
    }
}

} // namespace

/*
 * Decode gFigurines[5] (Ezlo Hat) from the user's gRomData.
 *
 * Each figurine is N * 32 bytes of raw 4bpp OBJ tile data plus a 32-byte
 * GBA-format palette. The figurine viewer arranges these as a 64x40
 * sprite (8 tiles wide x 5 tall = 40 tiles = 0x500 bytes). No piece-list
 * indirection — the data IS the bitmap, just in 8x8-tile column-major
 * order as the GBA's OAM-1D mapping expects.
 */
extern "C" SDL_Surface* Port_ExtractEzloFromRom(void) {
    if (!gRomData)
        return nullptr;

    int figIdx = kFigurineEzloHat;
    const Figurine& fig = gFigurines[figIdx];
    if (!fig.pal || !fig.gfx || fig.size <= 0)
        return nullptr;

    /* size = N tiles × 32 bytes. Use that to size the surface as a
     * square-ish grid. */
    int numTiles = fig.size / 32;
    int tilesW = kFigurineTilesW;
    if (tilesW <= 0)
        tilesW = 4;
    int totalH = numTiles / tilesW;
    if (totalH <= 0)
        return nullptr;

    int skipRows = kFigurineSkipRows;
    if (skipRows < 0)
        skipRows = 0;
    if (skipRows >= totalH)
        skipRows = 0; /* skip discards everything — fall back to full */
    int tilesH = totalH - skipRows;
    if (tilesH <= 0)
        return nullptr;

    Rgba palette[16];
    DecodePaletteGba(fig.pal, palette);

    const int W = tilesW * 8;
    const int H = tilesH * 8;
    SDL_Surface* surf = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGBA32);
    if (!surf)
        return nullptr;
    SDL_memset(surf->pixels, 0, static_cast<size_t>(surf->pitch) * H);

    const uint8_t* gfx = fig.gfx + skipRows * tilesW * 32;
    for (int ty = 0; ty < tilesH; ++ty) {
        for (int tx = 0; tx < tilesW; ++tx) {
            const uint8_t* tile = gfx + (ty * tilesW + tx) * 32;
            DecodeTile(tile, surf, tx * 8, ty * 8, palette);
        }
    }
    return surf;
}

extern "C" SDL_Surface* Port_CreateAppIcon(void) {
    if (SDL_Surface* rom_icon = Port_ExtractEzloFromRom()) {
        SDL_Log("[icon] using ROM-extracted Ezlo (Hat) figurine (%dx%d)", rom_icon->w, rom_icon->h);
        if (const char* dump = SDL_getenv("TMC_DUMP_ICON")) {
            SDL_SaveBMP(rom_icon, dump);
            SDL_Log("[icon] wrote %s for inspection", dump);
        }
        return rom_icon;
    }
    SDL_Log("[icon] using procedural placeholder (ROM/sprite tables not ready)");
    return MakePlaceholderIcon();
}
