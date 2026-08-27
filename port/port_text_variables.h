#ifndef PORT_TEXT_VARIABLES_H
#define PORT_TEXT_VARIABLES_H

#include "save.h"

#define PORT_TEXT_PLAYER_VARIABLE_SIZE (2 + FILENAME_LENGTH + 3)

static inline void Port_FormatPlayerNameVariable(u8* dest, const u8 name[FILENAME_LENGTH]) {
    u32 i;

    dest[0] = 2;
    dest[1] = 0xe;
    dest += 2;
    for (i = 0; i < FILENAME_LENGTH && name[i] != '\0'; ++i) {
        *dest++ = name[i];
    }
    dest[0] = 2;
    dest[1] = 0xf;
    dest[2] = '\0';
}

#endif /* PORT_TEXT_VARIABLES_H */
