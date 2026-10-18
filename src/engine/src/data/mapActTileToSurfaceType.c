#include "asm.h"
#include "player.h"
#include "tiles.h"

const KeyValuePair gMapActTileToSurfaceType[] = {
    { ACT_TILE_13, SURFACE_PIT },
    { ACT_TILE_38, SURFACE_SLOPE_GNDGND_V },
    { ACT_TILE_39, SURFACE_SLOPE_GNDGND_H },
    { ACT_TILE_82, SURFACE_26 },
    { ACT_TILE_99, SURFACE_24 },
    { ACT_TILE_116, SURFACE_EDGE },
    { ACT_TILE_8, SURFACE_7 },
    { ACT_TILE_31, SURFACE_MINISH_DOOR_FRONT },
    { ACT_TILE_32, SURFACE_MINISH_DOOR_BACK },
    { ACT_TILE_33, SURFACE_A },
    { ACT_TILE_34, SURFACE_B },
    { ACT_TILE_24, SURFACE_16 },
    { ACT_TILE_18, SURFACE_ICE },
    { ACT_TILE_15, SURFACE_SHALLOW_WATER },
    { ACT_TILE_14, SURFACE_SLOPE_GNDWATER },
    { ACT_TILE_16, SURFACE_WATER },
    { ACT_TILE_27, SURFACE_BUTTON },
    { ACT_TILE_29, SURFACE_BUTTON },
    { ACT_TILE_97, SURFACE_1B },
    { ACT_TILE_90, SURFACE_1C },
    { ACT_TILE_17, SURFACE_14 },
    { ACT_TILE_98, SURFACE_21 },
    { ACT_TILE_101, SURFACE_6 },
    { ACT_TILE_102, SURFACE_6 },
    { ACT_TILE_103, SURFACE_6 },
    { ACT_TILE_104, SURFACE_6 },
    { ACT_TILE_105, SURFACE_6 },
    { ACT_TILE_106, SURFACE_6 },
    { ACT_TILE_108, SURFACE_6 },
    { ACT_TILE_109, SURFACE_6 },
    { ACT_TILE_110, SURFACE_6 },
    { ACT_TILE_111, SURFACE_6 },
    { ACT_TILE_107, SURFACE_6 },
    { ACT_TILE_48, SURFACE_22 },
    { ACT_TILE_49, SURFACE_22 },
    { ACT_TILE_50, SURFACE_22 },
    { ACT_TILE_51, SURFACE_22 },
    { ACT_TILE_22, SURFACE_DUST },
    { ACT_TILE_25, SURFACE_HOLE },
    { ACT_TILE_240, SURFACE_HOLE },
    { ACT_TILE_87, SURFACE_CLONE_TILE },
    { ACT_TILE_83, SURFACE_LADDER },
    { ACT_TILE_241, SURFACE_LADDER },
    { ACT_TILE_63, SURFACE_AUTO_LADDER },
    { ACT_TILE_80, SURFACE_CLIMB_WALL },
    { ACT_TILE_81, SURFACE_2C },
    { ACT_TILE_52, SURFACE_LIGHT_GRADE },
    { ACT_TILE_53, SURFACE_29 },
    { ACT_TILE_42, SURFACE_E },
    { ACT_TILE_43, SURFACE_D },
    { ACT_TILE_44, SURFACE_10 },
    { ACT_TILE_45, SURFACE_F },
    { ACT_TILE_64, SURFACE_E },
    { ACT_TILE_65, SURFACE_D },
    { ACT_TILE_66, SURFACE_10 },
    { ACT_TILE_67, SURFACE_F },
    { ACT_TILE_72, SURFACE_10 },
    { ACT_TILE_74, SURFACE_E },
    { ACT_TILE_69, SURFACE_E },
    { ACT_TILE_71, SURFACE_E },
    { ACT_TILE_70, SURFACE_D },
    { ACT_TILE_73, SURFACE_D },
    { ACT_TILE_68, SURFACE_D },
    { ACT_TILE_75, SURFACE_D },
    { ACT_TILE_19, SURFACE_SWAMP },
    { ACT_TILE_41, SURFACE_DOOR },
    { ACT_TILE_23, SURFACE_2D },
    { ACT_TILE_40, SURFACE_DOOR_13 },
#ifdef PC_PORT
    /* PC-port fix (#5 KeyValuePair-terminator class): FindValueForKey /
     * FindEntryForKeyInternal iterate until `key == 0`. On GBA the assembler
     * placed `gMapActTileToSurfaceTypeEnd = 0` (a 0 u16) immediately after this
     * table so a not-found scan terminated naturally on the adjacent symbol.
     * On PC the C linker may reorder `const` definitions across TUs, so this
     * table (in its own translation unit) would otherwise run off the end into
     * unrelated .rodata when the searched act tile isn't listed (the common
     * case for ordinary ground under the player, hit every frame via
     * GetSurfaceUnder*). Inline a `{0, 0}` sentinel so the array self-terminates
     * regardless of layout — same fix as gUnk_0811C1F8.. in playerUtils.c. */
    { 0, 0 },
#endif
};
// const u16 gMapActTileToSurfaceTypeEnd = 0;
