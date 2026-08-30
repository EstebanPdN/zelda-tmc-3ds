#include <stdio.h>
#include <string.h>

#include "port_config.h"
#include "port_charge_bar.h"
#include "port_collision_fidelity.h"
#include "port_offset_remap.h"
#include "port_rom.h"
#include "region.h"

int gActiveRegion = TMC_REGION_USA;

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
    static _Alignas(4) u8 euRom[0x8400];
    static const u8 sharedOffers[] = {
        0x18, 0x2D, 0x35, 0x36, 0x37, 0x39, 0x3C, 0x44, 0x46,
        0x47, 0x4E, 0x50, 0x53, 0x55, 0x56, 0x58, 0x5F, 0x60,
    };
    static const u8 euContaminatedFusers[] = { 0x30, 0x3C, 0x66, 0x67, 0x68, 0x69, 0x6A };
    static const u8 euContaminatedOffers[] = { 0x0C, 0x34, 0x40, 0x4D, 0x5A, 0x29, 0x62 };
    static const u8 correctFirstOffers[] = { 0x0F, 0x40, 0x29, 0x29, 0x29, 0x29, 0x29 };
    static const u8 euE1Records[][PORT_FUSER_FUSION_RECORD_BYTES] = {
        { 0x05, 0x64, 0x3C, 0x1E, 0x0A, 0x0C, 0x00, 0x07, 0x64, 0x3C, 0x1E, 0x0A },
        { 0x05, 0x64, 0x1E, 0x3C, 0x0A, 0x34, 0x00, 0x02, 0x32, 0x0A, 0x1E, 0x3C },
        { 0x02, 0x64, 0x3C, 0x1E, 0x0A, 0x40, 0x00, 0x02, 0x64, 0x3C, 0x1E, 0x0A },
        { 0x02, 0x64, 0x3C, 0x1E, 0x0A, 0x4D, 0x00, 0x02, 0x64, 0x3C, 0x1E, 0x0A },
        { 0x02, 0x64, 0x3C, 0x1E, 0x0A, 0x5A, 0x45, 0x00, 0x02, 0x64, 0x1E, 0x3C },
        { 0x02, 0x64, 0x1E, 0x3C, 0x0A, 0x29, 0x25, 0x2A, 0x26, 0x2B, 0x2F, 0x00 },
        { 0x04, 0x64, 0x1E, 0x3C, 0x0A, 0x62, 0x00, 0x04, 0x64, 0x0A, 0x1E, 0x3C },
    };
    static const u8 euCorrectRecords[][PORT_FUSER_FUSION_RECORD_BYTES] = {
        { 0x02, 0x64, 0x0A, 0x1E, 0x3C, 0x0F, 0x00, 0x07, 0x64, 0x0A, 0x1E, 0x3C },
        { 0x02, 0x64, 0x3C, 0x1E, 0x0A, 0x40, 0x00, 0x02, 0x64, 0x3C, 0x1E, 0x0A },
        { 0x00, 0x64, 0x0A, 0x1E, 0x3C, 0x29, 0x25, 0x2A, 0x26, 0x2B, 0x2F, 0x00 },
        { 0x00, 0x64, 0x0A, 0x1E, 0x3C, 0x29, 0x25, 0x2A, 0x26, 0x2B, 0x2F, 0x00 },
        { 0x00, 0x64, 0x0A, 0x1E, 0x3C, 0x29, 0x25, 0x2A, 0x26, 0x2B, 0x2F, 0x00 },
        { 0x00, 0x64, 0x0A, 0x1E, 0x3C, 0x29, 0x25, 0x2A, 0x26, 0x2B, 0x2F, 0x00 },
        { 0x00, 0x64, 0x0A, 0x1E, 0x3C, 0x29, 0x25, 0x2A, 0x26, 0x2B, 0x2F, 0x00 },
    };
    u8 fusedBits[13] = { 0 };
    const u8* fusionData;
    const u16* shape;
    u32 i;

    CHECK_EQ(Port_ChargeBarUsaGfxOffset(0u), 0x21F20u, "charge frame 0 USA offset");
    CHECK_EQ(Port_ChargeBarUsaGfxOffset(1u), 0x21FE0u, "charge frame 1 USA offset");
    CHECK_EQ(Port_ChargeBarUsaGfxOffset(2u), 0x220A0u, "charge frame 2 USA offset");
    CHECK_EQ(Port_ChargeBarUsaGfxOffset(3u), 0x22160u, "charge frame 3 USA offset");
    CHECK_EQ(Port_ChargeBarUsaGfxOffset(4u), 0x21F20u, "charge frame upper bound falls back safely");
    CHECK_EQ(Port_ChargeBarUsaGfxOffset(UINT32_MAX), 0x21F20u,
             "charge frame wrapped value falls back safely");

    gActiveRegion = TMC_REGION_USA;
    CHECK_EQ(Port_RemapGfxOffset(Port_ChargeBarUsaGfxOffset(1u)), 0x21FE0u,
             "USA charge artwork keeps its native offset");
    gActiveRegion = TMC_REGION_EU;
    CHECK_EQ(Port_RemapGfxOffset(Port_ChargeBarUsaGfxOffset(0u)), 0x21EE0u,
             "EU charge frame 0 uses the regional runtime table");
    CHECK_EQ(Port_RemapGfxOffset(Port_ChargeBarUsaGfxOffset(1u)), 0x21FA0u,
             "EU charge frame 1 uses the regional runtime table");
    CHECK_EQ(Port_RemapGfxOffset(Port_ChargeBarUsaGfxOffset(2u)), 0x22060u,
             "EU charge frame 2 uses the regional runtime table");
    CHECK_EQ(Port_RemapGfxOffset(Port_ChargeBarUsaGfxOffset(3u)), 0x22120u,
             "EU charge frame 3 uses the regional runtime table");
    gActiveRegion = TMC_REGION_JP;
    CHECK_EQ(Port_RemapGfxOffset(Port_ChargeBarUsaGfxOffset(3u)), 0x22160u,
             "JP charge artwork shares the USA blob layout");
    gActiveRegion = TMC_REGION_USA;

    CHECK_EQ(PORT_USA_SPRITE_PTR_COUNT, 329u, "USA retail sprite-pointer count");
    CHECK_EQ(PORT_EU_SPRITE_PTR_COUNT, 328u, "EU retail sprite-pointer count reflects omitted entry");
    CHECK_EQ(PORT_USA_FRAME_OBJ_COUNT, 512u, "USA frame-object top-level count");
    CHECK_EQ(PORT_EU_FRAME_OBJ_COUNT, 511u, "EU frame-object top-level count reflects omitted entry");
    CHECK_EQ(Port_FrameObjListsSizeForRegion(ROM_REGION_USA), 200045u,
             "USA frame-object payload uses its exact retail size");
    CHECK_EQ(Port_FrameObjListsSizeForRegion(ROM_REGION_EU), 199561u,
             "EU frame-object payload stops before the following ROM table");
    CHECK_EQ(Port_FrameObjCountForRegion(ROM_REGION_USA), 512u,
             "USA frame-object lookup accepts exactly its native top-level entries");
    CHECK_EQ(Port_FrameObjCountForRegion(ROM_REGION_EU), 511u,
             "EU frame-object lookup excludes the omitted top-level entry");
    CHECK_EQ(Port_FixedTypeGfxCountForRegion(ROM_REGION_USA), 526u,
             "USA fixed-gfx table contains native indices 0 through 525");
    CHECK_EQ(Port_FixedTypeGfxCountForRegion(ROM_REGION_EU), 525u,
             "EU fixed-gfx table contains native indices 0 through 524");
    CHECK_EQ(Port_IsFixedTypeGfxIndexValidForRegion(ROM_REGION_USA, 525u), TRUE,
             "USA accepts its last native fixed-gfx index");
    CHECK_EQ(Port_IsFixedTypeGfxIndexValidForRegion(ROM_REGION_EU, 525u), FALSE,
             "EU rejects the first index beyond its fixed-gfx table");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_USA, 287u), 287u,
             "USA index before the regional hole stays native");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_USA, 288u), 288u,
             "USA keeps its ObjectB4_1 entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_USA, 289u), 289u,
             "USA index after the regional hole stays native");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 287u), 287u,
             "EU index immediately before the hole stays native");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 288u), PORT_INVALID_SPRITE_INDEX,
             "EU rejects the USA-only ObjectB4_1 entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 289u), 288u,
             "EU shifts the first sprite after the omitted entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 302u), 301u,
             "EU spiked-roller sprite selects its retail table entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 319u), 318u,
             "EU spear-Moblin alternate sprite selects its retail table entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 320u), 319u,
             "EU bow-Moblin alternate sprite selects its retail table entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 321u), 320u,
             "EU arrow projectile selects its retail table entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 322u), 321u,
             "EU item UI sprite selects its retail table entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 328u), 327u,
             "EU Vaati alternate sprite selects its retail table entry");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 505u), 504u,
             "EU late HUD sprite observes the same single table hole");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_EU, 511u), 510u,
             "EU last logical frame-object index remains in bounds");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_JP, 321u), 321u,
             "JP never inherits the EU-only table shift");
    CHECK_EQ(Port_RemapLogicalSpriteIndexForRegion(ROM_REGION_UNKNOWN, 321u), 321u,
             "unknown region fails closed without guessing an EU shift");
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

    CHECK_EQ(PORT_FUSER_TABLE_COUNT, 120u, "retail fuser pointer-table count stays bounded");
    CHECK_EQ(PORT_FUSION_TEXT_PTRS_USA, 0x1A7Cu, "USA fusion-text table offset");
    CHECK_EQ(PORT_FUSION_TEXT_PTRS_EU, 0x1B24u, "EU fusion-text table offset");
    CHECK_EQ(PORT_FUSER_FUSION_PTRS_USA, 0x1DCCu, "USA offered-fusion table offset");
    CHECK_EQ(PORT_FUSER_FUSION_PTRS_EU, 0x1E74u, "EU offered-fusion table offset");

    memset(rom, 0, sizeof(rom));
    CHECK_TRUE(Port_MapDataFromRom(rom, sizeof(rom), 0x1000u) == rom + 0x1000u,
               "map data aliases its immutable window in the loaded ROM");
    CHECK_TRUE(Port_MapDataFromRom(rom, sizeof(rom), sizeof(rom)) == NULL,
               "map data rejects an offset beyond the loaded ROM");
    WriteU32(rom + PORT_COLLISION_SHAPE_PTRS_USA, 0x08001000u);
    WriteU32(rom + PORT_COLLISION_SHAPE_PTRS_EU, 0x08001100u);
    memset(rom + 0x1000, 0xFF, 16u * sizeof(u16));
    memset(rom + 0x1100, 0, 16u * sizeof(u16));
    rom[PORT_TILE_TYPE_PROPERTIES_USA] = 0xCC;
    rom[PORT_TILE_TYPE_PROPERTIES_USA + 1u] = 0x57;
    rom[PORT_TILE_TYPE_PROPERTIES_EU] = 0x00;
    rom[PORT_TILE_TYPE_PROPERTIES_EU + 1u] = 0x00;
    WriteU32(rom + PORT_FUSION_TEXT_PTRS_USA + 44u * sizeof(u32), 0x08003000u);
    WriteU32(rom + PORT_FUSION_TEXT_PTRS_EU + 44u * sizeof(u32), 0x08003100u);
    WriteU32(rom + PORT_FUSER_FUSION_PTRS_USA + 44u * sizeof(u32), 0x08003200u);
    WriteU32(rom + PORT_FUSER_FUSION_PTRS_EU + 44u * sizeof(u32), 0x08003300u);
    rom[0x3000] = 0xA1;
    rom[0x3100] = 0xE1;
    rom[0x3200] = 0xA2;
    rom[0x3300] = 0xE2;

    fusionData = Port_ResolveFusionTextDataFromRom(rom, sizeof(rom), PORT_FUSION_TEXT_PTRS_USA, 44u);
    CHECK_TRUE(fusionData == rom + 0x3000, "USA fusion text stays on the USA-native pointer table");
    CHECK_EQ(fusionData != NULL ? fusionData[0] : 0u, 0xA1u, "USA fusion text target remains unchanged");
    fusionData = Port_ResolveFusionTextDataFromRom(rom, sizeof(rom), PORT_FUSION_TEXT_PTRS_EU, 44u);
    CHECK_TRUE(fusionData == rom + 0x3100, "EU fusion text resolves through the EU-native pointer table");
    CHECK_EQ(fusionData != NULL ? fusionData[0] : 0u, 0xE1u,
             "EU fusion text cannot silently use the USA target");
    fusionData = Port_ResolveFuserDataFromRom(rom, sizeof(rom), PORT_FUSER_FUSION_PTRS_USA, 44u, 6u);
    CHECK_TRUE(fusionData == rom + 0x3200, "USA fusion offer stays on the USA-native pointer table");
    CHECK_EQ(fusionData != NULL ? fusionData[0] : 0u, 0xA2u, "USA fusion offer target remains unchanged");
    fusionData = Port_ResolveFuserDataFromRom(rom, sizeof(rom), PORT_FUSER_FUSION_PTRS_EU, 44u, 6u);
    CHECK_TRUE(fusionData == rom + 0x3300, "EU fusion offer resolves through the EU-native pointer table");
    CHECK_EQ(fusionData != NULL ? fusionData[0] : 0u, 0xE2u,
             "EU fusion offer cannot silently use the USA target");
    CHECK_TRUE(Port_ResolveFuserDataFromRom(rom, sizeof(rom), PORT_FUSION_TEXT_PTRS_EU,
                                            PORT_FUSER_TABLE_COUNT, 6u) == NULL,
               "fuser ids outside the retail pointer table are rejected");
    WriteU32(rom + 44u * sizeof(u32), 0x08003400u);
    CHECK_TRUE(Port_ResolveFuserDataFromRom(rom, sizeof(rom), 0u, 44u, 6u) == NULL,
               "an unpopulated regional table offset cannot reinterpret the ROM header as pointers");
    WriteU32(rom + PORT_FUSION_TEXT_PTRS_EU + 46u * sizeof(u32), 0x080083FAu);
    CHECK_TRUE(Port_ResolveFusionTextDataFromRom(rom, sizeof(rom), PORT_FUSION_TEXT_PTRS_EU, 46u) ==
                   rom + 0x83FA,
               "one fusion text triple accepts exactly six target bytes");
    CHECK_TRUE(Port_ResolvePairedFusionTextDataFromRom(rom, sizeof(rom), PORT_FUSION_TEXT_PTRS_EU, 46u) == NULL,
               "paired fusion text rejects a target with only one triple remaining");
    WriteU32(rom + PORT_FUSION_TEXT_PTRS_EU + 45u * sizeof(u32), 0x080083FEu);
    CHECK_TRUE(Port_ResolveFusionTextDataFromRom(rom, sizeof(rom), PORT_FUSION_TEXT_PTRS_EU, 45u) == NULL,
               "truncated fusion text target is rejected");

    /* v1.2-E1 EU contamination fixture.  0x1E74 - 0x1DCC = 0xA8,
     * exactly 42 pointer entries: asking the stale USA base for fuser N reads
     * EU fuser N-42.  Populate both pointer locations and prove that every
     * observed E1 offer is an ordinary in-range id yet semantically wrong for
     * E2's corrected cursor.  Wall 0x69 is the intentional control: its stale
     * first offer happens to equal the correct first offer and must not be
     * touched. */
    memset(euRom, 0, sizeof(euRom));
    for (i = 0; i < ARRAY_COUNT(euContaminatedFusers); ++i) {
        const u32 fuserId = euContaminatedFusers[i];
        const u32 staleRecordOffset = 0x4000u + i * 0x20u;
        const u32 correctRecordOffset = 0x5000u + i * 0x20u;
        WriteU32(euRom + PORT_FUSER_FUSION_PTRS_USA + fuserId * sizeof(u32),
                 0x08000000u + staleRecordOffset);
        WriteU32(euRom + PORT_FUSER_FUSION_PTRS_EU + fuserId * sizeof(u32),
                 0x08000000u + correctRecordOffset);
        memcpy(euRom + staleRecordOffset, euE1Records[i], PORT_FUSER_FUSION_RECORD_BYTES);
        memcpy(euRom + correctRecordOffset, euCorrectRecords[i], PORT_FUSER_FUSION_RECORD_BYTES);
    }
    for (i = 0; i < ARRAY_COUNT(euContaminatedFusers); ++i) {
        const u32 fuserId = euContaminatedFusers[i];
        const u8* staleData = Port_ResolveFuserDataFromRom(euRom, sizeof(euRom), PORT_FUSER_FUSION_PTRS_USA,
                                                           fuserId, PORT_FUSER_FUSION_RECORD_BYTES);
        const u8* correctData = Port_ResolveFuserDataFromRom(euRom, sizeof(euRom), PORT_FUSER_FUSION_PTRS_EU,
                                                             fuserId, PORT_FUSER_FUSION_RECORD_BYTES);
        CHECK_TRUE(staleData != NULL && staleData[5] == euContaminatedOffers[i],
                   "stale E1 base reproduces the displaced EU offer");
        CHECK_TRUE(correctData != NULL && correctData[5] == correctFirstOffers[i],
                   "correct E2 base resolves the retail EU offer");
        CHECK_TRUE(Port_IsFuserSaveStateValid(correctData, 0u, euContaminatedOffers[i]),
                   "range-only E2 validation accepts the contaminated concrete id");
        CHECK_TRUE(Port_IsFuserSaveStateSemanticallyValid(staleData, 0u, euContaminatedOffers[i], fusedBits,
                                                          sizeof(fusedBits), sharedOffers,
                                                          ARRAY_COUNT(sharedOffers)),
                   "observed E1 offer is valid against the exactly displaced EU record");
        CHECK_EQ(Port_IsFuserSaveStateSemanticallyValid(
                     correctData, 0u, euContaminatedOffers[i], fusedBits, sizeof(fusedBits), sharedOffers,
                     ARRAY_COUNT(sharedOffers)),
                 euContaminatedOffers[i] == correctFirstOffers[i],
                 "semantic validation repairs only offers impossible at the corrected EU cursor");
        CHECK_EQ(Port_ShouldRepairE1EuFuserSaveState(
                     1, 0, fuserId, correctData, staleData, 0u, euContaminatedOffers[i], fusedBits,
                     sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                 euContaminatedOffers[i] != correctFirstOffers[i],
                 "automatic repair requires the exact EU E1 displacement signature");
        CHECK_TRUE(!Port_ShouldRepairE1EuFuserSaveState(
                       0, 0, fuserId, correctData, staleData, 0u, euContaminatedOffers[i], fusedBits,
                       sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)) &&
                       !Port_ShouldRepairE1EuFuserSaveState(
                           1, 1, fuserId, correctData, staleData, 0u, euContaminatedOffers[i], fusedBits,
                           sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                   "USA/JP and randomizer states are never automatically repaired");
        CHECK_TRUE(Port_IsFuserSaveStateSemanticallyValid(correctData, 0u, correctFirstOffers[i], fusedBits,
                                                          sizeof(fusedBits), sharedOffers,
                                                          ARRAY_COUNT(sharedOffers)),
                   "valid corrected EU offer is never selected for repair");
    }
    {
        static const u8 staleOnlyRecord[PORT_FUSER_FUSION_RECORD_BYTES] = {
            0x00, 0x64, 0x0A, 0x1E, 0x3C, 0x34, 0x00,
        };
        static const u8 correctedRecord[PORT_FUSER_FUSION_RECORD_BYTES] = {
            0x00, 0x64, 0x0A, 0x1E, 0x3C, 0x40, 0x00,
        };
        CHECK_TRUE(!Port_ShouldRepairE1EuFuserSaveState(
                       1, 0, PORT_FUSER_E1_EU_TABLE_DISPLACEMENT - 1u, correctedRecord, staleOnlyRecord, 0u,
                       0x34u, fusedBits, sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                   "EU fusers before the 42-entry displacement boundary fail closed");
        CHECK_TRUE(!Port_ShouldRepairE1EuFuserSaveState(
                       1, 0, PORT_FUSER_E1_EU_TABLE_DISPLACEMENT, correctedRecord, NULL, 0u, 0x34u,
                       fusedBits, sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                   "missing stale E1 ROM provenance fails closed");
        CHECK_TRUE(!Port_ShouldRepairE1EuFuserSaveState(
                       1, 0, PORT_FUSER_E1_EU_TABLE_DISPLACEMENT, correctedRecord, correctedRecord, 0u,
                       0x34u, fusedBits, sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                   "generic EU semantic mismatch without the E1 displacement signature fails closed");
    }

    /* USA never had the 0xA8 base displacement. Its valid fixed, random, and
     * terminal states stay byte-for-byte eligible, while a mismatched concrete
     * offer and an already-fused concrete offer are provably impossible. */
    {
        static const u8 usaFixedRecord[PORT_FUSER_FUSION_RECORD_BYTES] = {
            0x02, 0x64, 0x3C, 0x1E, 0x0A, 0x40, 0x00,
        };
        static const u8 usaRandomRecord[PORT_FUSER_FUSION_RECORD_BYTES] = {
            0x02, 0x64, 0x3C, 0x1E, 0x0A, 0xFF, 0x00,
        };
        CHECK_TRUE(Port_IsFuserSaveStateSemanticallyValid(usaFixedRecord, 0u, 0x40u, fusedBits,
                                                          sizeof(fusedBits), sharedOffers,
                                                          ARRAY_COUNT(sharedOffers)),
                   "valid USA fixed offer is unchanged");
        CHECK_TRUE(!Port_IsFuserSaveStateSemanticallyValid(usaFixedRecord, 0u, 0x34u, fusedBits,
                                                           sizeof(fusedBits), sharedOffers,
                                                           ARRAY_COUNT(sharedOffers)),
                   "USA fixed cursor rejects a displaced concrete offer too");
        CHECK_TRUE(Port_IsFuserSaveStateSemanticallyValid(usaRandomRecord, 0u, sharedOffers[0], fusedBits,
                                                          sizeof(fusedBits), sharedOffers,
                                                          ARRAY_COUNT(sharedOffers)),
                   "retail random cursor retains an unfused shared offer");
        CHECK_TRUE(!Port_IsFuserSaveStateSemanticallyValid(usaRandomRecord, 0u, 0x0Fu, fusedBits,
                                                           sizeof(fusedBits), sharedOffers,
                                                           ARRAY_COUNT(sharedOffers)),
                   "random cursor rejects a concrete id outside the shared list");
        fusedBits[0x40u / 8u] |= (u8)(1u << (0x40u % 8u));
        CHECK_TRUE(!Port_IsFuserSaveStateSemanticallyValid(usaFixedRecord, 0u, 0x40u, fusedBits,
                                                           sizeof(fusedBits), sharedOffers,
                                                           ARRAY_COUNT(sharedOffers)),
                   "already-fused concrete offer is not a stable saved state");
        CHECK_TRUE(Port_IsFuserSaveStateSemanticallyValid(usaFixedRecord, 0u, 0xF1u, fusedBits,
                                                          sizeof(fusedBits), sharedOffers,
                                                          ARRAY_COUNT(sharedOffers)) &&
                       Port_IsFuserSaveStateSemanticallyValid(usaFixedRecord, 0u, 0xF2u, fusedBits,
                                                              sizeof(fusedBits), sharedOffers,
                                                              ARRAY_COUNT(sharedOffers)) &&
                       Port_IsFuserSaveStateSemanticallyValid(usaFixedRecord, 0u, 0xF3u, fusedBits,
                                                              sizeof(fusedBits), sharedOffers,
                                                              ARRAY_COUNT(sharedOffers)) &&
                       Port_IsFuserSaveStateSemanticallyValid(usaFixedRecord, 0u, 0xFFu, fusedBits,
                                                              sizeof(fusedBits), sharedOffers,
                                                              ARRAY_COUNT(sharedOffers)),
                   "script and retail sentinel states are preserved conservatively");
    }

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
