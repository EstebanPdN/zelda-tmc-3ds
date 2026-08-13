#include "port_russian_3ds.h"

#include "main.h"
#include "port_config.h"

#include <stdio.h>
#include <stdlib.h>

#define RUSSIAN_TEXT_PATH "romfs:/ru/russian.bin"
#define RUSSIAN_FONT_PATH "romfs:/ru/russian_font.bin"
#define RUSSIAN_FONT_SIZE (128u * 64u)
#define RUSSIAN_CATEGORY_COUNT 80u

extern u32* gTranslations[];
extern void* gUnk_08109248[];

static u8* sRussianText;
static u32 sRussianTextSize;
static u8* sRussianFont;

static void* LoadWholeFile(const char* path, u32* outSize) {
    FILE* file = fopen(path, "rb");
    long size;
    void* buffer;

    if (outSize) *outSize = 0;
    if (!file) return NULL;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = malloc((size_t)size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    if (outSize) *outSize = (u32)size;
    return buffer;
}

static bool32 IsRange(const void* ptr, u32 size, const u8* begin, u32 length) {
    const uintptr_t at = (uintptr_t)ptr;
    const uintptr_t first = (uintptr_t)begin;
    const uintptr_t last = first + (uintptr_t)length;
    return begin != NULL && at >= first && at <= last && (uintptr_t)size <= last - at;
}

bool32 Port3DS_IsRussianFontGlyph(const void* ptr) {
    return IsRange(ptr, 1, sRussianFont, RUSSIAN_FONT_SIZE);
}

bool32 Port3DS_IsRussianAssetBytes(const void* ptr, u32 size) {
    return IsRange(ptr, size, sRussianText, sRussianTextSize) || IsRange(ptr, size, sRussianFont, RUSSIAN_FONT_SIZE);
}

const u8* Port3DS_RussianAssetEnd(const void* ptr) {
    if (IsRange(ptr, 0, sRussianText, sRussianTextSize)) return sRussianText + sRussianTextSize;
    if (IsRange(ptr, 0, sRussianFont, RUSSIAN_FONT_SIZE)) return sRussianFont + RUSSIAN_FONT_SIZE;
    return NULL;
}

bool32 Port3DS_LoadRussianLocale(void) {
    u32 fontSize = 0;

    if (gRomRegion != ROM_REGION_USA) {
        printf("Russian locale: skipped (USA/BZME ROM required).\n");
        return FALSE;
    }

    if (sRussianText && sRussianFont) {
        gTranslations[LANGUAGE_EN] = (u32*)sRussianText;
        gUnk_08109248[2] = sRussianFont;
        return TRUE;
    }

    sRussianText = (u8*)LoadWholeFile(RUSSIAN_TEXT_PATH, &sRussianTextSize);
    if (!sRussianText) {
        printf("Russian locale: could not load %s\n", RUSSIAN_TEXT_PATH);
        return FALSE;
    }

    /* The first root-table entry is the byte offset to category 0.
     * For this translation it must equal 80 * sizeof(u32). */
    if (sRussianTextSize < RUSSIAN_CATEGORY_COUNT * sizeof(u32) ||
        ((u32*)sRussianText)[0] != RUSSIAN_CATEGORY_COUNT * sizeof(u32)) {
        printf("Russian locale: invalid translation table.\n");
        free(sRussianText);
        sRussianText = NULL;
        sRussianTextSize = 0;
        return FALSE;
    }

    sRussianFont = (u8*)LoadWholeFile(RUSSIAN_FONT_PATH, &fontSize);
    if (!sRussianFont || fontSize != RUSSIAN_FONT_SIZE) {
        printf("Russian locale: invalid font bank (%lu bytes, expected %u).\n",
               (unsigned long)fontSize, (unsigned)RUSSIAN_FONT_SIZE);
        free(sRussianText);
        free(sRussianFont);
        sRussianText = NULL;
        sRussianFont = NULL;
        sRussianTextSize = 0;
        return FALSE;
    }

    /* Keep the engine/save/UI language as English. Raw text bytes 0x80..0xFF
     * are already routed by src/text.c to glyph bank 2 for every non-JP
     * language, so only these two pointers need replacing. */
    gTranslations[LANGUAGE_EN] = (u32*)sRussianText;
    gUnk_08109248[2] = sRussianFont;

    printf("Russian locale: loaded %lu-byte text table and %u-byte font bank.\n",
           (unsigned long)sRussianTextSize, (unsigned)RUSSIAN_FONT_SIZE);
    return TRUE;
}
