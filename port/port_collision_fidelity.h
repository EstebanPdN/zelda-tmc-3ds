#pragma once

#include "port_types.h"

/* The second byte in one of the original transition-tile records is the
 * collision layer that must be preserved.  Every other current layer moves
 * to the record's destination layer.  This mirrors the `cmp`/`beq` sequence
 * in CheckOnLayerTransition at 0x08016A68. */
static inline u8 Port_ApplyCollisionLayerTransition(u8 currentLayer, u8 preservedLayer, u8 destinationLayer) {
    return currentLayer == preservedLayer ? currentLayer : destinationLayer;
}

/* Issue #139 added an other-layer lookup for the lantern's ignitable flag.
 * That compatibility exception must not leak into movement, roof priority,
 * or projectile flags queried through the same generic GBA routine. */
static inline int Port_ShouldFallbackTilePropertyLayer(u32 mask) {
    return mask == 0x40u;
}
