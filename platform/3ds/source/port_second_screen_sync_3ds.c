#include "port_second_screen_sync_3ds.h"

#include <3ds/synchronization.h>

/* Separate locks avoid imposing an order between the compositor's UI state
 * and the engine snapshot exchange. They are initialized before either the
 * game loop or BottomWorkerMain can publish, paint, or dispatch a tap. */
static LightLock sUiLock;
static LightLock sSnapshotLock;

void Port_SecondScreen_3DS_SyncInit(void) {
    LightLock_Init(&sUiLock);
    LightLock_Init(&sSnapshotLock);
}

void Port_SecondScreen_3DS_LockUI(void) {
    LightLock_Lock(&sUiLock);
}

void Port_SecondScreen_3DS_UnlockUI(void) {
    LightLock_Unlock(&sUiLock);
}

void Port_SecondScreen_3DS_LockSnapshot(void) {
    LightLock_Lock(&sSnapshotLock);
}

void Port_SecondScreen_3DS_UnlockSnapshot(void) {
    LightLock_Unlock(&sSnapshotLock);
}
