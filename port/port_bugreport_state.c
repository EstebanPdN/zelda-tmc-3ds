/*
 * port_bugreport_state.c — C-side helper that reads the game state structs
 * for Port_BugReport_Capture. Lives in C so it can include the game
 * headers (the C++ side is plain SDL/filesystem).
 */

#include "structures.h"
#include "save.h"
#include "room.h"

struct PortBugReportState {
    unsigned char area;
    unsigned char room;
    short playerX;
    short playerY;
    short playerZ;
    unsigned char playerHealth;
    unsigned char playerMaxHealth;
    int frameCount;
};

extern struct PortBugReportState Port_BugReport_GetGameState(void);

struct PortBugReportState Port_BugReport_GetGameState(void) {
    struct PortBugReportState s;
    s.area = (unsigned char)gRoomControls.area;
    s.room = (unsigned char)gRoomControls.room;
    s.playerX = (short)gPlayerEntity.base.x.HALF.HI;
    s.playerY = (short)gPlayerEntity.base.y.HALF.HI;
    s.playerZ = (short)gPlayerEntity.base.z.HALF.HI;
    s.playerHealth = (unsigned char)gSave.stats.health;
    s.playerMaxHealth = (unsigned char)gSave.stats.maxHealth;
    s.frameCount = (int)gRoomTransition.frameCount;
    return s;
}
