#pragma once

#include "port_types.h"

typedef enum {
    PORT_CASTLE_MAID_DIALOG_NONE,
    PORT_CASTLE_MAID_DIALOG_CASTLE,
    PORT_CASTLE_MAID_DIALOG_TOWN,
} PortCastleMaidDialog;

/*
 * The maid scripts store a region-native GBA function pointer in the script
 * integer variable.  Those pointers cannot be called by the native port, so
 * translate each retail address to the corresponding host implementation.
 */
static inline PortCastleMaidDialog Port_ResolveCastleMaidDialog(u32 rawAddress) {
    switch (rawAddress & ~1u) {
        /* sub_0806464C: USA, EU, JP */
        case 0x0806464Cu:
        case 0x080640D4u:
        case 0x0806448Cu:
            return PORT_CASTLE_MAID_DIALOG_CASTLE;

        /* sub_08064688: USA, EU, JP */
        case 0x08064688u:
        case 0x08064110u:
        case 0x080644C8u:
            return PORT_CASTLE_MAID_DIALOG_TOWN;

        default:
            return PORT_CASTLE_MAID_DIALOG_NONE;
    }
}
