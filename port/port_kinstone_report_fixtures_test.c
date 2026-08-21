#include <stdio.h>
#include <string.h>

#include "port_rom.h"

static int sFailures;

#define CHECK_TRUE(condition, message)                                                                           \
    do {                                                                                                         \
        if (!(condition)) {                                                                                      \
            fprintf(stderr, "FAIL: %s\n", message);                                                            \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        unsigned got__ = (unsigned)(actual);                                                                     \
        unsigned want__ = (unsigned)(expected);                                                                  \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got 0x%X expected 0x%X\n", message, got__, want__);                     \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

static void WriteU32(u8* dest, u32 value) {
    dest[0] = (u8)value;
    dest[1] = (u8)(value >> 8);
    dest[2] = (u8)(value >> 16);
    dest[3] = (u8)(value >> 24);
}

static void InstallFuserRecord(u8* rom, u32 tableOffset, u32 fuserId, u32 recordOffset,
                               const u8 record[7]) {
    WriteU32(rom + tableOffset + fuserId * sizeof(u32), 0x08000000u + recordOffset);
    memcpy(rom + recordOffset, record, 7u);
}

static int IsUnlockedAtGlobalProgress(const u8* record, u32 globalProgress) {
    /* This is the exact gate used by GetFusionToOffer. */
    return record != NULL && record[0] <= globalProgress;
}

int main(void) {
    static _Alignas(4) u8 euRom[0x2400];
    static _Alignas(4) u8 usaRom[0x2400];
    static _Alignas(4) u8 packedPointerRom[0x80];
    static const u8 sharedOffers[] = {
        0x18, 0x2D, 0x35, 0x36, 0x37, 0x39, 0x3C, 0x44, 0x46,
        0x47, 0x4E, 0x50, 0x53, 0x55, 0x56, 0x58, 0x5F, 0x60,
    };
    static const u8 tingleE1[] = { 0x02, 0x32, 0x0A, 0x1E, 0x3C, 0xFF, 0x00 };
    static const u8 tingleRetail[] = { 0x02, 0x64, 0x3C, 0x1E, 0x0A, 0x4D, 0x00 };
    static const u8 forest5bE1[] = { 0x07, 0x64, 0x0A, 0x1E, 0x3C, 0x13, 0x00 };
    static const u8 forest5bRetail[] = { 0x05, 0x64, 0x1E, 0x3C, 0x0A, 0x4C, 0x00 };
    static const u8 forest5dE1[] = { 0x07, 0x64, 0x0A, 0x1E, 0x3C, 0x4A, 0x00 };
    static const u8 forest5dRetail[] = { 0x05, 0x64, 0x1E, 0x3C, 0x0A, 0x64, 0x00 };
    static const u8 forest56Retail[] = { 0x05, 0x64, 0x3C, 0x1E, 0x0A, 0x2E, 0x00 };
    static const struct {
        u32 fuserId;
        u32 e1Offset;
        u32 retailOffset;
        const u8* e1Record;
        const u8* retailRecord;
        u8 contaminatedOffer;
        u8 retailOffer;
    } repairCases[] = {
        { 0x3D, 0x20DF, 0x2211, tingleE1, tingleRetail, 0x3C, 0x4D },
        { 0x5B, 0x21B8, 0x22F6, forest5bE1, forest5bRetail, 0x13, 0x4C },
        { 0x5D, 0x21C6, 0x2304, forest5dE1, forest5dRetail, 0x4A, 0x64 },
    };
    u8 fusedBits[13] = { 0 };
    u8 npcTable[24] = { 0 };
    const u8* eu56;
    const u8* usa56;
    u64 packedFuser;
    u32 i;

    /* The packed resolver is also used by several even-addressed retail data
     * tables (guards, inns, Simon and Gust Jar).  Preserve the old/even case
     * while proving that odd byte-addressed records are not mistaken for
     * Thumb function pointers.  Bounds remain fail-closed in both cases. */
    WriteU32(packedPointerRom + 0x10u, 0x08000040u);
    WriteU32(packedPointerRom + 0x14u, 0x08000041u);
    WriteU32(packedPointerRom + 0x18u, 0x0800007Fu);
    CHECK_TRUE(Port_ResolvePackedRomDataPtrFromRom(
                   packedPointerRom, sizeof(packedPointerRom), 0x10u, 0u, 8u) == packedPointerRom + 0x40u,
               "generic packed-data resolver preserves existing even targets");
    CHECK_TRUE(Port_ResolvePackedRomDataPtrFromRom(
                   packedPointerRom, sizeof(packedPointerRom), 0x10u, 1u, 8u) == packedPointerRom + 0x41u,
               "generic packed-data resolver preserves legitimate odd byte targets");
    CHECK_TRUE(Port_ResolvePackedRomDataPtrFromRom(
                   packedPointerRom, sizeof(packedPointerRom), 0x10u, 2u, 2u) == NULL,
               "generic packed-data resolver rejects a target whose requested span crosses ROM end");
    CHECK_TRUE(Port_ResolvePackedRomDataPtrFromRom(
                   packedPointerRom, sizeof(packedPointerRom), 0x7Fu, 1u, 1u) == NULL,
               "generic packed-data resolver rejects an overflowing table entry");

    /* NEW-3g: the retail NPC wildcard row is id=0x1B, type=2,
     * type2=0xFF -> fuser 0x3D.  A concrete entity type2 of zero must match. */
    memcpy(npcTable + 12, (const u8[]){ 0x1B, 0x02, 0xFF, 0x3D, 0x87, 0x02 }, 6u);
    packedFuser = Port_FindEntityFuserDataFromRom(npcTable, sizeof(npcTable), 6u, 0x1B, 0x02, 0x00);
    CHECK_EQ((u32)packedFuser, 0x3Du, "Trilby Tingle resolves to fuser 0x3D");
    CHECK_EQ((u32)(packedFuser >> 32), 0x0287u, "Trilby Tingle retains its retail fusion text id");

    for (i = 0; i < ARRAY_COUNT(repairCases); ++i) {
        const u8* stale;
        const u8* corrected;

        InstallFuserRecord(euRom, PORT_FUSER_FUSION_PTRS_USA, repairCases[i].fuserId,
                           repairCases[i].e1Offset, repairCases[i].e1Record);
        InstallFuserRecord(euRom, PORT_FUSER_FUSION_PTRS_EU, repairCases[i].fuserId,
                           repairCases[i].retailOffset, repairCases[i].retailRecord);
        stale = Port_ResolveFuserDataFromRom(euRom, sizeof(euRom), PORT_FUSER_FUSION_PTRS_USA,
                                             repairCases[i].fuserId, PORT_FUSER_FUSION_RECORD_BYTES);
        corrected = Port_ResolveFuserDataFromRom(euRom, sizeof(euRom), PORT_FUSER_FUSION_PTRS_EU,
                                                 repairCases[i].fuserId, PORT_FUSER_FUSION_RECORD_BYTES);

        CHECK_TRUE(stale == euRom + repairCases[i].e1Offset,
                   "E1 EU displacement resolves the exact byte-addressed record");
        CHECK_TRUE(corrected == euRom + repairCases[i].retailOffset,
                   "correct EU table resolves the exact byte-addressed retail record");
        CHECK_TRUE(stale != NULL && memcmp(stale, repairCases[i].e1Record, 7u) == 0,
                   "E1 EU record bytes match the reported fuser fixture");
        CHECK_TRUE(corrected != NULL && memcmp(corrected, repairCases[i].retailRecord, 7u) == 0,
                   "correct EU record bytes match retail");
        CHECK_TRUE(Port_ShouldRepairE1EuFuserSaveState(
                       1, 0, repairCases[i].fuserId, corrected, stale, 0u, repairCases[i].contaminatedOffer,
                       fusedBits, sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                   "generic E1 EU repair covers the exact 0x3D/0x5B/0x5D report fixture");
        CHECK_TRUE(!Port_ShouldRepairE1EuFuserSaveState(
                       1, 0, repairCases[i].fuserId, corrected, stale, 0u, repairCases[i].retailOffer,
                       fusedBits, sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                   "a correct retail EU offer is never repaired");
        CHECK_TRUE(!Port_ShouldRepairE1EuFuserSaveState(
                       0, 0, repairCases[i].fuserId, corrected, stale, 0u, repairCases[i].contaminatedOffer,
                       fusedBits, sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                   "the E1 EU migration never mutates a USA save");
        CHECK_TRUE(!Port_ShouldRepairE1EuFuserSaveState(
                       1, 1, repairCases[i].fuserId, corrected, stale, 0u, repairCases[i].contaminatedOffer,
                       fusedBits, sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
                   "the E1 EU migration never mutates a randomizer save");
    }

    /* 0x3D is the important odd-pointer fixture.  Its E1 record is RANDOM,
     * so a concrete shared offer (0x3C) is legitimate under E1, while it is
     * impossible at retail 0x3D's fixed 0x4D cursor and must be repaired. */
    CHECK_EQ(Port_ReadU32(euRom + PORT_FUSER_FUSION_PTRS_USA + 0x3Du * sizeof(u32)), 0x080020DFu,
             "E1 Tingle record keeps its odd data pointer");
    CHECK_EQ(Port_ReadU32(euRom + PORT_FUSER_FUSION_PTRS_EU + 0x3Du * sizeof(u32)), 0x08002211u,
             "retail Tingle record keeps its odd data pointer");
    CHECK_TRUE(Port_IsFuserSaveStateSemanticallyValid(
                   tingleE1, 0u, 0x3Cu, fusedBits, sizeof(fusedBits), sharedOffers, ARRAY_COUNT(sharedOffers)),
               "concrete shared offer 0x3C is valid under E1 Tingle's RANDOM cursor");
    CHECK_TRUE(!Port_IsFuserSaveStateSemanticallyValid(
                   tingleRetail, 0u, 0x3Cu, fusedBits, sizeof(fusedBits), sharedOffers,
                   ARRAY_COUNT(sharedOffers)),
               "the same shared offer is impossible under retail Tingle's fixed cursor");

    /* OLD-9: fuser 0x56 is byte-identical in USA and EU, at odd addresses in
     * both ROMs.  Its first byte is the retail story gate: no bubble through
     * global_progress 4, then availability begins exactly at 5. */
    InstallFuserRecord(euRom, PORT_FUSER_FUSION_PTRS_EU, 0x56u, 0x22D1u, forest56Retail);
    InstallFuserRecord(usaRom, PORT_FUSER_FUSION_PTRS_USA, 0x56u, 0x2229u, forest56Retail);
    eu56 = Port_ResolveFuserDataFromRom(euRom, sizeof(euRom), PORT_FUSER_FUSION_PTRS_EU, 0x56u,
                                        PORT_FUSER_FUSION_RECORD_BYTES);
    usa56 = Port_ResolveFuserDataFromRom(usaRom, sizeof(usaRom), PORT_FUSER_FUSION_PTRS_USA, 0x56u,
                                         PORT_FUSER_FUSION_RECORD_BYTES);
    CHECK_TRUE(eu56 == euRom + 0x22D1u && usa56 == usaRom + 0x2229u,
               "fuser 0x56 preserves the retail odd data address in both regions");
    CHECK_TRUE(eu56 != NULL && usa56 != NULL && memcmp(eu56, forest56Retail, 7u) == 0 &&
                   memcmp(usa56, forest56Retail, 7u) == 0 && memcmp(eu56, usa56, 7u) == 0,
               "fuser 0x56 is byte-identical in EU and USA retail ROMs");
    for (i = 0; i < 5u; ++i) {
        CHECK_TRUE(!IsUnlockedAtGlobalProgress(eu56, i),
                   "fuser 0x56 correctly has no bubble before global_progress 5");
    }
    CHECK_TRUE(IsUnlockedAtGlobalProgress(eu56, 5u),
               "fuser 0x56 unlocks exactly at global_progress 5");
    CHECK_TRUE(IsUnlockedAtGlobalProgress(eu56, 6u),
               "fuser 0x56 remains unlocked after its retail story gate");

    if (sFailures != 0) {
        fprintf(stderr, "port_kinstone_report_fixtures_test: %d failure(s)\n", sFailures);
        return 1;
    }
    puts("port_kinstone_report_fixtures_test: ALL PASS");
    return 0;
}
