#include "port_asset_loader.h"
#include "port_rom.h"

#include <stdint.h>

extern u8* gRomData;
extern u32 gRomSize;

static bool32 IsRomBufferRange(const void* ptr, u32 size) {
    if (!ptr || !gRomData) return 0;
    const uintptr_t begin = (uintptr_t)gRomData;
    const uintptr_t end = begin + gRomSize;
    const uintptr_t at = (uintptr_t)ptr;
    return at >= begin && at <= end && size <= end - at;
}

bool32 Port_LoadPaletteGroupFromAssets(u32 group) { (void)group; return 0; }
bool32 Port_LoadGfxGroupFromAssets(u32 group) { (void)group; return 0; }
bool32 Port_LoadAreaTablesFromAssets(void) { return 0; }
bool32 Port_LoadSpritePtrsFromAssets(void) { return 0; }
bool32 Port_LoadTextsFromAssets(void) { return 0; }
bool32 Port_AreSpritePtrsLoadedFromAssets(void) { return 0; }
void Port_LogAssetLoaderStatus(void) {}
void Port_LogTextLookup(u32 langIndex, u32 textIndex) { (void)langIndex; (void)textIndex; }
bool32 Port_RefreshAreaDataFromAssets(u32 area) { (void)area; return 0; }
bool32 Port_IsAreaTablePtrFromAssets(u32 area, const void* ptr) { (void)area; (void)ptr; return 0; }
bool32 Port_IsRoomHeaderPtrReadable(const void* ptr) { return ptr != 0; }
bool32 Port_IsLoadedAssetBytes(const void* ptr, u32 size) { return IsRomBufferRange(ptr, size); }
const u8* Port_LoadedAssetBytesEnd(const void* ptr) {
    return IsRomBufferRange(ptr, 0) ? gRomData + gRomSize : 0;
}
const u8* Port_GetMapAssetDataByIndex(u32 assetIndex, u32* size) { (void)assetIndex; if (size) *size = 0; return 0; }
const u8* Port_GetSpriteAnimationData(u16 spriteIndex, u32 animIndex) {
    const SpritePtr* sprite = Port_GetSpritePtr(spriteIndex);
    const u8* animationTable;
    u32 animationAddress;
    u32 tableOffset;

    if (!sprite || !sprite->animations) return 0;
    if (animIndex >= gRomSize / sizeof(u32)) return 0;

    /* The slim 3DS build has no extracted asset cache, so animation tables
     * must follow their packed GBA addresses back into the loaded ROM. */
    tableOffset = animIndex * sizeof(u32);
    animationTable = (const u8*)sprite->animations;
    if (!IsRomBufferRange(animationTable + tableOffset, sizeof(u32))) return 0;

    animationAddress = Port_ReadU32(animationTable + tableOffset);
    if (animationAddress < 0x08000000u || animationAddress >= 0x08000000u + gRomSize) return 0;

    return (const u8*)Port_ResolveRomData(animationAddress);
}
int Port_MountAssetPaks(const char* root) { (void)root; return 0; }
void Port_UnmountAssetPaks(void) {}
bool32 Port_PaksMounted(void) { return 0; }
int Port_PakEntryCount(void) { return 0; }
void Port_AssetLoader_Reload(void) {}
