#include <stdio.h>
#include <string.h>

#include "port_text_variables.h"

static int sFailures;

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            ++sFailures;                             \
        }                                            \
    } while (0)

static void CheckName(const char* name, const u8* expected, size_t expectedSize) {
    u8 source[FILENAME_LENGTH] = { 0 };
    u8 result[PORT_TEXT_PLAYER_VARIABLE_SIZE];

    memcpy(source, name, strlen(name) < sizeof(source) ? strlen(name) : sizeof(source));
    memset(result, 0xcc, sizeof(result));
    Port_FormatPlayerNameVariable(result, source);
    CHECK(memcmp(result, expected, expectedSize) == 0, name);
}

int main(void) {
    static const u8 link[] = { 2, 0xe, 'L', 'i', 'n', 'k', 2, 0xf, 0 };
    static const u8 fries[] = { 2, 0xe, 'F', 'r', 'i', 'e', 's', 2, 0xf, 0 };
    static const u8 sixLetters[] = { 2, 0xe, 'A', 'B', 'C', 'D', 'E', 'F', 2, 0xf, 0 };

    CHECK(PORT_TEXT_PLAYER_VARIABLE_SIZE == 11, "six-letter save names fit with color controls and terminator");
    CheckName("Link", link, sizeof(link));
    CheckName("Fries", fries, sizeof(fries));
    CheckName("ABCDEF", sixLetters, sizeof(sixLetters));

    if (sFailures != 0) return 1;
    puts("port_text_variables_test: ALL PASS");
    return 0;
}
