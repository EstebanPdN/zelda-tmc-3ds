#ifndef PORT_CYRILLIC_H
#define PORT_CYRILLIC_H

/* Russian interface support for the port's own UI (bottom screen, settings,
 * updater, fatal screens). The game's script is untouched: only strings the
 * port itself draws pass through here.
 *
 * Strings are UTF-8. The panel lettering is capitals only, so lowercase
 * Cyrillic folds onto the 32 capitals and Ё folds onto Е (common Russian
 * practice, and the cap height leaves no room for a diaeresis). */

#include <stdint.h>

#define PORT_CYR_LETTER_COUNT 32 /* А..Я */
#define PORT_CYR_NONE (-1)

/* Decodes one UTF-8 sequence and advances *p. Malformed or truncated input
 * yields '?' and consumes one byte, so a bad string can never stall a loop.
 * Returns 0 at the terminator without advancing. */
static inline uint32_t PortUtf8_Next(const char** p) {
    const unsigned char* s = (const unsigned char*)*p;
    uint32_t cp;
    int extra, i;
    if (s[0] == 0) {
        return 0;
    }
    if (s[0] < 0x80) {
        *p += 1;
        return s[0];
    }
    if ((s[0] & 0xE0) == 0xC0) {
        cp = s[0] & 0x1Fu;
        extra = 1;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = s[0] & 0x0Fu;
        extra = 2;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = s[0] & 0x07u;
        extra = 3;
    } else {
        *p += 1;
        return '?';
    }
    for (i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *p += 1;
            return '?';
        }
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    *p += 1 + extra;
    return cp;
}

/* Index 0..31 of the capital (А=0 .. Я=31) for any Cyrillic letter of the
 * Russian alphabet in either case, or PORT_CYR_NONE. */
static inline int PortCyr_LetterIndex(uint32_t cp) {
    if (cp >= 0x0410u && cp <= 0x042Fu) {
        return (int)(cp - 0x0410u);
    }
    if (cp >= 0x0430u && cp <= 0x044Fu) {
        return (int)(cp - 0x0430u);
    }
    if (cp == 0x0401u || cp == 0x0451u) {
        return 5; /* Ё -> Е */
    }
    return PORT_CYR_NONE;
}

/* Capitals whose shape is the Latin capital's (А В Е К М Н О Р С Т Х):
 * returns that ASCII letter so callers can reuse the game's own glyph,
 * or 0 when the letter needs Cyrillic artwork. */
static inline char PortCyr_LatinTwin(int index) {
    static const char kTwin[PORT_CYR_LETTER_COUNT] = {
        'A', 0,   'B', 0, 0, 'E', 0, 0, 0, 0, 'K', 0, 'M', 'H', 'O', 0,
        'P', 'C', 'T', 0, 0, 'X', 0, 0, 0, 0, 0,   0, 0,   0,   0,   0,
    };
    return (index >= 0 && index < PORT_CYR_LETTER_COUNT) ? kTwin[index] : 0;
}

/* Number of displayed characters (code points) in a UTF-8 string. */
static inline int32_t PortUtf8_Length(const char* str) {
    int32_t n = 0;
    if (str == 0) {
        return 0;
    }
    while (PortUtf8_Next(&str) != 0) {
        n++;
    }
    return n;
}

#endif /* PORT_CYRILLIC_H */
