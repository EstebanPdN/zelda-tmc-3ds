#include "port_second_screen_state.h"
#include "area.h"
#include "game.h"
#include "item.h"
#include "itemMetaData.h"
#include "main.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
Main gMain; UI gUI; Area gArea; RoomControls gRoomControls;
PlayerEntity gPlayerEntity; PlayerState gPlayerState; SaveFile gSave; HUD gHUD;
u8 gEwram[0x40000];
const u8 gUnk_080C9044[8] = {0};
const ItemMetaData gItemMetaData[256] = {{0}};
const Wallet gWalletSizes[] = {{100,0},{300,0},{500,0},{999,0}};
const u8 gBombBagSizes[] = {10,30,50,99}, gQuiverSizes[] = {30,50,70,99};
void Port_SecondScreen_3DS_LockSnapshot(void) { }
void Port_SecondScreen_3DS_UnlockSnapshot(void) { }
u32 GetInventoryValue(u32 item) { return item == ITEM_BOOMERANG; }
void ForceEquipItem(u32 item, u32 slot) { gSave.stats.equipped[slot] = item; }
bool Port_Config_GetHideTopHud(void) { return false; }
u32 sub_080A6F40(void) { return 0xffff; }
int main(void) {
    SecondScreenSnapshot original, next;
    gMain.task = TASK_GAME; gMain.substate = GAMEMAIN_UPDATE;
    gRoomControls.area = 0x48; gRoomControls.room = 5;
    gArea.areaMetadata = SECOND_SCREEN_AR_IS_DUNGEON | SECOND_SCREEN_AR_HAS_KEYS;
    gArea.dungeon_idx = 2; gArea.roomResInfos[5].pixel_width = 240;
    gPlayerEntity.base.x.HALF.HI=321; gPlayerEntity.base.y.HALF.HI=654;
    gSave.dungeonKeys[2]=3; gSave.stats.rupees=100;
    Port_SecondScreenState_Publish(); Port_SecondScreenState_Read(&original);
    CHECK(original.area==0x48 && original.room==5 && original.visitedMask==(1ull<<5));
    gMain.substate=GAMEMAIN_SUBTASK;
    for (unsigned phase=1;phase<=4;++phase) {
        gUI.nextToLoad=phase;
        memset(&gRoomControls,0,sizeof(gRoomControls)); memset(&gArea,0,sizeof(gArea));
        gRoomControls.area=0x2f; gRoomControls.room=1;
        gArea.dungeon_idx=9; gPlayerEntity.base.x.HALF.HI=999;
        gSave.stats.rupees=200+phase; gSave.dungeonKeys[2]=4+phase;
        Port_SecondScreenState_Publish(); Port_SecondScreenState_Read(&next);
        CHECK(next.area==original.area && next.room==original.room);
        CHECK(next.playerX==321 && next.playerY==654 && next.dungeonIdx==2);
        CHECK(next.visitedMask==original.visitedMask);
        CHECK(memcmp(next.rooms,original.rooms,sizeof(next.rooms))==0);
        CHECK(next.rupees==200+phase && next.dungeonKeys==4+phase);
    }
    gUI.nextToLoad=0; gMain.substate=GAMEMAIN_UPDATE;
    gRoomControls.area=3; gRoomControls.room=7;
    Port_SecondScreenState_Publish(); Port_SecondScreenState_Read(&next);
    CHECK(next.area==3 && next.room==7 && next.playerX==999);
    gRoomControls.area=0x2f; gRoomControls.room=2;
    Port_SecondScreenState_Publish(); Port_SecondScreenState_Read(&next);
    CHECK(next.visitedMask==(1ull<<2)); /* Auxiliary room 1 was not visited. */
    gMain.task=TASK_TITLE;
    Port_SecondScreenState_Publish(); Port_SecondScreenState_Read(&next); CHECK(!next.inGame);
    puts("port_second_screen_state_test: ALL PASS"); return 0;
}
