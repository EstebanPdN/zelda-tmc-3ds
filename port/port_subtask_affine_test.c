/* Exercise the production menu lifecycle and affine allocator together. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#undef static_assert /* The GBA headers supply their portable definition. */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "subtask.h"
#include "game.h"
#include "fade.h"
#include "main.h"
#include "screen.h"
#include "ui.h"
#include "affine.h"

OAMControls gOAMControls;
UI gUI;
Main gMain;
Screen gScreen, gUnk_03001020;
FadeControl gFadeControl;
u16 gPaletteBuffer[0x200];
u8 gPaletteBufferBackup[0x400], gUnk_03000420[0x800];
GfxSlotList gGFXSlots;
Palette gPaletteList[16];
RoomControls gRoomControls;
RoomTransition gRoomTransition;
ActiveScriptInfo gActiveScriptInfo;
PlayerState gPlayerState;
MapLayer gMapBottom, gMapTop;
Area gArea;
void** gCurrentRoomProperties;

void MemCopy(const void* s, void* d, u32 n) { memcpy(d, s, n); }
void MemClear(void* d, u32 n) { memset(d, 0, n); }
void Port_LogSubtaskEntry(const char* n, unsigned a, unsigned b) {}
void sub_0805E958(void) {}
void sub_0805E974(void) {}
void DeleteAllEntities(void) {}
u32 GetFlagBankOffset(u32 x) { return 0; }
RoomResInfo* GetCurrentRoomInfo(void) { return NULL; }
void RestoreGameTask(bool32 x) {}
void RollingBarrelManager_OnEnterRoom(void) {}
void DisableVBlankDMA(void) {}
Entity* FindEntityByID(u32 a, u32 b, u32 c) { return NULL; }
void AnimatedBackgroundManager_RestoreBgGfx(void* p) {}
void sub_0801D000(u32 x) {}
void SetFade(u32 a, u32 b) {}
void SetFadeInverted(u32 x) {}
void SetInitializationPriority(void) {}
void FlushSprites(void) {}
void UpdateEntities(void) {}
void UpdateManagers(void) {}
void DrawUI(void) {}
void DrawUIElementsGameplay(void) {}
void CopyOAM(void) {}
void DrawEntities(void) {}
void UpdateCarriedObject(void) {}
void Subtask_FadeIn(void);
void Subtask_FadeOut(void);

int main(void) {
    Entity existing[4] = {0}, temporary = {0}, next = {0};
    unsigned char expected[0x100];
    for (unsigned cycle = 0; cycle < 256; ++cycle) {
        memset(&gOAMControls, 0, sizeof(gOAMControls));
        memset(existing, 0, sizeof(existing));
        for (unsigned i = 0; i < 4; ++i)
            assert(SetAffineInfo(&existing[i], 0x80 + i * 0x20, 0x100 + cycle, cycle << 8));
        memcpy(expected, gOAMControls.unk, sizeof(expected));
        Subtask_FadeIn();
        assert(memcmp(gUI.unk_2a8, expected, sizeof(expected)) == 0);
        /* Menu initialization clears the real affine table and menu effects
         * then reuse those slots, exactly as Subtask_Init does. */
        memset(gOAMControls.unk, 0, 0x100);
        memset(&temporary, 0, sizeof(temporary));
        assert(SetAffineInfo(&temporary, 0x777, 0x999, 0));
        Subtask_FadeOut();
        assert(memcmp(gOAMControls.unk, expected, sizeof(expected)) == 0);
        memset(&next, 0, sizeof(next));
        assert(SetAffineInfo(&next, 0x100, 0x100, 0));
        assert(next.spriteOrientation.b1 == 5);
        for (unsigned i = 0; i < 4; ++i)
            assert(existing[i].spriteOrientation.b1 == i + 1);
    }
    puts("port_subtask_affine_test: 256 production save/restore cycles PASS");
    return 0;
}
