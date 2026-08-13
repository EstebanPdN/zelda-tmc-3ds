#pragma once

#include "global.h"

/* Install the bundled Russian translation over the USA English text slot.
 * Returns TRUE when Russian assets were loaded and installed. */
bool32 Port3DS_LoadRussianLocale(void);

/* True when ptr points at one glyph inside the custom Russian bank. */
bool32 Port3DS_IsRussianFontGlyph(const void* ptr);

/* Treat the RomFS-backed locale buffers as native loaded assets so the
 * generic GBA-address resolver never mistakes a 3DS heap pointer for a raw
 * 0x08xxxxxx cartridge address. */
bool32 Port3DS_IsRussianAssetBytes(const void* ptr, u32 size);
const u8* Port3DS_RussianAssetEnd(const void* ptr);
