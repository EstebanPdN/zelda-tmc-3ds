#ifndef VRAM_H
#define VRAM_H

#include "global.h"
#include "entity.h"

#ifndef GFX_TYPES_DEFINED
#define GFX_TYPES_DEFINED
#define MAX_GFX_SLOTS 44

typedef enum {
    GFX_SLOT_FREE,
    GFX_SLOT_UNLOADED, // some sort of free? no longer in use?
    GFX_SLOT_STATUS2,  // some sort of free?
    GFX_SLOT_FOLLOWER, // Set by SetGFXSlotStatus for the following slots
    GFX_SLOT_RESERVED, // maybe ready to be loaded?
    GFX_SLOT_GFX,
    GFX_SLOT_PALETTE
} GfxSlotStatus;

typedef enum {
    GFX_VRAM_0,
    GFX_VRAM_1, // uploaded to vram?
    GFX_VRAM_2,
    GFX_VRAM_3, // not yet uploaded to vram?
} GfxSlotVramStatus;

typedef struct {
    /*0x00*/ u8 status : 4;
    /*0x00*/ u8 vramStatus : 4; // Whether the gfx was uploaded to the vram?
    /*0x01*/ u8 slotCount;
    /*0x02*/ u8 referenceCount; /**< How many entities use this gfx slot */
    /*0x03*/ u8 unk_3;
    /*0x04*/ u16 gfxIndex;
    /*0x06*/ u16 paletteIndex;
    /*0x08*/ const void* palettePointer;
} GfxSlot;

typedef struct {
    /*0x00*/ u8 unk0;
    /*0x01*/ u8 unk_1;
    /*0x02*/ u8 unk_2;
    /*0x03*/ u8 unk_3;
    /*0x04*/ GfxSlot slots[MAX_GFX_SLOTS];
} GfxSlotList;
extern GfxSlotList gGFXSlots;

PORT_STATIC_ASSERT_SIZE(GfxSlotList, 0x214, 0x2C8, "GfxSlotList size incorrect");
#endif

extern bool32 LoadFixedGFX(Entity*, u32);
extern bool32 LoadSwapGFX(Entity*, u32, u32);
extern void UnloadGFXSlots(Entity*);
extern void sub_080ADD70(void);

#ifdef PC_PORT
#include "port_gba_mem.h"
#ifndef gBG0Buffer
#define gBG0Buffer (*(u16(*)[0x400]) & gEwram[0x34CB0])
#define gBG1Buffer (*(u16(*)[0x400]) & gEwram[0x21F30])
#define gBG2Buffer (*(u16(*)[0x400]) & gEwram[0x344B0])
#define gBG3Buffer (*(u16(*)[0x800]) & gEwram[0x1A40])
#endif
#else
extern u16 gBG0Buffer[0x400];
extern u16 gBG1Buffer[0x400];
extern u16 gBG2Buffer[0x400];
extern u16 gBG3Buffer[0x800];
#endif

#ifndef OAM_TYPES_DEFINED
#define OAM_TYPES_DEFINED
typedef struct {
    u8 unk0;
    u8 unk1;
    u16 unk2;
    u8 unk4;
    u8 unk5;
    u8 unk6;
    u8 unk7;
} OAMObj;

typedef struct {
    u8 field_0x0;
    u8 field_0x1;
    u8 spritesOffset;
    u8 updated;
    u16 _4;
    u16 _6;
    u8 _0[0x18];
    struct OamData oam[0x80];
    OAMObj unk[0xA0]; /* todo: affine */
} OAMControls;
extern OAMControls gOAMControls;
#endif

#endif // VRAM_H
