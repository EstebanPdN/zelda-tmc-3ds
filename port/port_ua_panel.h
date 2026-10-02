/**
 * @file port_ua_panel.h
 * @brief Ukrainian text for the port's own panel UI (tloz-tmc-ua).
 *
 * The second screen letters its labels with the game's own fonts read from
 * the ROM (message font, bank 0/2; stylized banner font, bank 8). In the
 * Ukrainian ROM those banks hold Cyrillic glyphs at the Latin codes, so the
 * port's English ASCII labels came out as gibberish ("БАВИ" for "BACK").
 *
 * Port_UA_PanelText() is called at the four string entry points of
 * port_second_screen_theme.c. For the Ukrainian ROM it:
 *   1. looks the English string up in kPortUaStrings (whole string), else
 *      translates it word by word through kPortUaWords (for composed strings
 *      such as "PAGE 2 OF 5" or "CHANNEL: STABLE"), and
 *   2. encodes the UTF-8 result into the byte codes of the Ukrainian font
 *      (the tloz-tmc-ua charmap; see tmc/tools/src/tmc_strings/main.cpp).
 * Latin letters that are left over (version strings, changelog text from
 * GitHub, ...) are rendered with the Latin glyphs the Ukrainian font still
 * has (message font) or transliterated (banner font, which has none).
 * For any other ROM the string is returned untouched.
 *
 * Translations are a first draft — edit kPortUaStrings / kPortUaWords freely.
 * Keep the Ukrainian in the same case as the English (the banner font is
 * used upper-case). The Ukrainian banner font is wider than the English one:
 * buttons and value chips adapt, but settings rows (label + value), the
 * developer rows and the dialog lines have a fixed scale, so keep those no
 * wider than the English text (ua/measure_panel.py reports overflows).
 * The banner font has no Latin glyphs (Latin codes hold Cyrillic), so Latin
 * words such as "FPS" cannot be shown in it — translate them instead.
 */
#ifndef PORT_UA_PANEL_H
#define PORT_UA_PANEL_H

#include <stddef.h>
#include <string.h>

#include "port_ua.h"

typedef struct {
    const char* en;
    const char* ua; /* UTF-8 */
} PortUaPanelEntry;

/* Whole strings, exactly as the port passes them (see port/port_second_screen.c,
 * platform/3ds/source/update_ui_3ds.inc, platform/3ds/source/updater.c). */
static const PortUaPanelEntry kPortUaStrings[] = {
    /* tabs, headers, pages */
    { "ITEMS", "ПРЕДМЕТИ" },
    { "MAP", "МАПА" },
    { "QUEST", "СТАТИСТИКА" }, /* the game's pause menu calls this screen СТАТИСТИКА */
    { "SETTINGS", "НАЛАШТУВАННЯ" },
    { "SCREEN", "ЕКРАН" },
    { "GAMEPLAY", "ГРА" },
    { "DEVELOPER", "ІНСТРУМЕНТИ" },
    { "OVERLAY", "НАКЛАДКА" },
    { "RANDOMIZER", "РАНДОМАЙЗЕР" },
    { "UPDATE", "ОНОВЛЕННЯ" },
    { "ZOOM", "МАСШТАБ" },
    /* buttons */
    { "BACK", "НАЗАД" },
    { "CANCEL", "СКАСУВАТИ" },
    { "CONTINUE", "ПРОДОВЖИТИ" },
    { "DONE", "ГОТОВО" },
    { "RESTART", "РЕСТАРТ" },
    { "LOAD", "ВІДНОВИТИ" },
    { "LOAD STATE", "ВІДНОВИТИ ГРУ" },
    { "WRITE", "ЗБЕРЕГТИ" },
    { "MEM DUMP", "ЗБЕРЕГТИ ГРУ" },
    { "ENABLE RANDOMIZER", "УВІМКНУТИ РАНДОМАЙЗЕР" },
    { "DISABLE RANDOMIZER", "ВИМКНУТИ РАНДОМАЙЗЕР" },
    { "NEXT", "ДАЛІ" },
    { "PREV", "НАЗАД" },
    /* settings labels */
    { "TOP HUD", "ПАНЕЛЬ ЗВЕРХУ" },
    { "WIDESCREEN", "ШИРОКИЙ ЕКРАН" },
    { "FOLLOW CAM", "КАМЕРА СТЕЖИТЬ" },
    { "WINDCREST PINS", "МІТКИ ГЕРБІВ ВІТРУ" },
    { "FLOOR AUTO RETURN", "ПОВЕРНЕННЯ ПОВЕРХУ" },
    { "TURBO SPEED", "ШВИДКІСТЬ ТУРБО" },
    { "MASTER VOLUME", "ГУЧНІСТЬ" },
    { "AUTOSAVE", "АВТОЗБЕРЕЖЕННЯ" },
    { "COLOR CORRECTION", "КОРЕКЦІЯ КОЛЬОРУ" },
    { "SHOW FPS", "ЛІЧИЛЬНИК КАДРІВ" },
    { "HOLD TO ADVANCE TEXT", "ТЕКСТ УТРИМАННЯМ" },
    { "PANEL BACKDROP", "ТЛО ПАНЕЛІ" },
    { "SWAP SCREENS", "ПОМІНЯТИ ЕКРАНИ" },
    { "ASPECT RATIO", "СПІВВІДНОШЕННЯ" },
    { "DISPLAY STYLE", "ФІЛЬТР" },
    /* settings values */
    { "ON", "УВІМК" },
    { "OFF", "ВИМК" },
    { "SHOW", "ПОКАЗАТИ" },
    { "HIDE", "СХОВАТИ" },
    { "PATTERN", "ВІЗЕРУНОК" },
    { "CREAM", "КРЕМОВЕ" },
    { "DARK", "ТЕМНЕ" },
    { "DIM", "ТЬМЯНЕ" },
    { "STONE", "КАМІНЬ" },
    { "SLATE", "СЛАНЕЦЬ" },
    { "NAVY", "СИНЄ" },
    { "WIDE", "ШИРОКИЙ" },
    { "ORIGINAL", "ОРИГІНАЛ" },
    { "STRETCH", "РОЗТЯГ" },
    { "NATIVE", "РІДНИЙ" },
    { "BLUR", "РОЗМИТТЯ" },
    { "BILINEAR", "БІЛІНІЙНИЙ" },
    { "PIXEL PERFECT", "ПІКСЕЛЬНИЙ" },
    /* diagnostics */
    { "VERSION", "ВЕРСІЯ" },
    { "MODEL", "МОДЕЛЬ" },
    { "FPS NOW", "КАДРИ ЗАРАЗ" },
    { "FPS AVG", "КАДРИ СЕРЕД." },
    { "CORE1", "ЯДРО1" },
    { "AREA", "ОБЛАСТЬ" },
    { "ROOM", "КІМНАТА" },
    { "NEW 3DS", "НОВА 3DS" },
    { "OLD 3DS", "СТАРА 3DS" },
    /* multi-line messages (one entry per line, in the order the port draws them) */
    { "LOAD LATEST DUMP?", "ВІДНОВИТИ ГРУ?" },
    { "THE LATEST DUMP IN THE", "ПОТОЧНИЙ СТАН ГРИ" },
    { "DUMPS FOLDER WILL REPLACE", "ЗАМІНИТЬСЯ ОСТАННІМ" },
    { "THE CURRENT GAME STATE.", "ЗБЕРЕЖЕННЯМ." },
    { "UNSAVED PROGRESS MAY BE LOST.", "НЕЗБЕРЕЖЕНЕ БУДЕ ВТРАЧЕНО." },
    { "UNSAVED PROGRESS IS LOST", "НЕЗБЕРЕЖЕНЕ ВТРАЧЕНО" },
    { "RANDOMIZER REQUIRES A NEW GAME.", "ПОТРІБНА НОВА ГРА." },
    { "THE ACTIVE PROFILE SAVE,", "ЗБЕРЕЖЕННЯ ПРОФІЛЮ," },
    { "AUTOSAVES, SAVESTATES, AND", "АВТОЗБЕРЕЖЕННЯ, СТАНИ ТА" },
    { "RANDOMIZER DATA WILL BE", "ДАНІ РАНДОМАЙЗЕРА БУДЕ" },
    { "DELETED. THE ROM IS KEPT.", "ВИДАЛЕНО. ROM ЗАЛИШИТЬСЯ." },
    { "THE GAME WILL RESTART.", "ГРА ПЕРЕЗАПУСТИТЬСЯ." },
    /* developer-row results (port/port_dump_state.c Port_DumpState_ResultLabel) */
    { "LOADED", "ВІДНОВЛЕНО" },
    { "LEGACY", "СТАРИЙ" },
    { "NO DUMP", "НЕМАЄ" },
    { "NO STATE", "НЕМАЄ" },
    { "INVALID", "ПОМИЛКА" },
    { "WRONG ROM", "ІНШИЙ ROM" },
    { "I O ERROR", "ПОМИЛКА" },
    { "ERROR", "ПОМИЛКА" },
    /* R-button prompt stand-ins (kRActionWords) */
    { "DROP", "КИНУТИ" },
    { "THROW", "ЖБУРНУТИ" },
    { "READ", "ЧИТАТИ" },
    { "CHECK", "ОГЛЯНУТИ" },
    { "OPEN", "ВІДКРИТИ" },
    { "SPEAK", "ГОВОРИТИ" },
    { "GRAB", "СХОПИТИ" },
    { "LIFT", "ПІДНЯТИ" },
    { "GROW", "ВИРОСТИ" },
    { "SHRINK", "ЗМЕНШИТИСЬ" },
    { "ROLL", "ПЕРЕКИД" },
    /* dungeon plaques (names as in the game translation) */
    { "Deepwood Shrine", "Святилище Лісу" },
    { "Cave of Flames", "Печера Полум'я" },
    { "Fortress of Winds", "Фортеця Вітрів" },
    { "Temple of Droplets", "Храм Крапель" },
    { "Palace of Winds", "Палац Вітрів" },
    { "Dark Hyrule Castle", "Темний Замок Гайрул" },
    /* updater (update_ui_3ds.inc / updater.c; the UI upper-cases these) */
    { "NO RELEASE SELECTED", "РЕЛІЗ НЕ ОБРАНО" },
    { "DOWNLOAD AND INSTALL", "ЗАВАНТАЖИТИ Й ВСТАНОВИТИ" },
    { "CHECK FOR UPDATE", "ПЕРЕВІРИТИ ОНОВЛЕННЯ" },
    { "DOWNLOAD UPDATE", "ЗАВАНТАЖИТИ ОНОВЛЕННЯ" },
    { "INSTALL UPDATE", "ВСТАНОВИТИ ОНОВЛЕННЯ" },
    { "PLEASE WAIT", "ЗАЧЕКАЙТЕ" },
    { "TAP RELEASE FOR CHANGELOG", "ТОРКНІТЬСЯ РЕЛІЗУ ДЛЯ ЗМІН" },
    { "CHANGELOG ON TOP SCREEN", "ЗМІНИ НА ВЕРХНЬОМУ ЕКРАНІ" },
    { "UPDATE INSTALLED", "ОНОВЛЕННЯ ВСТАНОВЛЕНО" },
    { "3DSX LAUNCH PATH UNKNOWN", "ШЛЯХ ЗАПУСКУ 3DSX НЕВІДОМИЙ" },
    { "CANNOT SAVE UPDATE CHANNEL", "НЕ ВДАЛОСЯ ЗБЕРЕГТИ КАНАЛ" },
    { "CANNOT START UPDATE", "НЕ ВДАЛОСЯ ПОЧАТИ ОНОВЛЕННЯ" },
    { "CANNOT WRITE TO SD CARD", "НЕМАЄ ЗАПИСУ НА SD-КАРТКУ" },
    { "CHECKING FOR UPDATES", "ПЕРЕВІРКА ОНОВЛЕНЬ" },
    { "CONNECTION FAILED - RETRY", "ЗБІЙ З'ЄДНАННЯ - ПОВТОРІТЬ" },
    { "DOWNLOAD CHECK FAILED", "ПЕРЕВІРКА ЗАВАНТАЖЕННЯ НЕ ВДАЛАСЯ" },
    { "DOWNLOADING UPDATE", "ЗАВАНТАЖЕННЯ ОНОВЛЕННЯ" },
    { "INSTALLATION FAILED", "ВСТАНОВЛЕННЯ НЕ ВДАЛОСЯ" },
    { "INSTALLING UPDATE", "ВСТАНОВЛЕННЯ ОНОВЛЕННЯ" },
    { "INVALID RELEASE DATA", "НЕКОРЕКТНІ ДАНІ РЕЛІЗУ" },
    { "NO PRE-RELEASE AVAILABLE", "ПЕРЕДРЕЛІЗІВ НЕМАЄ" },
    { "NO WI-FI CONNECTION", "НЕМАЄ З'ЄДНАННЯ WI-FI" },
    { "NOT ENOUGH SD SPACE", "ЗАМАЛО МІСЦЯ НА SD" },
    { "SD CARD WRITE FAILED", "ЗБІЙ ЗАПИСУ НА SD" },
    { "TLS SERVICE FAILED", "ЗБІЙ СЛУЖБИ TLS" },
    { "UPDATE AVAILABLE", "Є ОНОВЛЕННЯ" },
    { "UPDATE CANCELLED", "ОНОВЛЕННЯ СКАСОВАНО" },
    { "VERIFYING DOWNLOAD", "ПЕРЕВІРКА ЗАВАНТАЖЕННЯ" },
    { "YOU ARE UP TO DATE", "У ВАС ОСТАННЯ ВЕРСІЯ" },
};

/* Single words, for strings the port composes at runtime ("PAGE 2 OF 5",
 * "CHANNEL: STABLE", "CHANGELOG V2.1", the wrapped lines of the built-in
 * update text). Looked up without surrounding punctuation. */
static const PortUaPanelEntry kPortUaWords[] = {
    { "PAGE", "СТОР." },     { "OF", "З" },            { "CHANNEL", "КАНАЛ" },
    { "STABLE", "СТАБІЛЬНИЙ" }, { "PRE-RELEASE", "ПЕРЕДРЕЛІЗ" }, { "CHANGELOG", "ЗМІНИ" },
    { "INSTALLED", "ВСТАНОВЛЕНО" }, { "SELECT", "ОБЕРІТЬ" }, { "A", "" },
    { "RELEASE", "РЕЛІЗ" },  { "BELOW", "НИЖЧЕ" },     { "TO", "ЩОБ" },
    { "READ", "ЧИТАТИ" },    { "ITS", "ЙОГО" },        { "CHOOSE", "ОБЕРІТЬ" },
    { "OR", "АБО" },         { "CHECK", "ПЕРЕВІРИТИ" }, { "THAT", "ЦЕЙ" },
    { "UPDATE", "ОНОВЛЕННЯ" }, { "VERSION", "ВЕРСІЯ" }, { "NEW", "НОВЕ" },
    { "FIXED", "ВИПРАВЛЕНО" }, { "ADDED", "ДОДАНО" },  { "CHANGED", "ЗМІНЕНО" },
    { "REMOVED", "ВИДАЛЕНО" }, { "AND", "ТА" },        { "THE", "" },
    { "FOR", "ДЛЯ" },        { "WITH", "З" },          { "ON", "НА" },
    { "IN", "У" },           { "NOW", "ТЕПЕР" },       { "NO", "НЕМАЄ" },
};

/* --- encoding ------------------------------------------------------------ */

/* Byte code of a Ukrainian letter in the tloz-tmc-ua font. Returns 0 for
 * code points that are not Cyrillic letters the font has. */
static inline unsigned char Port_UA_CyrillicCode(unsigned cp) {
    /* A..Х are consecutive in Unicode but the font skips Russian-only letters,
     * so spell the mapping out: Ukrainian alphabet order == font order. */
    static const unsigned short kUpper[] = { 0x410, 0x411, 0x412, 0x413, 0x490, 0x414, 0x415, 0x404, 0x416,
                                             0x417, 0x418, 0x406, 0x407, 0x419, 0x41A, 0x41B, 0x41C, 0x41D,
                                             0x41E, 0x41F, 0x420, 0x421, 0x422, 0x423, 0x424, 0x425 };
    static const unsigned char kUpperCode[] = { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
                                                0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52,
                                                0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A };
    static const unsigned short kUpperExt[] = { 0x426, 0x427, 0x428, 0x429, 0x42C, 0x42E, 0x42F };
    static const unsigned char kUpperExtCode[] = { 0xC0, 0xC2, 0xC4, 0xC6, 0xC7, 0xC8, 0xC9 };
    size_t i;
    unsigned lower = 0;
    if (cp >= 0x430 && cp <= 0x44F) { /* а..я -> А..Я */
        cp -= 0x20;
        lower = 0x20;
    } else if (cp == 0x491 || cp == 0x454 || cp == 0x456 || cp == 0x457) { /* ґ є і ї */
        cp -= (cp == 0x491) ? 1 : 0x50;
        lower = 0x20;
    }
    for (i = 0; i < sizeof(kUpper) / sizeof(kUpper[0]); i++) {
        if (kUpper[i] == cp) {
            return (unsigned char)(kUpperCode[i] + lower);
        }
    }
    for (i = 0; i < sizeof(kUpperExt) / sizeof(kUpperExt[0]); i++) {
        if (kUpperExt[i] == cp) {
            return (unsigned char)(kUpperExtCode[i] + lower);
        }
    }
    return 0;
}

/* Where the Ukrainian font keeps a Latin letter.
 * bigFont == 0: message font — every Latin letter still exists, either as a
 *   look-alike Cyrillic glyph at its ASCII code or at a code >= 0x80 (bank 2).
 * bigFont != 0: banner font — only look-alikes exist; the rest is
 *   transliterated so a version string or changelog stays readable. */
static inline unsigned char Port_UA_LatinCode(unsigned char c, int bigFont) {
    static const unsigned char kSmallUpper[26] = {
        /* A */ 0x41, /* B */ 0x43, /* C */ 0x56, /* D */ 0xCA, /* E */ 0x47, /* F */ 0xCB, /* G */ 0xCC,
        /* H */ 0x52, /* I */ 0x4C, /* J */ 0xCE, /* K */ 0x4F, /* L */ 0xCF, /* M */ 0x51, /* N */ 0xD0,
        /* O */ 0x53, /* P */ 0x55, /* Q */ 0xD1, /* R */ 0xD2, /* S */ 0xD4, /* T */ 0x57, /* U */ 0xD6,
        /* V */ 0x8C, /* W */ 0xD9, /* X */ 0x5A, /* Y */ 0xDB, /* Z */ 0xDC,
    };
    static const unsigned char kSmallLower[26] = {
        /* a */ 0x61, /* b */ 0xEA, /* c */ 0x76, /* d */ 0xEB, /* e */ 0x67, /* f */ 0xEC, /* g */ 0x66,
        /* h */ 0xEE, /* i */ 0x6C, /* j */ 0xEF, /* k */ 0xF0, /* l */ 0xF1, /* m */ 0x71, /* n */ 0x74,
        /* o */ 0x73, /* p */ 0x75, /* q */ 0xF2, /* r */ 0xF4, /* s */ 0xF6, /* t */ 0x9C, /* u */ 0x6B,
        /* v */ 0xF9, /* w */ 0xFB, /* x */ 0x7A, /* y */ 0x78, /* z */ 0xFC,
    };
    /* Banner font: look-alikes where they exist, transliteration otherwise. */
    static const unsigned char kBigUpper[26] = {
        /* A */ 0x41, /* B */ 0x43, /* C */ 0x56, /* D */ 0x46, /* E */ 0x47, /* F */ 0x59, /* G */ 0x44,
        /* H */ 0x52, /* I */ 0x4C, /* J */ 0x49, /* K */ 0x4F, /* L */ 0x50, /* M */ 0x51, /* N */ 0x52,
        /* O */ 0x53, /* P */ 0x55, /* Q */ 0x4F, /* R */ 0x55, /* S */ 0x56, /* T */ 0x57, /* U */ 0x58,
        /* V */ 0x43, /* W */ 0x43, /* X */ 0x5A, /* Y */ 0x58, /* Z */ 0x4A,
    };
    if (c >= 'A' && c <= 'Z') {
        return bigFont ? kBigUpper[c - 'A'] : kSmallUpper[c - 'A'];
    }
    if (c >= 'a' && c <= 'z') {
        return bigFont ? (unsigned char)(kBigUpper[c - 'a'] + 0x20) : kSmallLower[c - 'a'];
    }
    return c;
}

/* Decodes one UTF-8 sequence; returns its length (1 on malformed input). */
static inline int Port_UA_Utf8(const char* s, unsigned* cp) {
    const unsigned char* u = (const unsigned char*)s;
    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        *cp = ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
        return 3;
    }
    *cp = '?';
    return 1;
}

/* UTF-8 (Ukrainian and/or ASCII) -> font byte codes. */
static inline void Port_UA_EncodeText(const char* utf8, char* out, size_t cap, int bigFont) {
    size_t n = 0;
    if (cap == 0) {
        return;
    }
    while (*utf8 && n + 1 < cap) {
        unsigned cp;
        unsigned char code;
        utf8 += Port_UA_Utf8(utf8, &cp);
        if (cp < 0x80) {
            code = Port_UA_LatinCode((unsigned char)cp, bigFont);
        } else if ((code = Port_UA_CyrillicCode(cp)) != 0) {
            /* Cyrillic letter: code already final. */
        } else if (cp == 0x2019 || cp == 0x02BC) { /* ’ ʼ */
            code = 0x27;
        } else if (cp == 0x2013 || cp == 0x2014) { /* – — */
            code = '-';
        } else {
            code = '?';
        }
        out[n++] = (char)code;
    }
    out[n] = '\0';
}

static inline const char* Port_UA_LookupWord(const char* word, size_t len) {
    size_t i;
    for (i = 0; i < sizeof(kPortUaWords) / sizeof(kPortUaWords[0]); i++) {
        if (strlen(kPortUaWords[i].en) == len && memcmp(kPortUaWords[i].en, word, len) == 0) {
            return kPortUaWords[i].ua;
        }
    }
    return NULL;
}

static inline void Port_UA_Append(char* dst, size_t cap, size_t* n, const char* src, size_t len) {
    while (len-- && *n + 1 < cap) {
        dst[(*n)++] = *src++;
    }
    dst[*n] = '\0';
}

/* English -> Ukrainian (UTF-8): whole string first, then word by word. */
static inline void Port_UA_TranslateText(const char* en, char* out, size_t cap) {
    size_t i, n = 0;
    const char* p;
    for (i = 0; i < sizeof(kPortUaStrings) / sizeof(kPortUaStrings[0]); i++) {
        if (strcmp(kPortUaStrings[i].en, en) == 0) {
            Port_UA_Append(out, cap, &n, kPortUaStrings[i].ua, strlen(kPortUaStrings[i].ua));
            return;
        }
    }
    out[0] = '\0';
    for (p = en; *p;) {
        const char* start;
        const char* end;
        const char* core;
        const char* coreEnd;
        const char* ua;
        if (*p == ' ') {
            Port_UA_Append(out, cap, &n, p, 1);
            p++;
            continue;
        }
        start = p;
        while (*p && *p != ' ') {
            p++;
        }
        end = p;
        /* strip punctuation around the word for the lookup, keep it in the output */
        core = start;
        coreEnd = end;
        while (core < coreEnd && strchr(":.,!?()[]\"'", *core) != NULL) {
            core++;
        }
        while (coreEnd > core && strchr(":.,!?()[]\"'", coreEnd[-1]) != NULL) {
            coreEnd--;
        }
        ua = (core < coreEnd) ? Port_UA_LookupWord(core, (size_t)(coreEnd - core)) : NULL;
        if (ua == NULL) {
            Port_UA_Append(out, cap, &n, start, (size_t)(end - start));
        } else {
            Port_UA_Append(out, cap, &n, start, (size_t)(core - start));
            Port_UA_Append(out, cap, &n, ua, strlen(ua));
            Port_UA_Append(out, cap, &n, coreEnd, (size_t)(end - coreEnd));
        }
    }
}

/**
 * Panel string hook. Returns `str` unchanged unless the Ukrainian ROM is
 * loaded; then returns `buf` filled with the translated, font-encoded text.
 * `buf` must outlive the use of the returned pointer (a local array at the
 * call site is fine).
 */
static inline const char* Port_UA_PanelText(const char* str, char* buf, size_t cap, int bigFont) {
    char utf8[512];
    if (str == NULL || buf == NULL || cap == 0 || !Port_IsUkrainianRom()) {
        return str;
    }
    Port_UA_TranslateText(str, utf8, sizeof(utf8));
    Port_UA_EncodeText(utf8, buf, cap, bigFont);
    return buf;
}

/* Message-font glyph for a Ukrainian-font byte code >= 0x80: the game keeps
 * those in bank 2 (src/text.c sub_0805F25C, non-JP), 64 bytes per glyph. */
static inline const u8* Port_UA_MessageGlyphHigh(unsigned char code, void* const* fontBanks) {
    if (code < 0x80 || !Port_IsUkrainianRom() || fontBanks == NULL || fontBanks[2] == NULL) {
        return NULL;
    }
    return (const u8*)fontBanks[2] + (size_t)(code - 0x80) * 64u;
}

/* One-liners for the call sites in port_second_screen_theme.c. */
#define PORT_UA_PANEL_TEXT(str, bigFont) \
    char ua_text_buf_[256];              \
    str = Port_UA_PanelText(str, ua_text_buf_, sizeof(ua_text_buf_), bigFont)

#endif /* PORT_UA_PANEL_H */
