#include "flags.h"
#include "manager.h"
#include "object.h"
#include "port_cloud_tops_fight.h"
#include "region.h"

#include <stdio.h>
#include <string.h>

int gActiveRegion;
static int sFailures;

u32 Port_RemapBaselineLocalFlag(u32 bankOffset, u32 ordinal) {
    if (bankOffset != FLAG_BANK_1 || (ordinal != KUMOUE_02_00 && ordinal != KUMOUE_02_02)) {
        fprintf(stderr, "unexpected remap request: bank=%u ordinal=%u\n", bankOffset, ordinal);
        sFailures++;
        return ordinal;
    }
    if (gActiveRegion == TMC_REGION_USA) return ordinal;
    return ordinal == KUMOUE_02_00 ? 0xf0 : 0xf2;
}

#define CHECK(condition, message)                  \
    do {                                           \
        if (!(condition)) {                        \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                           \
        }                                          \
    } while (0)

static const EntityData sUsaCloudTopsTopFight[PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT] = {
    { MANAGER, 0x00, FIGHT_MANAGER, 0x00, 0x0000000a, 0, 0, 0x80000000 },
    { OBJECT, 0x0f, CLOUD, 0x00, KUMOUE_02_00, 0x208, 0x038, 0x00000000 },
    { MANAGER, 0x1f, ENTITY_SPAWN_MANAGER, 0x0c, 0x00000000, 0, 0, KUMOUE_02_00 << 16 },
    { 0xff, 0x00, 0x00, 0x00, 0x00000000, 0, 0, 0x00000000 },
};

static const EntityData sUsaCloudTopsBottomFight[PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT] = {
    { MANAGER, 0x00, FIGHT_MANAGER, 0x00, 0x0000000b, 0, 0, 0x80080000 },
    { OBJECT, 0x0f, CLOUD, 0x00, 0x000008f5, 0x238, 0x2e8, 0x00000000 },
    { MANAGER, 0x1f, ENTITY_SPAWN_MANAGER, 0x0d, 0x00000000, 0, 0, KUMOUE_02_00 << 16 },
    { 0xff, 0x00, 0x00, 0x00, 0x00000000, 0, 0, 0x00000000 },
};

static void RunTopRegion(int region, u16 expectedFlag, const char* name) {
    EntityData patched[PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT];

    gActiveRegion = region;
    Port_CloudTopsPrepareTopFightEntities(patched, sUsaCloudTopsTopFight);

    CHECK((u16)patched[1].type2 == expectedFlag, name);
    CHECK((u16)(patched[2].spritePtr >> 16) == expectedFlag, name);
    CHECK(patched[0].type2 == sUsaCloudTopsTopFight[0].type2, "fight manager remains retail-identical");
    CHECK(patched[1].xPos == 0x208 && patched[1].yPos == 0x038, "cloud coordinates remain retail-identical");
    CHECK(patched[2].type == 0x0c, "spawn manager still loads property 12");
    CHECK(patched[3].kind == 0xff, "entity-list sentinel remains intact");
    CHECK(memcmp(sUsaCloudTopsTopFight, (const EntityData[]) {
              { MANAGER, 0x00, FIGHT_MANAGER, 0x00, 0x0000000a, 0, 0, 0x80000000 },
              { OBJECT, 0x0f, CLOUD, 0x00, KUMOUE_02_00, 0x208, 0x038, 0x00000000 },
              { MANAGER, 0x1f, ENTITY_SPAWN_MANAGER, 0x0c, 0x00000000, 0, 0, KUMOUE_02_00 << 16 },
              { 0xff, 0x00, 0x00, 0x00, 0x00000000, 0, 0, 0x00000000 },
          }, sizeof(sUsaCloudTopsTopFight)) == 0,
          "USA template is never mutated");
}

static void RunBottomRegion(int region, u16 expectedCloudFlag, u16 expectedSpawnFlag, const char* name) {
    EntityData patched[PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT];

    gActiveRegion = region;
    Port_CloudTopsPrepareBottomFightEntities(patched, sUsaCloudTopsBottomFight);

    CHECK((patched[1].type2 & 0xffu) == expectedCloudFlag, name);
    CHECK((u16)(patched[2].spritePtr >> 16) == expectedSpawnFlag, name);
    CHECK(patched[0].type2 == sUsaCloudTopsBottomFight[0].type2, "bottom fight manager remains retail-identical");
    CHECK(patched[1].xPos == 0x238 && patched[1].yPos == 0x2e8, "bottom cloud coordinates remain retail-identical");
    CHECK(patched[2].type == 0x0d, "bottom spawn manager still loads property 13");
    CHECK(patched[3].kind == 0xff, "bottom entity-list sentinel remains intact");
    CHECK(memcmp(sUsaCloudTopsBottomFight, (const EntityData[]) {
              { MANAGER, 0x00, FIGHT_MANAGER, 0x00, 0x0000000b, 0, 0, 0x80080000 },
              { OBJECT, 0x0f, CLOUD, 0x00, 0x000008f5, 0x238, 0x2e8, 0x00000000 },
              { MANAGER, 0x1f, ENTITY_SPAWN_MANAGER, 0x0d, 0x00000000, 0, 0, KUMOUE_02_00 << 16 },
              { 0xff, 0x00, 0x00, 0x00, 0x00000000, 0, 0, 0x00000000 },
          }, sizeof(sUsaCloudTopsBottomFight)) == 0,
          "bottom USA template is never mutated");
}

int main(void) {
    RunTopRegion(TMC_REGION_USA, KUMOUE_02_00, "top USA keeps retail ordinal 243");
    RunTopRegion(TMC_REGION_EU, 0xf0, "top EU uses the ROM-native ordinal 240");
    RunTopRegion(TMC_REGION_JP, 0xf0, "top JP uses the ROM-native ordinal 240");
    RunBottomRegion(TMC_REGION_USA, KUMOUE_02_02, KUMOUE_02_00,
                    "bottom USA keeps retail ordinals 245 and 243");
    RunBottomRegion(TMC_REGION_EU, 0xf2, 0xf0,
                    "bottom EU uses ROM-native ordinals 242 and 240");
    RunBottomRegion(TMC_REGION_JP, 0xf2, 0xf0,
                    "bottom JP uses ROM-native ordinals 242 and 240");

    if (sFailures != 0) return 1;
    puts("port_cloud_tops_fight_test: ALL PASS");
    return 0;
}
