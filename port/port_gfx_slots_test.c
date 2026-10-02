#include "common.h"
#include "fileselect.h"
#include "main.h"
#include "structures.h"
#include "vram.h"
#include "port_config.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
GfxSlotList gGFXSlots;
OAMControls gOAMControls;
LinkedList gEntityLists[9];
PlayerEntity gPlayerEntity;
GenericEntity gAuxPlayerEntities[MAX_AUX_PLAYER_ENTITIES], gEntities[MAX_ENTITIES];
struct_gUnk_020000C0 gUnk_020000C0[0x30];
int gActiveRegion;
RomRegion gRomRegion;
static const u8 data[64];
const u8* gGlobalGfxAndPalettes = data;
u32 gFixedTypeGfxData[1024];
void MemClear(void* d, u32 n) { memset(d, 0, n); }
void CleanUpGFXSlots(void);
u32 FindFreeGFXSlots(u32);
void ReserveGFXSlots(u32, u32, u32);

int main(void) {
    Entity request = {0};
    for (unsigned region = 0; region < 2; ++region) {
        gActiveRegion = region ? TMC_REGION_EU : TMC_REGION_USA;
        gRomRegion = region ? ROM_REGION_EU : ROM_REGION_USA;
        ResetPalettes();
        gGFXSlots.unk0 = 1;
        for (unsigned i = 4; i < MAX_GFX_SLOTS; ++i)
            ReserveGFXSlots(i, i, 1);
        GfxSlotList full = gGFXSlots;
        CleanUpGFXSlots();
        CHECK(memcmp(&full, &gGFXSlots, sizeof(full)) == 0);
        CHECK(!LoadSwapGFX(&request, 1, 0));
        CHECK(memcmp(&full, &gGFXSlots, sizeof(full)) == 0);
        /* Reclaim an interior hole and relocate the final live slot. */
        for (unsigned i = 4; i < MAX_GFX_SLOTS - 1; ++i)
            gGFXSlots.slots[i].status = i & 1 ? GFX_SLOT_UNLOADED : GFX_SLOT_FREE;
        CHECK(FindFreeGFXSlots(20) == 4);
        Entity* live = &gEntities[0].base;
        for (unsigned i = 0; i < 9; ++i) {
            gEntityLists[i].first = gEntityLists[i].last = (Entity*)&gEntityLists[i];
        }
        gEntityLists[0].first = gEntityLists[0].last = live;
        live->next = live->prev = (Entity*)&gEntityLists[0];
        live->kind = NPC;
        Entity* suspended = &gEntities[1].base;
        suspended->next = suspended;
        suspended->spriteAnimation[0] = MAX_GFX_SLOTS - 1;
        suspended->spriteVramOffset = 0x140 + (MAX_GFX_SLOTS - 1) * 16;
        live->spriteAnimation[0] = MAX_GFX_SLOTS - 1;
        live->spriteVramOffset = 0x140 + (MAX_GFX_SLOTS - 1) * 16;
        live->spriteAnimation[2] = 1;
        suspended->spriteAnimation[2] = 2;
        memset(gUnk_020000C0, 0, sizeof(gUnk_020000C0));
        for (unsigned extra=1; extra<=2; ++extra) {
            *(u8*)&gUnk_020000C0[extra].unk_00[0].unk_00 = 1;
            gUnk_020000C0[extra].unk_00[0].unk_08.HALF_U.HI = live->spriteVramOffset;
        }
        gOAMControls.oam[0].tileNum = live->spriteVramOffset;
        CleanUpGFXSlots();
        CHECK(gUnk_020000C0[1].unk_00[0].unk_08.HALF_U.HI == 0x180);
        CHECK(gUnk_020000C0[2].unk_00[0].unk_08.HALF_U.HI == suspended->spriteVramOffset);
        CHECK(gOAMControls.oam[0].tileNum == 0x180);
        CHECK(live->spriteAnimation[0] == 4);
        CHECK(suspended->spriteAnimation[0] == MAX_GFX_SLOTS - 1);
        CHECK(suspended->spriteVramOffset == 0x140 + (MAX_GFX_SLOTS - 1) * 16);
        CHECK(live->spriteVramOffset == 0x180);
        CHECK(gGFXSlots.slots[4].gfxIndex == MAX_GFX_SLOTS - 1);
        CHECK(gGFXSlots.unk_3 == 1);
        CHECK(LoadSwapGFX(&request, 20, 0));
        CHECK(request.spriteAnimation[0] == 5);
        CHECK(!LoadSwapGFX(&request, 0, 0));
        CHECK(!LoadSwapGFX(&request, 40, 43));
        for (unsigned i = 0; i < 4; ++i)
            CHECK(gGFXSlots.slots[i].status == GFX_SLOT_PALETTE);
        CHECK(LoadSwapGFX(&request, 1, 3)); /* Dedicated player-item slot is valid. */
        CHECK(gGFXSlots.slots[3].status == GFX_SLOT_PALETTE);
        /* A fragmented full allocator must compact and retry in EU too. */
        ResetPalettes();
        gGFXSlots.unk0 = 1;
        memset(gEntities, 0, sizeof(gEntities));
        for (unsigned i = 4; i < MAX_GFX_SLOTS; i += 2)
            ReserveGFXSlots(i, i, 1);
        CHECK(LoadSwapGFX(&request, 12, 0));
        CHECK(request.spriteAnimation[0] == 24);
    }
    puts("port_gfx_slots_test: full/fragmented/last-slot/invalid allocations USA+EU PASS");
    return 0;
}
