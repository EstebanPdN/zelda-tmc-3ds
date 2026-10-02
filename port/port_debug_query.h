#ifndef PORT_DEBUG_QUERY_H
#define PORT_DEBUG_QUERY_H

#ifdef __cplusplus
extern "C" {
#endif

/* NULL when no friendly name is recorded for the area. */
const char* Port_DebugQuery_AreaName(unsigned char area);

/* Returns 0 when the area has no valid room table. */
int Port_DebugQuery_AreaRoomCount(unsigned char area);

/* Returns 1 and optionally fills width/height when the room exists. */
int Port_DebugQuery_RoomDimensions(unsigned char area, unsigned char room,
                                   unsigned short* w, unsigned short* h);

/* Shared predicate for debug/UI warp lists. */
int Port_DebugAction_AreaIsWarpable(unsigned char area);

#ifdef __cplusplus
}
#endif

#endif /* PORT_DEBUG_QUERY_H */
