#include <stdio.h>

#include "port_door_transition.h"

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        unsigned got__ = (unsigned)(actual);                                                                     \
        unsigned want__ = (unsigned)(expected);                                                                  \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got 0x%X expected 0x%X\n", message, got__, want__);                      \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

int main(void) {
    CHECK_EQ(Port_SelectDoorFacingEdge(0x10, 0), 2u,
             "active south input wins over a stale north animation");
    CHECK_EQ(Port_SelectDoorFacingEdge(0x00, 4), 0u,
             "active north input wins over a stale south animation");
    CHECK_EQ(Port_SelectDoorFacingEdge(0xFF, 4), 2u,
             "DIR_NONE recovers the doorway edge from south animation facing");
    CHECK_EQ(Port_SelectDoorFacingEdge(0x14, 4), PORT_DOOR_EDGE_NONE,
             "diagonal input cannot be collapsed onto one edge");
    CHECK_EQ(Port_SelectDoorFacingEdge(0x81, 4), PORT_DOOR_EDGE_NONE,
             "non-DIR_NONE inactive values fail closed");

    CHECK_EQ(Port_FindDoorApproachEdge(136, 325, 272, 352, 2, 1), 2u,
             "v0.27 throne-room dump selects its south neighbor");
    CHECK_EQ(Port_FindDoorApproachEdge(136, 325, 272, 352, 1, 1), PORT_DOOR_EDGE_NONE,
             "the same doorway cannot select a lateral neighbor");
    CHECK_EQ(Port_FindDoorApproachEdge(419, 146, 272, 352, 1, 1), PORT_DOOR_EDGE_NONE,
             "v0.28 out-of-room lateral regression is rejected");
    CHECK_EQ(Port_FindDoorApproachEdge(136, 325, 272, 352, 2, 0), PORT_DOOR_EDGE_NONE,
             "ordinary floor near an edge cannot trigger a door scroll");
    CHECK_EQ(Port_FindDoorApproachEdge(136, 319, 272, 352, 2, 1), PORT_DOOR_EDGE_NONE,
             "a door floor farther than the collision approach band is rejected");
    CHECK_EQ(Port_FindDoorApproachEdge(136, 32, 272, 352, 0, 1), 0u,
             "the north collision approach boundary is inclusive");
    CHECK_EQ(Port_FindDoorApproachEdge(240, 176, 272, 352, 1, 1), 1u,
             "the east collision approach boundary is inclusive");
    CHECK_EQ(Port_FindDoorApproachEdge(32, 176, 272, 352, 3, 1), 3u,
             "the west collision approach boundary is inclusive");
    CHECK_EQ(Port_FindDoorApproachEdge(-1, 20, 272, 352, 3, 1), PORT_DOOR_EDGE_NONE,
             "negative room-local coordinates fail closed");
    CHECK_EQ(Port_FindDoorApproachEdge(20, 20, 272, 352, 4, 1), PORT_DOOR_EDGE_NONE,
             "non-cardinal edge values fail closed");
    CHECK_EQ(Port_FindDoorApproachEdge(272, 176, 272, 352, 1, 1), PORT_DOOR_EDGE_NONE,
             "the first coordinate outside the room fails closed");

    if (sFailures != 0) {
        return 1;
    }
    puts("port_door_transition_test: ALL PASS");
    return 0;
}
