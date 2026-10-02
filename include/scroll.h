#ifndef SCROLL_H
#define SCROLL_H

#include "global.h"
#include "transitions.h"

#ifndef STRUCT_02034480_DEFINED
#define STRUCT_02034480_DEFINED
typedef struct {
    u16 unk_00;
    u8 unk_02[0xE];
} struct_02034480;
extern struct_02034480 gUnk_02034480;
#endif

extern bool32 DoApplicableTransition(u32, u32, u32, u32);
extern void DoExitTransitionWithType(const Transition* screenTransition, u32 transitionType);

static inline s16 DecodePreservedTransitionAxis(u16 value, u16 origin) {
    if ((value & 0x8000u) != 0) {
        return (s16)((value & 0x7fffu) - origin);
    }
    return (s16)value;
}

/*
 * A room transition may only be selected from coordinates that belong to the
 * current room.  Expressing both lower- and upper-bound checks as one unsigned
 * comparison also rejects coordinates before the origin without underflowing
 * into the opposite edge.
 */
static inline bool32 IsPointInsideRoomBounds(s32 x, s32 y, s32 originX, s32 originY, u32 width, u32 height) {
    return ((u32)x - (u32)originX) < width && ((u32)y - (u32)originY) < height;
}

void UpdateIsDiggingCave(void);
void sub_08080930(u32);

extern void sub_080809D4(void);
extern void sub_08080CB4(struct Entity_*);

#endif // SCROLL_H
