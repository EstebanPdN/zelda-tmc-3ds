#ifndef UI_H
#define UI_H

#include "global.h"
#include "sprite.h"

#define MAX_UI_ELEMENTS 24

#ifndef UI_TYPES_DEFINED
#define UI_TYPES_DEFINED
typedef enum {
    UI_ELEMENT_BUTTON_A,
    UI_ELEMENT_BUTTON_B,
    UI_ELEMENT_BUTTON_R,
    UI_ELEMENT_ITEM_A,
    UI_ELEMENT_ITEM_B,
    UI_ELEMENT_TEXT_R,
    UI_ELEMENT_HEART,
    UI_ELEMENT_EZLONAGSTART,
    UI_ELEMENT_EZLONAGACTIVE,
    UI_ELEMENT_TEXT_A,
    UI_ELEMENT_TEXT_B
} UIElementType;

/**
 * @brief Floating UI element
 */
typedef struct {
    u8 used : 1;
    u8 unk_0_1 : 1;
    u8 unk_0_2 : 2; // Load data into VRAM? 0: do not load, 1: ready to load 2: loaded
    u8 unk_0_4 : 4;
    u8 type;            /**< @see UIElementType */
    u8 type2;           // Subtype
    u8 buttonElementId; /**< Id of the button UI element this text is attached to */
    u8 action;
    u8 unk_5;
    u8 unk_6;
    u8 unk_7;
    u8 unk_8;
    u8 unk_9[3];
    u16 x;
    u16 y;
    u8 frameIndex;
    u8 duration;
    u8 spriteSettings;
    u8 frameSettings;
    Frame* framePtr;
    u8 unk_18;
    u8 numTiles;
    u16 unk_1a; // TODO oam id? VRAM target (element->unk_1a * 0x20 + 0x6010000)
    u32* firstTile;
} UIElement;

typedef enum {
    HUD_HIDE_NONE,
    HUD_HIDE_1 = 0x1, // A
    HUD_HIDE_2 = 0x2, // B
    HUD_HIDE_4 = 0x4, // R
    HUD_HIDE_8 = 0x8,
    HUD_HIDE_HEARTS = 0x10,
    HUD_HIDE_CHARGE_BAR = 0x20,
    HUD_HIDE_RUPEES = 0x40,
    HUD_HIDE_KEYS = 0x80,

    HUD_HIDE_ALL = 0xff
} HUDHideFlags;

typedef struct {
    u8 unk_0;
    u8 hideFlags;
    u8 unk_2;
    u8 health;
    u8 maxHealth;
    u8 unk_5;
    u8 unk_6;
    u8 unk_7;
    u8 unk_8;
    u8 unk_9;
    u8 unk_a;
    u8 unk_b;
    u8 unk_c;
    u8 unk_d;
    u16 rupees;
    u8 unk_10; // TODO drawing keys dirty flag or something?
    u8 unk_11;
    u8 dungeonKeys;
    s8 unk_13;
    s8 unk_14;
    u8 unk_15;
    u16 buttonX[3]; /**< X coordinates for the button UI elements */
    u16 buttonY[3]; /**< Y coordinates for the button UI elements */
    u8 filler22[0x2];
    u8 ezloNagFuncIndex;
    u8 filler25[7];
    u8 rActionInteractObject; // used as R button UI frame index
    u8 rActionInteractTile;
    u8 rActionGrabbing;
    u8 rActionPlayerState; // if not 0, overrides other R actions
    u8 buttonText[3];
    u8 unk_33;
    UIElement elements[MAX_UI_ELEMENTS];
} HUD;
extern HUD gHUD;
#endif

extern void DrawUIElements(void);
#ifdef PC_PORT
/* DrawUIElements for the gameplay view only — suppressible by the
 * hide_top_hud runtime flag (src/ui.c). Menu screens keep calling
 * DrawUIElements directly so their own prompts never disappear. */
extern void DrawUIElementsGameplay(void);
#endif
extern void UpdateUIElements(void);
extern void CreateUIElement(u32, u32);
extern void sub_0801C2F0(u32, u32);
extern void sub_0801C25C(void);
extern void DrawUI(void);
extern void InitUI(bool32);
extern void RefreshUI(void);
extern void RecoverUI(u32 bottomPt, u32 topPt);

#endif // UI_H
