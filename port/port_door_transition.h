#ifndef PORT_DOOR_TRANSITION_H
#define PORT_DOOR_TRANSITION_H

#include "global.h"

#define PORT_DOOR_EDGE_NONE 0xFFu
#define PORT_DOOR_APPROACH_BAND 0x20

/* Select one edge only from a cardinal input direction. Once a doorway
 * action has consumed input and produced DIR_NONE, the last animation facing
 * is the remaining production signal. Diagonal or malformed directions do
 * not get reduced to an arbitrary edge. */
static inline u32 Port_SelectDoorFacingEdge(u32 direction, u32 animationState) {
    if ((direction & 0x87u) == 0) {
        return direction >> 3;
    }
    if (direction == 0xFFu) {
        return (animationState & 6u) >> 1;
    }
    return PORT_DOOR_EDGE_NONE;
}

/* Return the single room edge implied by a door floor and Link's facing.
 * This does not start or choose a transition: the caller must preserve the
 * production order of explicit-exit resolution followed by adjacent-room
 * resolution. */
static inline u32 Port_FindDoorApproachEdge(s32 relX, s32 relY, u32 roomWidth, u32 roomHeight, u32 facingEdge,
                                            u32 onDoorFloor) {
    if (!onDoorFloor || roomWidth == 0 || roomHeight == 0 || relX < 0 || relY < 0 || relX >= (s32)roomWidth ||
        relY >= (s32)roomHeight) {
        return PORT_DOOR_EDGE_NONE;
    }

    switch (facingEdge) {
        case 0: /* north */
            return relY <= PORT_DOOR_APPROACH_BAND ? 0u : PORT_DOOR_EDGE_NONE;
        case 1: /* east */
            return relX >= (s32)roomWidth - PORT_DOOR_APPROACH_BAND ? 1u : PORT_DOOR_EDGE_NONE;
        case 2: /* south */
            return relY >= (s32)roomHeight - PORT_DOOR_APPROACH_BAND ? 2u : PORT_DOOR_EDGE_NONE;
        case 3: /* west */
            return relX <= PORT_DOOR_APPROACH_BAND ? 3u : PORT_DOOR_EDGE_NONE;
        default:
            return PORT_DOOR_EDGE_NONE;
    }
}

#endif /* PORT_DOOR_TRANSITION_H */
