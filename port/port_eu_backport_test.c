/* Exercise the native callbacks and metadata, without a ROM or patched assets. */
#include "common.h"
#include "area.h"
#include "flags.h"
#include "game.h"
#include "object.h"
#include "item.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "script.h"
#include "message.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
int gActiveRegion;
Area gArea;
SaveFile gSave;
static unsigned marked, bags, usaBags, wallet, bought, boomerang;
EntityData gUnk_080F5758, gUnk_080F57E8, gUnk_080F57A8, gUnk_080F57C8,
 gUnk_080F5888, gUnk_080F58A8, gUnk_080F5868, gUnk_080F5828, gUnk_080F5848,
 gUnk_080F5788, gUnk_080F66AC;
void MarkFuserDone(Entity* ent) { ++marked; }
u32 GetInventoryValue(u32 item) { return item == ITEM_BOOMERANG ? boomerang : 0; }
u32 CheckLocalFlag(u32 flag) { return flag == SHOP00_SAIFU ? wallet : flag == SHOP00_BOMBBAG ? bought : 0; }
u32 CheckLocalFlagByBank(u32 bank, u32 flag) { return 0; }
u32 CheckLocalFlagB(u32 flag) { return CheckLocalFlag(flag); }
bool32 CheckLocalFlagByBankB(u32 bank, u32 flag) { return 0; }
u32 CheckGlobalFlag(u32 flag) { return 0; }
void LoadStaticBackground(u32 index) { }
void SetWorldMapPos(u32 area, u32 room, u32 x, u32 y) { }
void LoadRoomEntityList(const EntityData* list) {
    if (list == &gUnk_080F58A8) { ++usaBags; return; }
    if (list->kind == OBJECT && list->id == SHOP_ITEM && list->type == ITEM_BOMBBAG) {
        ++bags;
        if (list[1].kind != 0xff) bags = 999;
    }
}
void Farmers_MarkEenieFuserDoner(Entity*, ScriptExecutionContext*);
void sub_StateChange_HouseInteriors3_StockwellShop(void);
void sub_StateChange_WindTribeTowerRoof_Main(void);
int main(void) {
    for (unsigned region = 0; region < 2; ++region) {
        gActiveRegion = region;
        Entity eenie = {0}; ScriptExecutionContext ctx = {0};
        marked = 0;
        Farmers_MarkEenieFuserDoner(&eenie, &ctx); CHECK(marked == 0);
        ctx.condition = 1;
        Farmers_MarkEenieFuserDoner(&eenie, &ctx); CHECK(marked == 1);
        eenie.type = 1;
        Farmers_MarkEenieFuserDoner(&eenie, &ctx); CHECK(marked == 1);
        for (wallet=0; wallet<=1; ++wallet) for (boomerang=0; boomerang<=1; ++boomerang)
        for (bought=0; bought<=1; ++bought) {
            bags = usaBags = 0;
            sub_StateChange_HouseInteriors3_StockwellShop();
            const unsigned expected = wallet && boomerang && !bought;
            CHECK(bags == (region == TMC_REGION_EU ? expected : 0));
            CHECK(usaBags == (region == TMC_REGION_USA ? expected : 0));
        }
        gArea.areaMetadata = 0;
        sub_StateChange_WindTribeTowerRoof_Main();
        CHECK(gArea.areaMetadata == AR_ALLOWS_WARP);
        CHECK(GetItemPrice(ITEM_BOMBBAG) == 600);
        CHECK(GetSaleItemConfirmMessageID(ITEM_BOMBBAG) == TEXT_INDEX(TEXT_STOCKWELL,0x25));
    }
    CHECK(gUnk_080FD964_eu[ITEM_BOMBBAG].gotItemMessageId == TEXT_INDEX(TEXT_ITEM_GET,7));
    CHECK(gUnk_080FD964[ITEM_BOMBBAG].gotItemMessageId == TEXT_INDEX(TEXT_ITEM_GET,0x63));
    puts("port_eu_backport_test: ALL PASS"); return 0;
}
