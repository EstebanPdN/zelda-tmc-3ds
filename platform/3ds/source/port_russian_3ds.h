#pragma once

#include "global.h"

/* Install the bundled Russian translation over the USA English text slot.
 * Returns TRUE when Russian assets were loaded and installed. */
bool32 Port3DS_LoadRussianLocale(void);

/* True after the bundled Russian locale has been installed. */
bool32 Port3DS_RussianLocaleActive(void);

/* True when ptr points at one glyph inside the custom Russian message bank. */
bool32 Port3DS_IsRussianFontGlyph(const void* ptr);

/* Russian replacement glyphs for the game's stylized bank-8 area-name font.
 * Returns NULL for codes outside the bundled Cyrillic range, leaving the
 * original USA banner font untouched for ASCII/punctuation. */
u32* Port3DS_RussianBannerGlyph(u32 code);
bool32 Port3DS_IsRussianBannerFontGlyph(const void* ptr);

/* Treat the RomFS-backed locale buffers as native loaded assets so the
 * generic GBA-address resolver never mistakes a 3DS heap pointer for a raw
 * 0x08xxxxxx cartridge address. */
bool32 Port3DS_IsRussianAssetBytes(const void* ptr, u32 size);
const u8* Port3DS_RussianAssetEnd(const void* ptr);
