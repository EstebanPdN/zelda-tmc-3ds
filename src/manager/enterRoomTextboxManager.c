/**
 * @file enterRoomTextboxManager.c
 * @ingroup Managers
 *
 * @brief Shows textbox when entering the room.
 */
#include "manager/enterRoomTextboxManager.h"
#include "area.h"
#include "common.h"
#include "fileselect.h"
#include "game.h"
#include "message.h"
#include "screen.h"
#include "vram.h"
#include "fade.h"
#ifdef PC_PORT
#include "../port/port_tts.h"
#endif

const u16 gUnk_08108DE8[] = { 0,     0x70b, 0x70c, 0x70d, 0x70e, 0x70f, 0x710, 0x711, 0x712, 0x713, 0x714,
                              0x715, 0x716, 0x717, 0x718, 0x719, 0x71a, 0x71b, 0x71c, 0x71d, 0x71e, 0x71f,
                              0x726, 0x720, 0x720, 0x721, 0x722, 0x723, 0x725, 0x727, 0x724, 0x728 };
void sub_0805E140(EnterRoomTextboxManager*);
void sub_0805E18C(EnterRoomTextboxManager*);
void (*const EnterRoomTextboxManager_Actions[])(EnterRoomTextboxManager*) = {
    sub_0805E140,
    sub_0805E18C,
};

extern const Font gUnk_08108E30;
extern const Font gUnk_08108E48;
extern const u8 gUnk_08108E60[];

void sub_0805E140(EnterRoomTextboxManager*);
void sub_0805E18C(EnterRoomTextboxManager*); // unused?
void sub_0805E1D8(EnterRoomTextboxManager*);
void sub_0805E1F8(u32, bool32);

void EnterRoomTextboxManager_Main(EnterRoomTextboxManager* this) {
    EnterRoomTextboxManager_Actions[super->action](this);
    /* Kill the location-name banner the moment anything else starts a
     * message (e.g. Zelda calling out from off-screen as you leave the
     * house). Without this the banner lingers and the user sees a "stray"
     * box pop up centered before the dialog opens off to the left. */
    if ((gRoomControls.room != this->unk_20) || (gMessage.state & MESSAGE_ACTIVE)) {
        sub_0805E1D8(this);
    }
}

void sub_0805E140(EnterRoomTextboxManager* this) {
    super->flags |= ENT_PERSIST;
    super->action = 1;
    this->unk_20 = gRoomControls.room;
    super->timer = 120;
    super->subtimer = 60;
    SetEntityPriority((Entity*)this, PRIO_HIGHEST);
    sub_0805E1F8(gUnk_08108DE8[gArea.locationIndex], AreaIsDungeon());
}

void sub_0805E18C(EnterRoomTextboxManager* this) {
    if (gFadeControl.active)
        return;
    if (super->type2) {
        if (--super->subtimer == 0) {
            super->type2 = 0;
            gPlayerState.controlMode = CONTROL_1;
            ClearEventPriority();
        }
    }
    if (--super->timer == 0) {
        sub_0805E1D8(this);
    }
}

void sub_0805E1D8(EnterRoomTextboxManager* this) {
    /* A first-visit banner temporarily owns both player control and the event
     * priority. If another message dismisses it before the normal 60-frame
     * release, return what the banner owns before deleting the manager. */
    if (super->type2) {
        super->type2 = 0;
        gPlayerState.controlMode = CONTROL_1;
        ClearEventPriority();
    }
    MemClear(&gBG0Buffer[0xa0], 0x80);
    gScreen.bg0.updated = 1;
    DeleteThisEntity();
}

void sub_0805E1F8(u32 unk0, bool32 isDungeon) {
    struct {
        u8 unk_00[4];
        u8 unk_04;
        u8 unk_05;
        u8 unk_06[3];
    } PACKED tmp;
    const Font* font;
    MemClear(&gBG0Buffer[0xa0], 0x80);
    MemCopy(gUnk_08108E60, &tmp, sizeof(tmp));
    tmp.unk_04 = unk0 >> 8;
    tmp.unk_05 = unk0;
    font = &gUnk_08108E48;
    if (!isDungeon)
        font = &gUnk_08108E30;
    ShowTextBox((uintptr_t)&tmp, font);
    gScreen.bg0.updated = 1;
#ifdef PC_PORT
    /* Announce the new area / dungeon name to TTS. The visual banner
     * is built around a tiny wrapper buffer that re-targets a textIndex,
     * so we just feed the textIndex directly to the speech path —
     * dedupe inside Port_TTS_Speak suppresses re-announcing when the
     * player crosses room boundaries inside the same area. */
    if (unk0 != 0) {
        Port_TTS_SpeakTextIndex(unk0);
    }
#endif
}
