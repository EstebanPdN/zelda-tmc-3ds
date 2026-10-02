#include "message.h"
#include "asm.h"
#include "common.h"
#include "functions.h"
#include "global.h"
#include "room.h"
#include "save.h"
#include "sound.h"
#include "ui.h"
#ifdef PC_PORT
#include <stdio.h>
#include "../port/port_tts.h"
#endif

#define MESSAGE_ADVANCE_KEYS (A_BUTTON | B_BUTTON | DPAD_ANY | R_BUTTON)
#ifdef PC_PORT
/* Port QoL (default off): when "hold to advance text" is enabled, a HELD
 * advance key keeps pages turning, not just a fresh press. Default off keeps
 * GBA-identical timing (one fresh press per page, important for timed
 * cutscenes). Both message-advance sites read this macro. */
extern bool Port_Config_GetHoldToAdvanceText(void);
#define MESSAGE_PRESS_ANY_ADVANCE_KEYS                 \
    (((gInput.newKeys & MESSAGE_ADVANCE_KEYS) != 0) || \
     (Port_Config_GetHoldToAdvanceText() && (gInput.heldKeys & MESSAGE_ADVANCE_KEYS) != 0))
#else
#define MESSAGE_PRESS_ANY_ADVANCE_KEYS ((gInput.newKeys & MESSAGE_ADVANCE_KEYS) != 0)
#endif

#define MESSAGE_WIDTH 0x20
#define MESSAGE_POSITION_INDEX(window) ((window).yPos * MESSAGE_WIDTH + (window).xPos)

#define MESSAGE_PALETTE 0xF

// todo: move this to a shared header for ui tiles
enum {
    MSG_BORDER_CORNER = 0x7B,
    MSG_BORDER_H_CORNER = 0x7C,
    MSG_BORDER_H_STRAIGHT = 0x7D,
    MSG_BORDER_V_CORNER = 0x7E,
    MSG_BORDER_V_STRAIGHT = 0x7F,
    MSG_CURSOR = 0x80,
    MSG_BACKGROUND = 0x81,
    MSG_TEXT_LINE1TOP = 0x82,
    MSG_TEXT_LINE1BOTTOM = 0x83,
    MSG_TEXT_LINE2TOP = 0xb6,
    MSG_TEXT_LINE2BOTTOM = 0xb7,
};

extern void WriteBit(u32*, u32);
extern bool32 sub_0805EF40(Token* tok, const u8*);
extern void sub_0805F918(u32, u32, void*);
extern u32 DecToHex(u32);

u32 sub_08056FEC(u32, u8*);
u32 GetCharacter(Token* tok);
extern void sub_0805EEB4(Token* tok, u32 textIdx);
extern void sub_0805F8E4(u32 idx, WStruct* data);
u32 sub_0805F7DC(u32, WStruct*);
u32 GetFontStrWith(Token*, u32);

#ifdef PC_PORT
extern u8* gUnk_08107BE0[];

/* Per-page TTS state. The shadow Token walks the same dialog stream
 * as the renderer's gTextRender.curToken but in independent lock-step:
 * we read one page-worth of printable chars (up to the newline that
 * would put the renderer into RENDER_WAIT) and hand that to TTS, then
 * resume at TextDispRoll when the player advances. This way TTS
 * speaks exactly what's visible in the current text window instead
 * of the whole dialog at once. */
static Token sTtsToken;
static u8 sTtsLineNo;   /* mirrors gTextRender._98.bytes.lineNo */
static bool sTtsActive; /* false once we've spoken the final page or dialog ended */

/* Walk sTtsToken forward, collecting printable bytes into a buffer
 * until we hit a page boundary:
 *   - chr == 0 → end of dialog (mark inactive)
 *   - chr == 1 (newline) on line 1 → renderer would enter RENDER_WAIT,
 *     i.e. end of current page; the *next* page restarts on line 0
 *   - chr == 1 on line 0 → just an in-page newline; emit a space
 * Speaks the collected buffer via Port_TTS_Speak (urgent + no dedupe
 * so consecutive identical pages still play and an in-flight previous
 * page is cut off cleanly when the player advances).
 *
 * No-op if TTS is disabled or sTtsActive is false. */
static void PortTTSSpeakCurrentPage(void) {
    char buf[1024];
    size_t pos = 0;
    int iter = 0;

    if (!Port_TTS_GetEnabled() || !sTtsActive)
        return;

    while (pos + 4 < sizeof(buf) && iter++ < 8192) {
        u32 chr = GetCharacter(&sTtsToken);
        if (chr == 0) {
            sTtsActive = false;
            break;
        }
        if (chr == 1) {
            if (sTtsLineNo == 0) {
                sTtsLineNo = 1;
                if (pos > 0 && buf[pos - 1] != ' ')
                    buf[pos++] = ' ';
                continue;
            }
            /* lineNo == 1 → next char would push the renderer into
             * RENDER_WAIT. Stop here, leave Token positioned so the
             * next PortTTSSpeakCurrentPage call picks up where we
             * left off. Reset our shadow lineNo to 0 because the
             * roll-to-next-page clears both lines. */
            sTtsLineNo = 0;
            break;
        }
        /* High byte 1 = "normal printable byte from text stream"
         * (the default case in GetCharacter adds 0x100). Other
         * non-zero high bytes are control wrappers (color, choice,
         * wait, etc.) — skip them. */
        if ((chr >> 8) != 1)
            continue;
        u8 c = (u8)(chr & 0xff);
        if (c >= 0x20 && c < 0x80)
            buf[pos++] = (char)c;
    }

    if (pos == 0)
        return;
    buf[pos] = '\0';

    PortTtsOptions opts = { 0 };
    opts.priority = PORT_TTS_PRIO_URGENT; /* cut off prior page if still speaking */
    opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
    opts.dedupe = false;
    Port_TTS_Speak(buf, &opts);
}

/* Re-init the shadow Token at the start of a dialog (or after a
 * choice jumps to a new text index). Cancels any in-flight speech
 * from a prior dialog and speaks the first page. */
static void PortTTSBeginDialog(u32 textIndex) {
    if (!Port_TTS_GetEnabled())
        return;
    Port_TTS_Stop();
    MemClear(&sTtsToken, sizeof(sTtsToken));
    sub_0805EEB4(&sTtsToken, textIndex);
    /* Same variable-source table the renderer uses — without this,
     * GetCharacter's case-6 substitution falls back to global slots
     * and player_name / item_name reads come out empty. */
    sTtsToken._c = (void*)&gUnk_08107BE0;
    sTtsLineNo = 0;
    sTtsActive = true;
    PortTTSSpeakCurrentPage();
}

/* Speak a text index as a single utterance — the whole text walked
 * once, no pagination. Used for menu focus labels (pause menu item
 * names, descriptions, popup hints) which are typically a single
 * short string. URGENT so the previous focus's announcement is cut
 * off when the cursor moves; dedupe so identical labels that re-fire
 * within the dedupe window don't spam. */
void Port_TTS_SpeakTextIndex(u32 textIndex) {
    Token tok;
    char buf[1024];
    size_t pos = 0;
    int iter = 0;

    if (!Port_TTS_GetEnabled())
        return;

    MemClear(&tok, sizeof(tok));
    sub_0805EEB4(&tok, textIndex);
    tok._c = (void*)&gUnk_08107BE0;

    while (pos + 4 < sizeof(buf) && iter++ < 8192) {
        u32 chr = GetCharacter(&tok);
        if (chr == 0)
            break;
        if (chr == 1) {
            if (pos > 0 && buf[pos - 1] != ' ')
                buf[pos++] = ' ';
            continue;
        }
        if ((chr >> 8) != 1)
            continue;
        u8 c = (u8)(chr & 0xff);
        if (c >= 0x20 && c < 0x80)
            buf[pos++] = (char)c;
    }

    if (pos == 0)
        return;
    buf[pos] = '\0';

    PortTtsOptions opts = { 0 };
    opts.priority = PORT_TTS_PRIO_URGENT;
    opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
    opts.dedupe = true;
    Port_TTS_Speak(buf, &opts);
}
#endif

static void StatusUpdate(u32 status);

/*static*/ u16 RunTextCommand(TextRender* this);
/*static*/ void PaletteChange(TextRender* this, u32 id);
static void SwitchChoice(u32 to, u32 from);

static void MsgChangeLine(u32 lineNo);
static void SetState(u32 status);

static void DeleteWindow(void);
static u32 ChangeWindowSize(u32 delta);
static void CreateWindow(void);

static void DispString(void);
static void DispCursor(void);
static void DrawCanvasLine(void);
static void sub_08056F88(u32, u32);
static void sub_08056FBC(TextRender*);

typedef u32 (*MessageFunction)(void);
static u32 MsgIdle(void);
/*static*/ u32 MsgInit(void);
static u32 MsgUpdate(void);
/*static*/ u32 MsgOpen(void);
static u32 MsgClose(void);
static u32 MsgDie(void);
typedef enum {
    MSG_IDLE,
    MSG_INIT,
    MSG_UPDATE,
    MSG_OPEN,
    MSG_CLOSE,
    MSG_DIE,
} MessageStatus;

typedef void (*TextRenderFunction)(TextRender*);
static void TextDispInit(TextRender* this);
static void TextDispUpdate(TextRender* this);
static void TextDispDie(TextRender* this);
static void TextDispWait(TextRender* this);
static void TextDispRoll(TextRender* this);
/*static*/ void TextDispEnquiry(TextRender* this);
typedef enum {
    RENDER_INIT,
    RENDER_UPDATE,
    RENDER_DIE,
    RENDER_WAIT,
    RENDER_ROLL,
    RENDER_ENQUIRY,
} TextRenderStatus;

typedef struct Window {
    u8 unk0;
    u8 active;
    u8 unk2;
    u8 unk3;
    u8 xPos;
    u8 yPos;
    u8 width;
    u8 height;
} Window;
#ifdef PC_PORT
Window gCurrentWindow;
Window gNewWindow;
#else
extern Window gCurrentWindow;
extern Window gNewWindow;
#endif

typedef struct {
    u8 unk_00;
    u8 unk_01[1];
    s8 choiceCount;
    s8 currentChoice;
    u8 unk_04[4];
    u16 unk_08[4];
    u16 unk_10[4];
} MessageChoices;
#ifdef PC_PORT
MessageChoices gMessageChoices;
#else
extern MessageChoices gMessageChoices;
#endif

extern u8 gTextGfxBuffer[0xD00];

s32 sub_08056338(void) {
    s32 result;

    result = -1;
    if (((gMessage.state & MESSAGE_ACTIVE) == 0) && (gUnk_02000040.unk_00 == 3))
        result = gUnk_02000040.unk_01;
    return result;
}

void MessageClose(void) {
    if (gMessage.state & MESSAGE_ACTIVE) {
        gMessage.state = 0x88;
    }
}

void MessageFromTarget(u32 index) {
    if (gRoomControls.camera_target != NULL) {
        MessageNoOverlap(index, gRoomControls.camera_target);
    } else {
        MessageRequest(index);
    }
}

void MessageNoOverlap(u32 index, Entity* entity) {
    s16 y;
    s16 height;

    MessageRequest(index);

    y = entity->y.HALF.HI;
    height = entity->z.HALF.HI;

    if (((y + height) - gRoomControls.scroll_y) > 0x58) {
        gMessage.textWindowPosY = 1;
    }
}

void MessageAtHeight(u32 index, u32 y) {
    MessageAtPos(index, 1, y);
}

void MessageAtPos(u32 index, u32 x, u32 y) {
    MessageRequest(index);
    gMessage.textWindowPosX = x;
    gMessage.textWindowPosY = y;
}

void MessageRequest(u32 index) {
    MemClear(&gMessage, sizeof(gMessage));
    gMessage.textIndex = index;
    gMessage.textSpeed = 99;
    gMessage.textWindowWidth = 26;
    gMessage.textWindowHeight = 4;
    gMessage.textWindowPosX = 1;
    gMessage.textWindowPosY = 12;
    gMessage.state = 1;
}

void MessageInitialize(void) {
    MemClear(&gMessage, sizeof(gMessage));
    MemClear(&gTextRender, sizeof(gTextRender));
    MemClear(&gNewWindow, sizeof(gNewWindow));
    MemClear(&gCurrentWindow, sizeof(gCurrentWindow));
    MemClear(&gUnk_02000040, 4);
}

void MessageMain(void) {
    static const MessageFunction gMessageFunctions[] = {
        [MSG_IDLE] = MsgIdle, [MSG_INIT] = MsgInit,   [MSG_UPDATE] = MsgUpdate,
        [MSG_OPEN] = MsgOpen, [MSG_CLOSE] = MsgClose, [MSG_DIE] = MsgDie,
    };

    if (gMessage.state == 1) {
        MemClear((u32*)&gTextRender, sizeof(gTextRender));
        StatusUpdate(MSG_INIT);
    }

    if (gTextRender.newlineDelay != 0) {
        gTextRender.newlineDelay--;
    } else {
        int result;
        do {
            result = gMessageFunctions[gTextRender.msgStatus]();
        } while (result != 0);
    }
    if (gTextRender.updateDraw) {
        gTextRender.updateDraw = 0;
        DrawCanvasLine();
    }
    DeleteWindow();
    CreateWindow();
}

static void StatusUpdate(u32 status) {
    gTextRender.msgStatus = status;
    gTextRender.renderStatus = RENDER_INIT;
}

static u32 MsgIdle(void) {
    gTextRender._98.bytes.b1 = 0;
    return 0;
}

#ifdef PC_PORT
/* GBA-original gTextRender lived at EWRAM 0x02022780, so the variable-
 * substitution buffers gUnk_020227E8/F0/F8/0x02022800 happened to alias
 * the rupees / field_0x14 / field_0x18 / field_0x1c slots inside
 * gTextRender's _66/_77 fields (populated by sub_08056FBC). On the
 * port, gTextRender is heap-allocated, so those raw GBA addresses
 * resolve to unrelated gEwram[] bytes that nothing ever writes —
 * "You got X items!" textboxes printed with no number visible (or
 * garbage with the broken-DecToHex bug). Make the buf-source array
 * point at the actual fields inside gTextRender. */
u8* gUnk_08107BE0[] = {
    NULL, /* player_name — set at runtime, see SyncTextVariableSlots */
    NULL, /* rupees      */
    NULL, /* field_0x14  */
    NULL, /* field_0x18  */
    NULL, /* field_0x1c  */
};

static void SyncTextVariableSlots(void) {
    if (gUnk_08107BE0[0] != NULL) {
        return; /* already initialized */
    }
    gUnk_08107BE0[0] = ((u8*)&gTextRender) + offsetof(TextRender, player_name);
    gUnk_08107BE0[1] = ((u8*)&gTextRender) + offsetof(TextRender, _66) + 0x02;
    gUnk_08107BE0[2] = ((u8*)&gTextRender) + offsetof(TextRender, _66) + 0x0a;
    gUnk_08107BE0[3] = ((u8*)&gTextRender) + offsetof(TextRender, _77) + 0x01;
    gUnk_08107BE0[4] = ((u8*)&gTextRender) + offsetof(TextRender, _77) + 0x09;
}
#else
extern u8 gUnk_020227DC, gUnk_020227E8, gUnk_020227F0, gUnk_020227F8, gUnk_02022800;
u8* const gUnk_08107BE0[] = {
    &gUnk_020227DC, &gUnk_020227E8, &gUnk_020227F0, &gUnk_020227F8, &gUnk_02022800,
};
#endif

u32 MsgInit(void) {
    char* dest;
    u32 i;

#ifdef PC_PORT
    SyncTextVariableSlots();
#endif
    MemClear((void*)&gNewWindow, sizeof(gNewWindow));
    MemClear((void*)&gMessageChoices, sizeof(gMessageChoices));
    MemClear((void*)&gTextRender, sizeof(gTextRender));
    MemCopy(&gMessage, &gTextRender, sizeof(gMessage));
    if (gTextRender.message.textSpeed == 99) {
        gTextRender.message.textSpeed = gSaveHeader->msg_speed;
    }
    gTextRender._9c = 0xff;
    sub_0805EEB4(&gTextRender.curToken, gTextRender.message.textIndex);

    dest = &gTextRender.player_name[0];
    dest[0] = 2;
    dest[1] = 0xe; // Green text color
    dest += 2;
    for (i = 0; i < FILENAME_LENGTH; ++i) {
        char c = gSave.name[i];
        if (c == '\0')
            break;
        *dest++ = c;
    }

    dest[0] = 2;
    dest[1] = 0xf; // White text color
    dest[2] = '\0';
    sub_08056FBC(&gTextRender);
    gTextRender.curToken._c = &gUnk_08107BE0;
    gTextRender._50.unk8 = gTextGfxBuffer;
    gTextRender._50.unk4 = 0xd0;
#ifdef PC_PORT
    /* Announce the dialog text to a screen reader / TTS backend.
     * Uses a separate shadow Token so the renderer's gTextRender.curToken
     * walk is unaffected. Only speaks the current page; TextDispRoll
     * resumes for the next page when the player advances. */
    PortTTSBeginDialog(gTextRender.message.textIndex);
#endif
    SetState(2);
    MsgChangeLine(0);
    StatusUpdate(MSG_UPDATE);
    return 1;
}

/*static*/ u32 MsgOpen(void) {
    if (gTextRender.renderStatus == RENDER_INIT) {
        gTextRender.renderStatus = RENDER_UPDATE;
        gTextRender._98.bytes.b1 = 1;
        sub_08056F88(gTextRender.message.unk3, gTextRender._50.bgColor);
        SoundReq(SFX_TEXTBOX_OPEN);
    }

    if (ChangeWindowSize(1)) {
        gTextRender._98.bytes.b1 = 2;
        StatusUpdate(MSG_UPDATE);
    }
    return 0;
}

static u32 MsgClose(void) {
    if (gTextRender.renderStatus == RENDER_INIT) {
        gTextRender.renderStatus = RENDER_UPDATE;
        gTextRender._98.bytes.b1 = 3;
        MsgChangeLine(0);
        SoundReq(SFX_TEXTBOX_CLOSE);
    }

    if (ChangeWindowSize(-1)) {
        gTextRender._98.bytes.b1 = 0;
        StatusUpdate(MSG_UPDATE);
    }
    return 0;
}

static u32 MsgDie(void) {
    SetState(0);
    StatusUpdate(MSG_IDLE);
    return 0;
}

static u32 MsgUpdate(void) {
    static const TextRenderFunction gTextDispFunctions[] = {
        [RENDER_INIT] = TextDispInit, [RENDER_UPDATE] = TextDispUpdate, [RENDER_DIE] = TextDispDie,
        [RENDER_WAIT] = TextDispWait, [RENDER_ROLL] = TextDispRoll,     [RENDER_ENQUIRY] = TextDispEnquiry,
    };

    SetState(4);
    gTextDispFunctions[gTextRender.renderStatus](&gTextRender);
    ChangeWindowSize(0);
    return 0;
}

static void TextDispInit(TextRender* this) {
    if (gTextRender.curToken.unk00 == 0) {
        if (gTextRender._98.bytes.b1 == 0) {
            StatusUpdate(MSG_DIE);
        }
    } else {
        this->renderStatus = RENDER_UPDATE;
    }
}

static void TextDispUpdate(TextRender* this) {
    static const u8 speeds[] = { 5, 3, 1 };
    u32 speedModifier;
    s32 numCharsToRead, pxDrawn;

    if (this->delay != 0) {
        this->delay--;
        return;
    }

    if ((gInput.heldKeys & B_BUTTON) != 0) {
        speedModifier = 8;
    } else {
        speedModifier = 1;
    }
    this->typeSpeed -= speedModifier;

    if (this->typeSpeed > 0) {
        return;
    }

    numCharsToRead = 0;
    do {
        numCharsToRead++;
        this->typeSpeed += speeds[this->message.textSpeed];
    } while (this->typeSpeed <= 0);

    pxDrawn = 0;
    do {
        u32 pxCnt = RunTextCommand(this);
        if (pxCnt == 0 || this->delay != 0 || this->newlineDelay != 0)
            break;
        pxDrawn += pxCnt;
        numCharsToRead--;
    } while (0 < numCharsToRead);

    if (pxDrawn != 0) {
        gTextRender.updateDraw = 1;
    } else {
        this->typeSpeed = 0;
    }
}

u16 RunTextCommand(TextRender* this) {
    u32 chr = this->curToken.extended;
    s32 temp;
    u32 tmp2;
    u32 tmp3;
    u32* ptr;

    if (chr == 0) {
        s32 r1;
        chr = GetCharacter(&this->curToken);
        switch (chr) {
            case 0:
                if (gUnk_02000040.unk_00 == 1) {
                    this->renderStatus = RENDER_ENQUIRY;
                    if (!REGION_IS_EU) {
                        SwitchChoice(0, 0);
                    }
                } else {
                    this->renderStatus = RENDER_DIE;
                }
                break;
            case 1:
                this->newlineDelay = 2;
                if (this->_98.bytes.lineNo == 0) {
                    MsgChangeLine(1);
                } else {
                    this->renderStatus = RENDER_WAIT;
                }
                break;
            case 2:
                StatusUpdate(MSG_OPEN);
                break;
            case 3:
                StatusUpdate(MSG_CLOSE);
                break;
            case 4:
                temp = GetFontStrWith(&this->curToken, 0);
                this->_50.unk6 = this->_50.unk6 + (this->_50.unk4 - this->_50.unk6 - temp) / 2;
                break;
            case 5:
                gMessage.unk = this->curToken.param;
                break;
            case 6:
                ptr = &gMessage.flags;
                tmp3 = this->curToken.param;
                tmp3 &= 0x1f;
                WriteBit(ptr, tmp3);
                break;
            case 7:
                temp = this->curToken.param;
                switch (temp) {
                    case 0xe:
                        this->_91 = this->_8f;
                        temp = 2;
                        break;
                    case 0xf:
                        temp = this->_91;
                        break;
                }
                this->_90 = 0;
                PaletteChange(this, temp);
                break;
            case 8:
                SoundReq(this->curToken.param);
                break;
            case 9:
                gTextRender.message.unk3 = this->curToken.param;
                sub_08056F88(this->curToken.param, this->_50.bgColor);
                break;
            case 10:
                this->message.textWindowPosY = this->curToken.param;
                break;
            case 12:
                if (gUnk_02000040.unk_00 != 1) {
                    MemClear(&gUnk_02000040, 4);
                    MemClear(&gMessageChoices, sizeof(gMessageChoices));
                    gMessageChoices.unk_00 = 1;
                    gUnk_02000040.unk_00 = 1;
                }
                if (gMessageChoices.choiceCount > 3)
                    break;

                gMessageChoices.unk_10[gMessageChoices.choiceCount] = this->curToken._6;
                gMessageChoices.unk_08[gMessageChoices.choiceCount] = this->_50.unk6;
                gMessageChoices.choiceCount++;
                this->_50.unk6 += 8;
                break;
            case 13:
                temp = this->curToken.param;
                switch (temp) {
                    case 0xff:
                        this->_8e = 1;
                        break;
                    case 0xfe:
                        this->_8e = 2;
                        break;
                    default:
                        this->delay = temp;
                        break;
                }
                break;
            case 14:
                this->_94 = this->curToken.param;
                break;
            default:
                break;
        }
        if (chr >> 8 == 0)
            return 0;
    }
    if (gTextRender._98.bytes.b1 != 2) {
        StatusUpdate(MSG_OPEN);
        return 0;
    }
    if (gMessage.unk == 0) {
        gMessage.unk = 0x80;
    }
    this->curToken.extended = 0;
    if (chr >> 8 == 7) {
        this->_90 = this->_8f | 0x80;
        PaletteChange(this, 0);
    } else {
        if ((this->_90 & 0x80) != 0) {
            temp = this->_90;
            this->_90 = 0;
            PaletteChange(this, temp);
        }
    }
    return sub_0805F7DC(chr, &this->_50);
}

/*static*/ void PaletteChange(TextRender* this, u32 id) {
    u32 temp = id % 8;
    this->_8f = temp;
    this->_50.charColor = temp;
}

const u8 gUnk_08107C0F[] = { 0x8, 0x1e, 0x4, 0x12, 0x0 };
const u8 gUnk_08107C14[] = { 0x8, 0x1e, 0x8, 0xFE, 0x0 };

void TextDispEnquiry(TextRender* this) {
    s32 nextTextIdx, choiceIdx, lastChoice;
    u32 doSwitch;
    const u8* src;

    choiceIdx = gMessageChoices.currentChoice;
    switch (gInput.newKeys) {
        case START_BUTTON:
        case A_BUTTON:
            nextTextIdx = gMessageChoices.unk_10[choiceIdx];
            if (nextTextIdx == 0) {
                src = gUnk_08107C14;
            } else {
                this->message.textIndex = nextTextIdx;
                sub_0805EEB4(&this->curToken, nextTextIdx);
#ifdef PC_PORT
                PortTTSBeginDialog(nextTextIdx);
#endif
                src = gUnk_08107C0F;
            }
            sub_0805EF40(&this->curToken, src);
            gUnk_02000040.unk_01 = gMessageChoices.currentChoice;
            gUnk_02000040.unk_00 = 3;
            MemClear(&gMessageChoices, sizeof(gMessageChoices));
            SoundReq(SFX_TEXTBOX_SELECT);
            this->renderStatus = RENDER_UPDATE;
            return;
        case DPAD_LEFT:
            choiceIdx--;
            break;
        case DPAD_RIGHT:
            choiceIdx++;
            break;
        default:
            break;
    }
    /* After A_BUTTON, MemClear zeros gMessageChoices.choiceCount and
     * renderStatus advances. The remaining bookkeeping is moot once
     * choices are gone; the % below would trap SIGFPE on x86 with
     * choiceCount == 0 (ARM silently returns garbage). (#16) */
    if (gMessageChoices.choiceCount <= 0) {
        return;
    }
    choiceIdx = (choiceIdx + gMessageChoices.choiceCount) % gMessageChoices.choiceCount;
    lastChoice = gMessageChoices.currentChoice;
    if (choiceIdx != lastChoice) {
        gMessageChoices.currentChoice = choiceIdx;
        SoundReq(SFX_TEXTBOX_CHOICE);
        doSwitch = 1;
    } else {
        doSwitch = 0;
    }
    if (gUnk_02000040.unk_00 == 1) {
        gMessageChoices.unk_00 = gUnk_02000040.unk_00 = 2;
        doSwitch = 1;
    }
    if (doSwitch) {
        if (REGION_IS_EU) {
            u32 previousUnk6 = gTextRender._50.unk6;
            gTextRender._50.unk6 = gMessageChoices.unk_08[lastChoice];
            sub_0805F8E4(0, &this->_50);
            gTextRender._50.unk6 = gMessageChoices.unk_08[choiceIdx];
            sub_0805F8E4(1, &this->_50);
            gTextRender._50.unk6 = previousUnk6;
            gTextRender.updateDraw = 1;
        } else {
            SwitchChoice(choiceIdx, lastChoice);
        }
    }
}

static void SwitchChoice(u32 to, u32 from) {
    u16 t;
    t = gTextRender._50.unk6;
    gTextRender._50.unk6 = gMessageChoices.unk_08[from];
    sub_0805F8E4(0, &gTextRender._50);
    gTextRender._50.unk6 = gMessageChoices.unk_08[to];
    sub_0805F8E4(1, &gTextRender._50);
    gTextRender._50.unk6 = t;
    gTextRender.updateDraw = 1;
}

static void TextDispWait(TextRender* this) {
    gMessage.unk = 0;
    if (this->_94 != 0) {
        if (--this->_94 == 0) {
            this->renderStatus = RENDER_ROLL;
        }
    } else if (MESSAGE_PRESS_ANY_ADVANCE_KEYS) {
        SoundReq(SFX_TEXTBOX_SWAP);
        this->_98.bytes.b2 = 0;
        this->renderStatus = RENDER_ROLL;
    } else {
        this->_98.bytes.b2++;
    }
}

static void TextDispRoll(TextRender* this) {
    MsgChangeLine(0);
    PaletteChange(this, this->_8f | 0x40);
    this->renderStatus = RENDER_UPDATE;
#ifdef PC_PORT
    /* Player just advanced past the previous page (RENDER_WAIT →
     * RENDER_ROLL). Speak the next page from the shadow Token. */
    PortTTSSpeakCurrentPage();
#endif
}

static void TextDispDie(TextRender* this) {
    gMessage.unk = 0;
    SetState(7);
    if ((this->_8e != 1) && (this->_8e == 2 || MESSAGE_PRESS_ANY_ADVANCE_KEYS)) {
        StatusUpdate(MSG_CLOSE);
    }
}

static void MsgChangeLine(u32 lineNo) {
    gTextRender._98.bytes.lineNo = lineNo;
    if (lineNo == 0) {
        MemFill32(0xFFFFFFFF, gTextGfxBuffer, sizeof(gTextGfxBuffer));
        DrawCanvasLine();
        gTextRender.beginTiles[0] = (MESSAGE_PALETTE << 12) | MSG_TEXT_LINE1TOP;
        gTextRender.beginTiles[1] = (MESSAGE_PALETTE << 12) | MSG_TEXT_LINE1BOTTOM;
        gTextRender.beginTiles[2] = (MESSAGE_PALETTE << 12) | MSG_TEXT_LINE2TOP;
        gTextRender.beginTiles[3] = (MESSAGE_PALETTE << 12) | MSG_TEXT_LINE2BOTTOM;
        gTextRender._50.unk6 = 0;
        gTextRender._50.unk4 = 0xd0;
    } else {
        gTextRender._50.unk6 = 0xd0;
        gTextRender._50.unk4 = 0x1a0;
    }
    PaletteChange(&gTextRender, gTextRender._8f | 0x40);
}

static void SetState(u32 status) {
    gMessage.state = gTextRender.message.state = status;
}

static void DeleteWindow(void) {
    Window* window = &gCurrentWindow;
    if (window->active) {
        u16* ptr;
        int i;
        window->active = 0;
        ptr = &gBG0Buffer[MESSAGE_POSITION_INDEX(*window)];
        i = window->height + 2;
        do {
            int j = 0;
            do {
                ptr[j] = 0;
            } while (j++, j < window->width + 2);
            ptr += MESSAGE_WIDTH;
            i--;
        } while (i > 0);
        RecoverUI(window->yPos, window->height);
        RefreshUI();
    }
}

static u32 ChangeWindowSize(u32 delta) {
    Window* window;
    u32 result = 0;

    gTextRender._98.bytes.sizeScale += delta;

    if (gTextRender._98.bytes.sizeScale < 1) {
        gTextRender._98.bytes.sizeScale = 0;
        result = 1;
    } else if (gTextRender._98.bytes.sizeScale >= 8) {
        gTextRender._98.bytes.sizeScale = 8;
        result = 1;
    }

    window = &gNewWindow;
    if (gTextRender._98.bytes.sizeScale != 0) {
        u32 scale = gTextRender._98.bytes.sizeScale;
        window->width = (scale * (gTextRender.message.textWindowWidth << 1)) / 16;
        if ((window->width & 1) != 0) {
            window->width++;
        }
        window->xPos =
            ((gTextRender.message.textWindowWidth / 2) + gTextRender.message.textWindowPosX) - (window->width / 2);

        window->height = (scale * (gTextRender.message.textWindowHeight << 1)) / 16;
        if ((window->height & 1) != 0) {
            window->height++;
        }
        window->yPos =
            ((gTextRender.message.textWindowHeight / 2) + gTextRender.message.textWindowPosY) - (window->height / 2);
    } else {
        window->yPos = -1;
        window->xPos = -1;
        window->height = -1;
        window->width = -1;
    }
    window->active = 1;
    return result;
}

static void CreateWindow(void) {
    s32 borderBeginTile;
    u16* ptr;

    if (gTextRender._98.bytes.sizeScale <= 0)
        return;
    ptr = &gBG0Buffer[MESSAGE_POSITION_INDEX(gNewWindow)];
    borderBeginTile = (MESSAGE_PALETTE << 12) | MSG_BORDER_CORNER;
    DispMessageFrame(ptr, gNewWindow.width, gNewWindow.height, borderBeginTile);
    DispString();
    DispCursor();
    gCurrentWindow = gNewWindow;
    RefreshUI();
}

// Also used by figurine menu
void DispMessageFrame(u16* buffer, s32 width, s32 height, u32 flags) {
    u16* ptr = buffer;
    u16* ptr2 = &buffer[(height << 5) + 0x20];
    u32 flags1;
    u32 i;
    *ptr++ = flags;
    *ptr2++ = flags | 0x800;
    if (width > 0) {
        *ptr++ = flags + 1;
        *ptr2++ = (flags + 1) | 0x800;
        width -= 2;
        while (width-- > 0) {
            *ptr++ = flags + 2;
            *ptr2++ = (flags + 2) | 0x800;
        }

        *ptr++ = (flags + 1) | (0x80 << 3);
        *ptr2++ = (flags + 1) | (0xc0 << 4);
    }

    *ptr = flags | 0x400;
    *ptr2 = flags | 0xc00;
    buffer += 0x20;
    ptr += 0x20;

    if (height > 0) {
        *buffer = flags + 3;
        *ptr = (flags + 3) | 0x400;
        buffer += 0x20;
        ptr += 0x20;

        height -= 2;
        while (height-- > 0) {
            *buffer = flags + 4;
            *ptr = (flags + 4) | 0x400;
            buffer += 0x20;
            ptr += 0x20;
        }

        *buffer = (flags + 3) | 0x800;
        *ptr++ = (flags + 3) | 0xc00;
    }
}

/*
    Each character is two tiles tall.
    They are stored in VRAM in the following order:

    82 84 86 --> |  | |\| |/
    83 85 87 --> |_ | | \ |\

    b6 b8 ba -->   | | |
    b7 b9 bb -->   . . .

    There are 2 lines of text available, and each line is 20 chars long,
    So the canvas is 20x4 tiles.
*/

static void DispString(void) {
    u32 r0, tileAttr;
    u16 *buf, *line;
    s32 i, w, h;

    Window* window = &gNewWindow;

    if (window->width != 0) {
        if (window->height != 0) {
            h = window->height;
            buf = &gBG0Buffer[MESSAGE_POSITION_INDEX(*window) + 1];
            i = (4 - h) / 2;
            do {
                //! @bug abnormally big width causes tile idx to overwrite other tile attributes
                w = window->width;
                tileAttr = gTextRender.beginTiles[i];
                buf += MESSAGE_WIDTH;
                r0 = i + 1;
                h--;
                line = buf;
                do {
                    *line = tileAttr;
                    line++;
                    tileAttr += 2;
                    w--;
                } while (w > 0);
                i = r0;
            } while (h != 0);
        }
    }
}

static void DispCursor(void) {
    u32 offset;
    u16* ptr;

    if ((gTextRender._98.word & 0x10ff00) == 0x100200) {
        ptr = &gBG0Buffer[MESSAGE_POSITION_INDEX(gNewWindow)];
        offset = (((gNewWindow.height + 1) * MESSAGE_WIDTH) - 2);
        offset += gNewWindow.width;
        ptr += offset;
        *ptr = 0xf080;
    }
}

static void DrawCanvasLine(void) {
    LoadResourceAsync(gTextGfxBuffer, (void*)(uintptr_t)0x0600D040, sizeof(gTextGfxBuffer));
}

void sub_08056F88(u32 unk_1, u32 unk_2) {
    u32 uVar1;

    if (0xf < unk_1) {
        unk_1 = 0;
    }
    uVar1 = unk_1 << 4 | unk_2;
    if (gTextRender._9c != uVar1) {
        gTextRender._9c = uVar1;
        sub_0805F918(unk_1, unk_2, (void*)0x0600CF60);
    }
}

static void sub_08056FBC(TextRender* ctb) {
    sub_08056FEC(ctb->message.rupees, &ctb->_66[0x2]);
    sub_08056FEC(ctb->message.field_0x14, &ctb->_66[0xa]);
    sub_08056FEC(ctb->message.field_0x18, &ctb->_77[0x1]);
    sub_08056FEC(ctb->message.field_0x1c, &ctb->_77[0x9]);
}

u32 sub_08056FEC(u32 this, u8* param_2) {
    u32 uVar1;
    int iVar2;
    int iVar3;
    int iVar4;
    u8 local_1c[8];

    uVar1 = DecToHex(this);
    uVar1 = uVar1 & 0xfffffff;
    iVar4 = 0;
    do {
        local_1c[iVar4++] = uVar1 & 0xf;
        uVar1 = uVar1 / 16;
    } while (uVar1 != 0);
    for (iVar3 = 0, iVar2 = iVar4 - 1; iVar2 >= 0; iVar3++, iVar2--) {
        param_2[iVar3] = local_1c[iVar2] | 0x30;
    }
    param_2[iVar3] = 0;
    return iVar4;
}

void sub_08057044(u32 a1, struct_020227E8* a2, u32 a3) {
    u32 z[2];
    u32 tmp = sub_08056FEC(a1, (u8*)z);
    u32 first = z[0];
    first <<= 8 * (3 - tmp);
    a2->_0.WORD = first | a3;
    a2->_4.WORD = 0;
}
