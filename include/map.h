#ifndef MAP_H
#define MAP_H

#include "screen.h"

/**
 * @page TileMap Tile Map
 * @brief The map consists of tiles to create the world.
 *
 * GBA graphics are made out of tiles with a size of 8px by 8px. We call those tiles *subTiles*.
 * Four of those subTiles are combined together to form a bigger 16px by 16px tile (usually called metaTile). We call
 * these *tiles*.
 *
 * Each map can be up to 64 * 64 tiles big. The map consists of two layers of tiles gMapTop and gMapBottom.
 * To access the map array the tilePos index can be used which goes from 0 to 64 * 64 = 4096.
 * Each value in the MapLayer.mapData is a tileIndex which defined which tile of the tileSet should be used.
 * The tileSet contains tiles from 0 to 2048. Special tileIndex from 0x4000 to 0x4096 can be used to denote special
 * tiles.
 *
 * A tile is created from four subTiles. The subTiles for each tile in the tileSet are stored in MapLayer.subTiles. This
 * is loaded for the current area from gAreaTileSets_*. The subTiles can also be flipped or the palette changed. This is
 * stored in bits 0xa to 0xf, see https://www.coranac.com/tonc/text/regbg.htm#sec-map.
 *
 * Each tile in the tileSet also has a tileType defined. This is stored in MapLayer.tileTypes and loaded from
 * gAreaTileSetTypes_*. The inverse dictionary from tileType to tileIndex is stored in MapLayer.tileIndices.
 *
 * The map also stores the collision for each tile in the tileMap layer in MapLayer.collisionData. By default this is
 * filled from gMapTileTypeToCollisionData based on the tileType of the tiles. But it can also be loaded from
 * gRoomCollisionMap_*. In MapLayer.actTiles some additional type for each tile is stored which is filled from
 * gMapTileTypeToActTile based on the tileType of the tiles.
 */

// Maximum width or height of a map in tiles.
#define MAX_MAP_SIZE 64
// Maximum amount of tiles in a tileSet.
#define TILESET_SIZE 2048

/**
 * @brief Layer of the TileMap.
 * @ingroup TileMap
 */
typedef struct {
    /*0x0000*/ BgSettings* bgSettings;
    /**
     * tileIndex for each tile on the current layer.
     */
    /*0x0004*/ u16 mapData[MAX_MAP_SIZE * MAX_MAP_SIZE];
    /**
     * Collision data for each tile on the current layer.
     * @see CollisionData
     */
    /*0x2004*/ u8 collisionData[MAX_MAP_SIZE * MAX_MAP_SIZE];
    /**
     * Copy of the map data.
     * @see mapData
     */
    /*0x3004*/ u16 mapDataOriginal[MAX_MAP_SIZE * MAX_MAP_SIZE];
    /**
     * Maps from the tileIndex to the tileType.
     * @see TileType
     */
    /*0x5004*/ u16 tileTypes[TILESET_SIZE];
    /**
     * Maps from a tileType to a tileIndex. Inverse of @see tileTypes.
     * @see TileType
     */
    /*0x6004*/ u16 tileIndices[TILESET_SIZE];
    /**
     * Maps from a tile index to the four sub tiles (with attributes) it consists of.
     * @see https://www.coranac.com/tonc/text/regbg.htm#sec-map
     */
    /*0x7004*/ u16 subTiles[TILESET_SIZE * 4];
    /**
     * Some sort of special behavior for tiles? Falling into holes or jumping off walls does not work when this is all
     * zero.
     * @see ActTile
     */
    /*0xb004*/ u8 actTiles[MAX_MAP_SIZE * MAX_MAP_SIZE];
} MapLayer;

PORT_STATIC_ASSERT_OFFSET(MapLayer, mapData, 0x0004, 0x0008, "MapLayer.mapData offset drift");
PORT_STATIC_ASSERT_OFFSET(MapLayer, collisionData, 0x2004, 0x2008, "MapLayer.collisionData offset drift");
PORT_STATIC_ASSERT_OFFSET(MapLayer, mapDataOriginal, 0x3004, 0x3008, "MapLayer.mapDataOriginal offset drift");
PORT_STATIC_ASSERT_OFFSET(MapLayer, tileTypes, 0x5004, 0x5008, "MapLayer.tileTypes offset drift");
PORT_STATIC_ASSERT_OFFSET(MapLayer, tileIndices, 0x6004, 0x6008, "MapLayer.tileIndices offset drift");
PORT_STATIC_ASSERT_OFFSET(MapLayer, subTiles, 0x7004, 0x7008, "MapLayer.subTiles offset drift");
PORT_STATIC_ASSERT_OFFSET(MapLayer, actTiles, 0xb004, 0xb008, "MapLayer.actTiles offset drift");
PORT_STATIC_ASSERT_SIZE(MapLayer, 0xc004, 0xc008, "MapLayer size drift");

extern MapLayer gMapTop;
extern MapLayer gMapBottom;

typedef enum {
    LAYER_BOTTOM = 1,
    LAYER_TOP = 2,
} LayerIndex;

/**
 * Returns the MapLayer for a layer of the map.
 *
 * @param layer @see LayerIndex
 */
extern MapLayer* GetLayerByIndex(u32 layer);

// There is another map data definition following.
#define MAP_MULTIPLE 0x80000000
#ifdef PC_PORT
#define MAP_SRC_FILE 0x40000000
#else
#define MAP_SRC_FILE 0
#endif
// The src is compressed.
#define MAP_COMPRESSED 0x80000000

/**
 * Definition where some map data is found and where it should be copied to.
 * Defined using the map_data asm macro, e.g. in map_headers.s
 */
typedef struct {
    /**
     * @brief Source offset from gMapData.
     *
     * MAP_MULTIPLE bit is set if there is another MapDataDefinition following.
     */
    u32 src;
    /**
     * Target pointer where to load or decompress the map data to.
     */
    void* dest;
    /**
     * @brief Size of the map data in bytes.
     *
     * MAP_COMPRESSED bit is set if the map data is compressed.
     */
    u32 size;
} MapDataDefinition;

PORT_STATIC_ASSERT_SIZE(MapDataDefinition, 0x0C, 0x18, "MapDataDefinition size drift");
PORT_STATIC_ASSERT_OFFSET(MapDataDefinition, src,  0x00, 0x00, "MapDataDefinition.src offset");
PORT_STATIC_ASSERT_OFFSET(MapDataDefinition, dest, 0x04, 0x08, "MapDataDefinition.dest offset");
PORT_STATIC_ASSERT_OFFSET(MapDataDefinition, size, 0x08, 0x10, "MapDataDefinition.size offset");

#endif // MAP_H
