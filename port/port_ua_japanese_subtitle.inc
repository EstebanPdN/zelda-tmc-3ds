/*
 * tloz-tmc-ua: Ukrainian title subtitle objects (included at the end of
 * src/object/japaneseSubtitle.c).
 *
 * The Ukrainian title reuses the Japanese subtitle object: type 0 flies the cap
 * in and spawns the seven subtitle pieces, type 1 slides each piece in from the
 * left to its final position (blue palette), types 2/3 are the cap's eye and
 * Ezlo's hat at new coordinates. Mirrors
 * tloz-tmc-ua/tmc/src/object/japaneseSubtitle.c. Active only for the TMC-UA
 * ROM; other ROMs run the upstream JapaneseSubtitle().
 */
#ifdef PC_PORT
#include "common.h"
#include "port_ua.h"

#define PORT_UA_SUB_PALETTE 10
#define PORT_UA_SUB_FIRST_DELAY 1   /* frames before the first piece moves */
#define PORT_UA_SUB_DELAY_STEP 3    /* extra frames per following piece */
#define PORT_UA_SUB_SPEED 15        /* px per frame */
#define PORT_UA_SUB_X 10
#define PORT_UA_SUB_Y 101
#define PORT_UA_CAP_X 200
#define PORT_UA_CAP_Y 73
#define PORT_UA_CAP_EYE_X (PORT_UA_CAP_X + 4)
#define PORT_UA_CAP_EYE_Y (PORT_UA_CAP_Y - 29)
#define PORT_UA_HAT_X 180
#define PORT_UA_HAT_Y 25

typedef struct {
    u16 x;
    u16 y;
} PortUaSubtitleCoords;

static const PortUaSubtitleCoords sPortUaSubtitleFinal[7] = {
    { 48 + PORT_UA_SUB_X, PORT_UA_SUB_Y },  { 80 + PORT_UA_SUB_X, PORT_UA_SUB_Y },
    { 104 + PORT_UA_SUB_X, PORT_UA_SUB_Y + 8 }, { 136 + PORT_UA_SUB_X, PORT_UA_SUB_Y },
    { 160 + PORT_UA_SUB_X, PORT_UA_SUB_Y }, { 184 + PORT_UA_SUB_X, PORT_UA_SUB_Y },
    { 200 + PORT_UA_SUB_X, PORT_UA_SUB_Y },
};

/* type 0: the cap flies in, the pieces are spawned one per frame */
static void Port_UA_Subtitle_Type0(Entity* this) {
    Entity* piece;

    switch (this->action) {
        case 0:
            this->action = 1;
            this->spriteSettings.draw = 2;
            this->frameIndex = 9;
            this->timer = 8;
            this->subtimer = 0;
            this->spriteRendering.b3 = 1;
            this->spritePriority.b0 = 4;
            this->spriteOrientation.flipY = 1;
            sub_0806FB00(this, PORT_UA_CAP_X, PORT_UA_CAP_Y, 30);
            break;
        case 1:
            if ((this->subtimer < 7) && (--this->timer == 0)) {
                this->timer = 1;
                CreateObject(JAPANESE_SUBTITLE, 1, this->subtimer);
                this->subtimer++;
            }
            if (sub_0806FB38(this) || (1 < gMenu.menuType)) {
                this->x.HALF.HI = PORT_UA_CAP_X;
                this->y.HALF.HI = PORT_UA_CAP_Y;
                this->frameIndex = 10;
                this->action = 2;
                while (this->subtimer < 7) {
                    CreateObject(JAPANESE_SUBTITLE, 1, this->subtimer);
                    this->subtimer++;
                }
                piece = CreateObject(JAPANESE_SUBTITLE, 2, 0);
                if (piece != NULL) {
                    piece->x.HALF.HI = PORT_UA_CAP_EYE_X;
                    piece->y.HALF.HI = PORT_UA_CAP_EYE_Y;
                    SortEntityAbove(this, piece);
                }
                piece = CreateObject(JAPANESE_SUBTITLE, 3, 0);
                if (piece != NULL) {
                    piece->x.HALF.HI = PORT_UA_HAT_X;
                    piece->y.HALF.HI = PORT_UA_HAT_Y;
                    SortEntityAbove(this, piece);
                }
            }
            break;
    }
}

/* type 1: one subtitle piece sliding in from the left */
static void Port_UA_Subtitle_Type1(Entity* this) {
    const PortUaSubtitleCoords* coords = &sPortUaSubtitleFinal[this->type2 < 7 ? this->type2 : 6];

    this->palette.b.b0 = PORT_UA_SUB_PALETTE;
    switch (this->action) {
        case 0:
            this->action = 1;
            this->spriteSettings.draw = 2;
            this->frameIndex = this->type2 + 2;
            this->spritePriority.b0 = 7 - this->type2;
            this->y.HALF.HI = coords->y;
            this->x.HALF.HI = -16;
            this->timer = PORT_UA_SUB_FIRST_DELAY + this->type2 * PORT_UA_SUB_DELAY_STEP;
            break;
        case 1:
            if (--this->timer == 0) {
                this->action = 2;
            }
            break;
        case 2:
            this->x.HALF.HI += PORT_UA_SUB_SPEED;
            if (this->x.HALF.HI >= coords->x) {
                this->x.HALF.HI = coords->x;
                this->action = 3;
            }
            break;
        case 3:
            break;
    }
    if (gInput.newKeys & (A_BUTTON | START_BUTTON)) {
        this->x.HALF.HI = coords->x;
        this->y.HALF.HI = coords->y;
        this->action = 3;
    }
}

static bool32 Port_UA_JapaneseSubtitle(Entity* this) {
    if (!Port_IsUkrainianRom()) {
        return FALSE;
    }
    switch (this->type) {
        case 0:
            Port_UA_Subtitle_Type0(this);
            break;
        case 1:
            Port_UA_Subtitle_Type1(this);
            break;
        case 2:
            JapaneseSubtitle_Type2(this); /* the blinking eye: unchanged */
            break;
        case 3:
            JapaneseSubtitle_Type3(this); /* Ezlo's hat: unchanged */
            break;
        default:
            break;
    }
    return TRUE;
}
#endif /* PC_PORT */
