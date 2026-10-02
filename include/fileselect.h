#ifndef FILESELECT_H
#define FILESELECT_H

#include "global.h"
#include "save.h"
#include "sound.h"
#include "structures.h"

typedef struct {
    /*0x00*/ u8 unk_0x0;
    /*0x02*/ u8 filler0[0x4];
    /*0x06*/ u8 state;
    /*0x07*/ u8 subState;
    /*0x08*/ u16 timer;
    /*0x0a*/ u8 fillerA[0x5];
    /*0x10*/ s16 unk_0x10;
    /*0x12*/ u16 unk_0x12;
    /*0x14*/ u8 fillerB[0xC];
    /*0x20*/ s8 unk_0x20;
    /*0x21*/ u8 fillerC[0xF];
} ChooseFileState;
static_assert(sizeof(ChooseFileState) == 0x30, "ChooseFileState size incorrect");

#ifdef PC_PORT
extern u8 _gMenuSharedStorage[0x40];
#define gChooseFileState (*(ChooseFileState*)_gMenuSharedStorage)
#else
extern ChooseFileState gChooseFileState;
#endif

// typedef struct {
//     u8 unk00 : 4;
//     u8 unk04 : 4;
//     u8 unk1;
//     u8 charColor;
//     u8 bgColor;
//     u16 unk4;
//     u16 unk6;
//     u8* unk8;
// } struct_02036540;

extern struct_020227E8 gUnk_020227E8[];
static_assert(sizeof(struct_020227E8) == 8, "struct_020227E8 size incorrect");

#ifdef PC_PORT
extern u8 gUnk_020227F0[];
extern u8 gUnk_020227F8[];
extern u8 gUnk_02022800[];

static inline struct_020227E8* GetTextVariableSlot(u32 idx) {
    switch (idx) {
        case 0:
            return &gUnk_020227E8[0];
        case 1:
            return (struct_020227E8*)gUnk_020227F0;
        case 2:
            return (struct_020227E8*)gUnk_020227F8;
        case 3:
            return (struct_020227E8*)gUnk_02022800;
        default:
            return &gUnk_020227E8[0];
    }
}
#else
static inline struct_020227E8* GetTextVariableSlot(u32 idx) {
    return &gUnk_020227E8[idx];
}
#endif

typedef struct {
    /*0x00*/ u8 isTransitioning;
    /*0x01*/ u8 unk1;
    /*0x02*/ u8 unk2;
    /*0x03*/ u8 unk3;
    /*0x04*/ u8 unk4;
    /*0x05*/ u8 unk5;
    /*0x06*/ u8 unk6;
    /*0x07*/ u8 unk7;
    /*0x08*/ s8 saveStatus[3];
    /*0x0b*/ SaveFile saves[3];
} struct_02019EE0;
extern u16 MAY_ALIAS gMapDataBottomSpecial[0x4000];
#define gFileSelectState (*(struct_02019EE0*)gMapDataBottomSpecial)

extern u32 sub_08056FEC(u32, u8*);
extern u32 ShowTextBox(uintptr_t textIndexOrPtr, const Font* font);
extern void ClearTileMaps(void);
extern void ResetSaveFile(u32);
extern WStruct* sub_0805F2C8(void);
extern u32 sub_0805F7DC(u32, WStruct*);
extern void sub_0805F300(WStruct*);
extern void sub_08050A64(u32);
extern void sub_08050AFC(u32);
extern void sub_08050384();
extern void CreateDialogBox(u32, u32);

extern const Font gUnk_080FC844;
extern const u16 gUnk_080FC85C[][3];
extern void (*const gUnk_080FC908[])(void);
extern u8 gTextGfxBuffer[];

#endif // FILESELECT_H
