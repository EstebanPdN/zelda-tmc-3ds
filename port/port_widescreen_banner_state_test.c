#include "port_widescreen_banner_state.h"

#include <stdio.h>
#include <string.h>

static int sFailures;

#define CHECK(condition, message)                     \
    do {                                              \
        if (!(condition)) {                           \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                              \
        }                                             \
    } while (0)

int main(void) {
    PortWidescreenBannerState state;
    memset(&state, 0, sizeof(state));

    CHECK(PORT_WS_ENTER_ROOM_BANNER_X0 == 0 && PORT_WS_ENTER_ROOM_BANNER_X1 == 240,
          "banner publishes the complete native row");
    CHECK(PORT_WS_ENTER_ROOM_BANNER_Y0 == 5 * 8 && PORT_WS_ENTER_ROOM_BANNER_Y1 == 7 * 8,
          "banner publishes the two BG0 rows cleared by its manager");
    CHECK(!Port_WidescreenBannerState_IsActive(&state, 1, 2, 3), "zeroed lifecycle is inactive");

    Port_WidescreenBannerState_Set(&state, 1, 2, 3);
    CHECK(Port_WidescreenBannerState_IsActive(&state, 1, 2, 3), "matching gameplay room stays active");
    CHECK(!Port_WidescreenBannerState_IsActive(&state, 1, 2, 4), "room transition retires banner");
    CHECK(!state.active, "room-transition retirement is persistent");

    Port_WidescreenBannerState_Set(&state, 1, 2, 3);
    CHECK(!Port_WidescreenBannerState_IsActive(&state, 0, 2, 3), "leaving gameplay retires banner");
    Port_WidescreenBannerState_Set(&state, 1, 2, 3);
    Port_WidescreenBannerState_Set(&state, 0, 2, 3);
    CHECK(!Port_WidescreenBannerState_IsActive(&state, 1, 2, 3), "manager cleanup retires banner");

    if (sFailures != 0) {
        return 1;
    }
    printf("port_widescreen_banner_state_test: ALL PASS\n");
    return 0;
}
