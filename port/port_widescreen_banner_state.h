#ifndef PORT_WIDESCREEN_BANNER_STATE_H
#define PORT_WIDESCREEN_BANNER_STATE_H

#include <stdint.h>

/* EnterRoomTextboxManager clears exactly two complete BG0 tilemap rows at
 * index 0xa0 (row 5), so this is the native band that must move as one unit
 * when the 240-pixel UI canvas is centered in a wide frame. */
#define PORT_WS_ENTER_ROOM_BANNER_X0 0
#define PORT_WS_ENTER_ROOM_BANNER_X1 240
#define PORT_WS_ENTER_ROOM_BANNER_Y0 40
#define PORT_WS_ENTER_ROOM_BANNER_Y1 56

typedef struct {
    uint8_t active;
    uint8_t area;
    uint8_t room;
} PortWidescreenBannerState;

static inline void Port_WidescreenBannerState_Set(PortWidescreenBannerState* state, int active,
                                                   uint8_t area, uint8_t room) {
    state->active = active != 0;
    state->area = area;
    state->room = room;
}

/* A room banner cannot survive a task or room change.  Clearing stale state
 * here makes global entity teardown safe even when it bypasses the manager's
 * ordinary timed cleanup callback. */
static inline int Port_WidescreenBannerState_IsActive(PortWidescreenBannerState* state, int inGame,
                                                       uint8_t area, uint8_t room) {
    if (!state->active) {
        return 0;
    }
    if (!inGame || state->area != area || state->room != room) {
        state->active = 0;
        return 0;
    }
    return 1;
}

#endif /* PORT_WIDESCREEN_BANNER_STATE_H */
