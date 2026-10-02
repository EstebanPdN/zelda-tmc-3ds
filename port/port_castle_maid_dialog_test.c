#include <stdio.h>

#include "port_castle_maid_dialog.h"

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        unsigned got__ = (unsigned)(actual);                                                                     \
        unsigned want__ = (unsigned)(expected);                                                                  \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got %u expected %u\n", message, got__, want__);                         \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

int main(void) {
    CHECK_EQ(Port_ResolveCastleMaidDialog(0x0806464Du), PORT_CASTLE_MAID_DIALOG_CASTLE,
             "USA castle callback resolves from its Thumb address");
    CHECK_EQ(Port_ResolveCastleMaidDialog(0x080640D5u), PORT_CASTLE_MAID_DIALOG_CASTLE,
             "EU castle callback resolves from its Thumb address");
    CHECK_EQ(Port_ResolveCastleMaidDialog(0x0806448Du), PORT_CASTLE_MAID_DIALOG_CASTLE,
             "JP castle callback resolves from its Thumb address");
    CHECK_EQ(Port_ResolveCastleMaidDialog(0x08064689u), PORT_CASTLE_MAID_DIALOG_TOWN,
             "USA town callback resolves from its Thumb address");
    CHECK_EQ(Port_ResolveCastleMaidDialog(0x08064111u), PORT_CASTLE_MAID_DIALOG_TOWN,
             "EU town callback resolves from its Thumb address");
    CHECK_EQ(Port_ResolveCastleMaidDialog(0x080644C9u), PORT_CASTLE_MAID_DIALOG_TOWN,
             "JP town callback resolves from its Thumb address");
    CHECK_EQ(Port_ResolveCastleMaidDialog(0x08000001u), PORT_CASTLE_MAID_DIALOG_NONE,
             "unknown callbacks fail closed");

    if (sFailures != 0) {
        return 1;
    }
    puts("port_castle_maid_dialog_test: ALL PASS");
    return 0;
}
