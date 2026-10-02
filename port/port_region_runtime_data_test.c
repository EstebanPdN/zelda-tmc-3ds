#include <stdio.h>
#include <string.h>

#include "port_config.h"
#include "port_collision_fidelity.h"
#include "port_rom.h"

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        unsigned got__ = (unsigned)(actual);                                                                     \
        unsigned want__ = (unsigned)(expected);                                                                  \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got 0x%X expected 0x%X\n", message, got__, want__);                      \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

#define CHECK_TRUE(condition, message)                                                                           \
    do {                                                                                                         \
        if (!(condition)) {                                                                                      \
            fprintf(stderr, "FAIL: %s\n", message);                                                            \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

static void WriteU32(u8* dest, u32 value) {
    dest[0] = (u8)value;
    dest[1] = (u8)(value >> 8);
    dest[2] = (u8)(value >> 16);
    dest[3] = (u8)(value >> 24);
}

int main(void) {
    static _Alignas(4) u8 rom[0x8400];
    const u16* shape;

    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_USA, 322u), 322u,
             "USA item UI sprite stays native");
    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_EU, 322u), 321u,
             "EU item UI sprite uses its shifted entry");
    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_USA, 505u), 505u,
             "USA HUD button sprite stays native");
    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_EU, 505u), 504u,
             "EU HUD button sprite uses its shifted entry");
    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_EU, 506u), 506u,
             "EU fixed UI remap is not a broad late-sprite shift");
    CHECK_EQ(Port_ShouldUseAreaAssetCacheForRegion(ROM_REGION_USA), 1u,
             "USA may use its matching extracted area-table cache");
    CHECK_EQ(Port_ShouldUseAreaAssetCacheForRegion(ROM_REGION_EU), 0u,
             "EU keeps region-native room properties from the active ROM");
    CHECK_EQ(Port_ShouldUseAreaAssetCacheForRegion(ROM_REGION_JP), 0u,
             "JP keeps region-native room properties from the active ROM");
    CHECK_EQ(Port_ShouldUseAreaAssetCacheForRegion(ROM_REGION_UNKNOWN), 0u,
             "unknown regions fail closed to the active-ROM path");

    CHECK_EQ(Port_ApplyCollisionLayerTransition(2u, 2u, 1u), 2u,
             "transition tile preserves its guarded upper layer");
    CHECK_EQ(Port_ApplyCollisionLayerTransition(1u, 2u, 1u), 1u,
             "transition tile sends every other layer to its destination");
    CHECK_EQ(Port_ApplyCollisionLayerTransition(2u, 1u, 2u), 2u,
             "inverse transition sends upper-layer traversal to layer 2");
    CHECK_EQ(Port_ApplyCollisionLayerTransition(1u, 3u, 3u), 3u,
             "one-way transition tile forces layer 3 from layer 1");
    CHECK_EQ(Port_ApplyCollisionLayerTransition(2u, 3u, 3u), 3u,
             "one-way transition tile forces layer 3 from layer 2");
    CHECK_EQ(Port_ApplyCollisionLayerTransition(3u, 3u, 3u), 3u,
             "one-way transition tile preserves layer 3");
    CHECK_EQ(Port_ShouldFallbackTilePropertyLayer(0x40u), 1u,
             "lantern compatibility may inspect the other tile layer");
    CHECK_EQ(Port_ShouldFallbackTilePropertyLayer(0x10u), 0u,
             "climb and jump properties stay on the active collision layer");
    CHECK_EQ(Port_ShouldFallbackTilePropertyLayer(0x80u), 0u,
             "projectile properties stay on the active collision layer");

    memset(rom, 0, sizeof(rom));
    WriteU32(rom + PORT_COLLISION_SHAPE_PTRS_USA, 0x08001000u);
    WriteU32(rom + PORT_COLLISION_SHAPE_PTRS_EU, 0x08001100u);
    memset(rom + 0x1000, 0xFF, 16u * sizeof(u16));
    memset(rom + 0x1100, 0, 16u * sizeof(u16));
    rom[PORT_TILE_TYPE_PROPERTIES_USA] = 0xCC;
    rom[PORT_TILE_TYPE_PROPERTIES_USA + 1u] = 0x57;
    rom[PORT_TILE_TYPE_PROPERTIES_EU] = 0x00;
    rom[PORT_TILE_TYPE_PROPERTIES_EU + 1u] = 0x00;

    CHECK_EQ(Port_ReadTileTypePropertyFromRom(rom, sizeof(rom), PORT_TILE_TYPE_PROPERTIES_USA, 0u), 0x57CCu,
             "USA tile-property offset reads the USA record");
    CHECK_EQ(Port_ReadTileTypePropertyFromRom(rom, sizeof(rom), PORT_TILE_TYPE_PROPERTIES_EU, 0u), 0u,
             "EU tile-property offset does not read the USA-position pointer bytes");
    CHECK_EQ(Port_ReadTileTypePropertyFromRom(rom, sizeof(rom), sizeof(rom) - 1u, 0u), 0u,
             "truncated tile-property record is rejected");

    shape = Port_ResolveCollisionShapeFromRom(rom, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_EU, 0);
    CHECK_TRUE(shape == (const u16*)(rom + 0x1100), "EU collision table resolves its EU-native target");
    CHECK_EQ(shape != NULL ? shape[0] : 1u, 0u, "EU passable collision mask stays clear");

    shape = Port_ResolveCollisionShapeFromRom(rom, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_USA, 0);
    CHECK_TRUE(shape == (const u16*)(rom + 0x1000), "USA collision table resolves its USA-native target");
    CHECK_EQ(shape != NULL ? shape[0] : 0u, 0xFFFFu, "stale-region pointer would select the blocking mask");

    CHECK_TRUE(Port_ResolveCollisionShapeFromRom(NULL, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_EU, 0) == NULL,
               "null ROM is rejected");
    CHECK_TRUE(Port_ResolveCollisionShapeFromRom(rom, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_EU, 40) == NULL,
               "out-of-range collision mask index is rejected");
    CHECK_TRUE(Port_ResolveCollisionShapeFromRom(rom, PORT_COLLISION_SHAPE_PTRS_EU + 3u,
                                                 PORT_COLLISION_SHAPE_PTRS_EU, 0) == NULL,
               "truncated pointer table is rejected");
    WriteU32(rom + PORT_COLLISION_SHAPE_PTRS_EU + sizeof(u32), 0x08001101u);
    CHECK_TRUE(Port_ResolveCollisionShapeFromRom(rom, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_EU, 1) == NULL,
               "unaligned collision mask target is rejected");

    if (sFailures != 0) {
        return 1;
    }
    printf("port_region_runtime_data_test: ALL PASS\n");
    return 0;
}
