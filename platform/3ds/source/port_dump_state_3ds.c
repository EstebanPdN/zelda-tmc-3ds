#include "port_dump_state_3ds.h"

#include "fade.h"
#include "game.h"
#include "main.h"
#include "player.h"
#include "region.h"
#include "room.h"
#include "save.h"

#include "platform_3ds.h"

#include <stdio.h>
#include <string.h>

bool Port_DumpState3DS_Write(const char* dumpDirectory) {
    char path[192];
    SaveFile captured;

    if (dumpDirectory == NULL || gMain.task != TASK_GAME || gMain.state != GAMETASK_MAIN ||
        snprintf(path, sizeof(path), "%s/%s", dumpDirectory, PORT_DUMP_LOAD_STATE_FILENAME) >= (int)sizeof(path)) {
        return false;
    }

    memcpy(&captured, &gSave, sizeof(captured));
    PlayerRoomStatus* status = &captured.saved_status;
    memcpy(status, &gRoomTransition.player_status, sizeof(*status));
    status->area_next = gRoomControls.area;
    status->room_next = gRoomControls.room;
    status->start_anim = gPlayerEntity.base.animationState;
    status->spawn_type = (gPlayerState.flags & PL_MINISH) ? PL_SPAWN_MINISH : PL_SPAWN_DEFAULT;
    status->start_pos_x = gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x;
    status->start_pos_y = gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y;
    status->layer = gPlayerEntity.base.collisionLayer;
    return Port_DumpState_WriteFile(path, gActiveRegion, &captured);
}

PortDumpStateResult Port_DumpState3DS_LoadLatest(void) {
    PortDumpStateData state;
    PortDumpStateResult result = Port_DumpState_ReadLatest("dumps", gActiveRegion, &state);

    if (result != PORT_DUMP_STATE_OK && result != PORT_DUMP_STATE_OK_LEGACY)
        return result;

    Platform3DS_MarkFrameDiscontinuity(OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP);
    memcpy(&gSave, &state.save, sizeof(gSave));
    gSaveHeader->msg_speed = gSave.msg_speed;
    gSaveHeader->brightness = gSave.brightness;
    {
        extern void Rando_Runtime_Refresh(void);
        Rando_Runtime_Refresh();
    }
    SetFade(FADE_IN_OUT | FADE_INSTANT, 8);
    SetTask(TASK_GAME);
    return result;
}
