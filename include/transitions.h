#ifndef TRANSITIONS_H
#define TRANSITIONS_H

#include "global.h"
#include "roomid.h"

typedef enum {
    WARP_TYPE_BORDER,
    WARP_TYPE_AREA,
    WARP_TYPE_BORDER2,
    WARP_TYPE_AREA2,
    WARP_TYPE_END_OF_LIST = 0xffff,
} WarpType;

typedef enum {
    TRANSITION_TYPE_NORMAL,
    TRANSITION_TYPE_INSTANT_MINISH,
    TRANSITION_TYPE_DROP_IN,
    TRANSITION_TYPE_INSTANT,
    TRANSITION_TYPE_STEP_FORWARD,
    TRANSITION_TYPE_5,
    TRANSITION_TYPE_DROP_IN_MINISH,
    TRANSITION_TYPE_STAIR_EXIT,
    TRANSITION_TYPE_8,
    TRANSITION_TYPE_9,
    TRANSITION_TYPE_HOP_IN_FORWARD,
    TRANSITION_TYPE_HOP_IN,
    TRANSITION_TYPE_FLY_IN,
} TransitionType;

typedef enum {
    TRANSITION_SHAPE_AREA_12x12,
    TRANSITION_SHAPE_AREA_12x28,
    TRANSITION_SHAPE_AREA_28x12,
    TRANSITION_SHAPE_AREA_44x12,

    TRANSITION_SHAPE_BORDER_NORTH_WEST = 0x01,
    TRANSITION_SHAPE_BORDER_NORTH_EAST = 0x02,
    TRANSITION_SHAPE_BORDER_NORTH = 0x03,
    TRANSITION_SHAPE_BORDER_EAST_NORTH = 0x04,
    TRANSITION_SHAPE_BORDER_EAST_SOUTH = 0x08,
    TRANSITION_SHAPE_BORDER_EAST = 0x0c,
    TRANSITION_SHAPE_BORDER_SOUTH_WEST = 0x10,
    TRANSITION_SHAPE_BORDER_SOUTH_EAST = 0x20,
    TRANSITION_SHAPE_BORDER_SOUTH = 0x30,
    TRANSITION_SHAPE_BORDER_WEST_NORTH = 0x40,
    TRANSITION_SHAPE_BORDER_WEST_SOUTH = 0x80,
    TRANSITION_SHAPE_BORDER_WEST = 0xc0,
} TransitionShape;

typedef struct Transition {
    u16 warp_type; /**< @see WarpType */
    u16 startX;
    u16 startY;
    u16 endX;
    u16 endY;
    u8 shape;
    u8 area;
#ifdef PC_PORT
    u8 room;
    u8 layer;
    u8 transition_type;
    u8 facing_direction; // 0-4
#else
    RoomID room : 8;
    u8 layer;
    TransitionType transition_type : 8;
    u8 facing_direction; // 0-4
#endif
    u16 transitionSFX;
    u8 unk2;
    u8 unk3;
} Transition;
static_assert(sizeof(Transition) == 20, "Transition size incorrect");

extern const Transition gExitList_RoyalValley_ForestMaze[];
extern const Transition gUnk_08134FBC[];
extern const Transition gUnk_08135048[];
extern const Transition gUnk_08135190[];
extern const Transition gUnk_0813A76C[];
extern const Transition* const* const gExitLists[];

#endif // TRANSITIONS_H
