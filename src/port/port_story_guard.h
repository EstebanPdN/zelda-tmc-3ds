#ifndef PORT_STORY_GUARD_H
#define PORT_STORY_GUARD_H

#include <stdbool.h>

/* SOUGEN_01_ZELDA is set when the opening escort reaches Hyrule Town.
 * TABIDACHI is set substantially later, after the castle/sword sequence.
 * Consequently departed==true with escortComplete==false is impossible in a
 * vanilla playthrough and must never replay the opening Zelda entity list. */
static inline bool Port_ShouldRunZeldaIntro(bool escortComplete, bool departed) {
    return !escortComplete && !departed;
}

#endif /* PORT_STORY_GUARD_H */
