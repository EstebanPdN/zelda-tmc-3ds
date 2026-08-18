#include "port_second_screen_state.h"

#include <stdio.h>
#include <string.h>

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                \
    do {                                                                                   \
        unsigned got__ = (unsigned)(actual);                                               \
        unsigned want__ = (unsigned)(expected);                                            \
        if (got__ != want__) {                                                             \
            fprintf(stderr, "FAIL: %s: got %u expected %u\n", message, got__, want__); \
            sFailures++;                                                                   \
        }                                                                                  \
    } while (0)

int main(void) {
    SecondScreenSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    CHECK_EQ(Port_SecondScreenChargeVisible(&snapshot), 0, "inactive charge is hidden");
    snapshot.chargeAction = 1;
    CHECK_EQ(Port_SecondScreenChargeVisible(&snapshot), 0, "visible top HUD owns the charge cue");
    snapshot.topHudHidden = 1;
    CHECK_EQ(Port_SecondScreenChargeVisible(&snapshot), 1, "hidden top HUD publishes active charge");

    snapshot.chargeTimer = -1;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 0, "negative pre-charge timer is empty");
    snapshot.chargeTimer = 0;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 0, "zero timer is empty");
    snapshot.chargeTimer = 1;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 1, "first native tick fills one quarter");
    snapshot.chargeTimer = 20;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 1, "twenty ticks remain one quarter");
    snapshot.chargeTimer = 21;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 2, "twenty-one ticks round up");
    snapshot.chargeTimer = 799;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 40, "last pre-full tick rounds to full");
    snapshot.chargeTimer = 800;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 40, "native full timer is forty quarters");
    snapshot.chargeTimer = 32767;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 40, "oversized timer is clamped");

    snapshot.topHudHidden = 0;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 0, "visible top HUD suppresses bottom progress");
    snapshot.topHudHidden = 1;
    snapshot.chargeAction = 0;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 0, "released sword suppresses bottom progress");
    CHECK_EQ(Port_SecondScreenChargeVisible(NULL), 0, "null snapshot is hidden safely");
    CHECK_EQ(Port_SecondScreenChargeSteps(NULL), 0, "null snapshot has no progress");

    if (sFailures != 0) {
        return 1;
    }
    printf("port_second_screen_charge_test: ALL PASS\n");
    return 0;
}
