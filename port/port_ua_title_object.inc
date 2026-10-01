/*
 * tloz-tmc-ua: Ukrainian title-screen objects (included at the end of
 * src/object/titleScreenObject.c).
 *
 * In the Ukrainian title the logo (type 0) and the subtitle (type 1) rise from
 * below the screen to their final place, the TM/® pieces (types 2, 3) stay
 * static and the animated Ezlo hat (type 4) is not shown. Mirrors
 * tloz-tmc-ua/tmc/src/object/titleScreenObject.c. Active only for the TMC-UA
 * ROM; every other ROM runs the upstream TitleScreenObject().
 */
#ifdef PC_PORT
#include "common.h"
#include "menu.h"
#include "port_ua.h"

#define PORT_UA_LOGO_FINAL_X 100
#define PORT_UA_LOGO_FINAL_Y 54
#define PORT_UA_SUBTITLE_OFFSET_X 56
#define PORT_UA_SUBTITLE_OFFSET_Y 0
#define PORT_UA_SUBTITLE_PALETTE 6
#define PORT_UA_ANIMATION_START_Y 180
#define PORT_UA_ANIMATION_SPEED (2 << 16) /* 2 px per frame, 16.16 fixed point */

/* Final position, frame and priority per type (same layout as gUnk_08125014). */
static const struct_08125014 sPortUaTitlePieces[] = {
    { PORT_UA_LOGO_FINAL_X, PORT_UA_LOGO_FINAL_Y, 16, 4, 0 },
    { PORT_UA_LOGO_FINAL_X + PORT_UA_SUBTITLE_OFFSET_X, PORT_UA_LOGO_FINAL_Y + PORT_UA_SUBTITLE_OFFSET_Y, 22, 5, 0 },
    { 190, 20, 25, 2, 0 },
    { 192, 81, 26, 2, 0 },
    { 32, 104, 27, 3, 0 },
};

static void Port_UA_TitlePieceInit(Entity* this) {
    const struct_08125014* ptr = &sPortUaTitlePieces[this->type];
    this->x.HALF.HI = ptr->unk_0;
    this->y.HALF.HI = ptr->unk_2;
    this->frameIndex = ptr->unk_4;
    this->spritePriority.b0 = ptr->unk_6;
    this->spriteRendering.b3 = 1;
    this->spriteOrientation.flipY = 1;
    this->spriteSettings.draw = 2;
    this->action = 1;
}

/* types 0 and 1: rise from the bottom */
static void Port_UA_TitlePieceRise(Entity* this) {
    const struct_08125014* data = &sPortUaTitlePieces[this->type];

    switch (this->action) {
        case 0:
            Port_UA_TitlePieceInit(this);
            if (this->type == 1) {
                this->palette.b.b0 = PORT_UA_SUBTITLE_PALETTE;
                this->y.WORD = (PORT_UA_ANIMATION_START_Y + PORT_UA_SUBTITLE_OFFSET_Y) << 16;
            } else {
                this->y.WORD = PORT_UA_ANIMATION_START_Y << 16;
            }
            break;
        case 1:
            this->y.WORD -= PORT_UA_ANIMATION_SPEED;
            if (this->y.HALF.HI <= data->unk_2) {
                this->y.HALF.HI = data->unk_2;
                this->y.HALF.LO = 0;
                this->action = 2;
            }
            if ((gInput.newKeys & (A_BUTTON | START_BUTTON)) || (gMenu.menuType > 1)) {
                this->y.HALF.HI = data->unk_2;
                this->y.HALF.LO = 0;
                this->action = 2;
            }
            break;
        case 2:
            break;
    }
}

/* types 2 and 3: static */
static void Port_UA_TitlePieceStatic(Entity* this) {
    if (this->action == 0) {
        Port_UA_TitlePieceInit(this);
    }
}

static bool32 Port_UA_TitleScreenObject(Entity* this) {
    if (!Port_IsUkrainianRom()) {
        return FALSE;
    }
    switch (this->type) {
        case 0:
        case 1:
            Port_UA_TitlePieceRise(this);
            break;
        case 2:
        case 3:
            Port_UA_TitlePieceStatic(this);
            break;
        default:
            /* type 4, the Ezlo hat animation: hidden in the Ukrainian title */
            break;
    }
    return TRUE;
}
#endif /* PC_PORT */
