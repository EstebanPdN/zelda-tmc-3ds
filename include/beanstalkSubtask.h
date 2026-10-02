#ifndef BEANSTALKSUBTASK_H
#define BEANSTALKSUBTASK_H

#include "global.h"
#include "map.h"

/*
tiles 0x4000 and above create an entry here
*/
#define MAX_SPECIAL_TILES 0x100
typedef struct {
    u16 tilePosAndLayer; // (layer << 12) | position
    u16 tileIndex;
} SpecialTileEntry;
extern SpecialTileEntry gTilesForSpecialTiles[MAX_SPECIAL_TILES];

typedef struct {
    u16 collision;
    u16 tileIndex;
} struct_080B44D0;

void LoadMapData(MapDataDefinition* dataDefinition);

#if defined(PC_PORT) && defined(MULTI_REGION)
/*
 * LoadMapData for COMPILED MapDataDefinition tables (gCaveBorder_*,
 * gGyorgMapping*): their src offsets are baked USA-baseline gMapData indices
 * and must be remapped for EU/JP (port/port_offset_remap.h). ROM-native /
 * asset-derived definitions must keep calling LoadMapData directly.
 */
void LoadMapDataB(const MapDataDefinition* dataDefinition);
#else
#define LoadMapDataB(def) LoadMapData((MapDataDefinition*)(def))
#endif

/**
 * Renders a tileMap with 16x16 tiles into a subTileMap with 8x8 tiles.
 *
 * Takes into account the special tile indicess >= 0x4000 using GetTileSetIndexForSpecialTile.
 */
void RenderMapLayerToSubTileMap(u16* tileMap, MapLayer* mapLayer);

extern void sub_0801AFE4(void);
extern void SetBGDefaults(void);

#ifndef TILE_DATA_DEFINED
#define TILE_DATA_DEFINED
typedef struct {
    s16 tileIndex;
    s16 tilePosOffset;
} TileData;
#endif

/**
 * @brief Sets multiple tiles at once
 *
 * @param tileData [u16 tileIndex, s16 positionOffset], ends with 0xffff
 * @param basePosition the position the offsets in tileData are based on
 * @param layer the tile layer
 */
extern void SetMultipleTiles(const TileData* tileData, u32 basePosition, u32 layer);

#ifndef STRUCT_02018EB0_DEFINED
#define STRUCT_02018EB0_DEFINED
typedef struct {
    u8 unk_0;
    u8 unk_1;
    u8 unk_2[2];
    u16 unk_4;
    u8 filler[12];
    struct Entity_* unk_14;
    u8 unk_18;
    u8 unk_19;
    u8 unk_1a;
    u8 unk_1b;
} struct_02018EB0;

extern struct_02018EB0 gUnk_02018EB0;
#endif

#endif // BEANSTALKSUBTASK_H
