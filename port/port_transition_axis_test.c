#include <stdio.h>

#include "scroll.h"

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                          \
    do {                                                                                                             \
        const int actualValue = (actual);                                                                            \
        const int expectedValue = (expected);                                                                        \
        if (actualValue != expectedValue) {                                                                          \
            fprintf(stderr, "FAIL: %s: got 0x%04x expected 0x%04x\n", message, actualValue & 0xffff,                \
                    expectedValue & 0xffff);                                                                         \
            sFailures++;                                                                                             \
        }                                                                                                            \
    } while (0)

int main(void) {
    CHECK_EQ(DecodePreservedTransitionAxis(0x8a1d, 0x0870), 0x01ad,
             "preserved Minish Woods Y becomes room-local");
    CHECK_EQ((u16)DecodePreservedTransitionAxis(0x0ba8, 0x0000), 0x0ba8, "fixed X is unchanged");
    CHECK_EQ((u16)DecodePreservedTransitionAxis(0x01f8, 0x0000), 0x01f8, "fixed transition endpoint is unchanged");
    CHECK_EQ((u16)DecodePreservedTransitionAxis(0x8008, 0x0010), 0xfff8, "preserved axis can move before origin");

    CHECK_EQ(IsPointInsideRoomBounds(240, 432, 240, 432, 256, 160), TRUE, "room origin is inside");
    CHECK_EQ(IsPointInsideRoomBounds(495, 591, 240, 432, 256, 160), TRUE, "last room pixel is inside");
    CHECK_EQ(IsPointInsideRoomBounds(239, 432, 240, 432, 256, 160), FALSE, "one pixel west is outside");
    CHECK_EQ(IsPointInsideRoomBounds(496, 432, 240, 432, 256, 160), FALSE, "east endpoint is outside");
    CHECK_EQ(IsPointInsideRoomBounds(240, 431, 240, 432, 256, 160), FALSE, "one pixel north is outside");
    CHECK_EQ(IsPointInsideRoomBounds(240, 592, 240, 432, 256, 160), FALSE, "south endpoint is outside");
    CHECK_EQ(IsPointInsideRoomBounds(240, 432, 240, 432, 0, 160), FALSE, "zero-width room rejects points");
    CHECK_EQ(IsPointInsideRoomBounds(223, 524, 240, 432, 240, 160), FALSE,
             "dump 06 cannot chain a second transition from west of room 22/11");
    CHECK_EQ(IsPointInsideRoomBounds(675, 132, 384, 16, 272, 352), FALSE,
             "dump 08 cannot chain a second transition from east of room 80/02");
    CHECK_EQ(IsPointInsideRoomBounds(255, 524, 240, 432, 240, 160), TRUE,
             "canonical 15-pixel post-scroll carry remains inside room 22/11");

    if (sFailures == 0) {
        puts("port_transition_axis_test: ALL PASS");
        return 0;
    }
    fprintf(stderr, "port_transition_axis_test: %d FAILED\n", sFailures);
    return 1;
}
