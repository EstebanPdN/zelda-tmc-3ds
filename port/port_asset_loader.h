#pragma once

#ifdef __cplusplus
#include <filesystem>

/* C++-only hooks used by the mod loader to control explicit mod directories.
 * The C asset API below remains stable for game/port C code. */
void Port_SetModsExplicitSelection(bool explicitSelection);
void Port_AddModRoot(const std::filesystem::path& modRoot);

extern "C" {
#endif

#include "global.h"
bool32 Port_LoadPaletteGroupFromAssets(u32 group);
bool32 Port_LoadGfxGroupFromAssets(u32 group);
bool32 Port_LoadAreaTablesFromAssets(void);
bool32 Port_LoadSpritePtrsFromAssets(void);
bool32 Port_LoadTextsFromAssets(void);
bool32 Port_AreSpritePtrsLoadedFromAssets(void);
void Port_LogAssetLoaderStatus(void);
void Port_LogTextLookup(u32 langIndex, u32 textIndex);
bool32 Port_RefreshAreaDataFromAssets(u32 area);
bool32 Port_IsAreaTablePtrFromAssets(u32 area, const void* ptr);
bool32 Port_IsRoomHeaderPtrReadable(const void* ptr);
bool32 Port_IsLoadedAssetBytes(const void* ptr, u32 size);
const u8* Port_LoadedAssetBytesEnd(const void* ptr);
const u8* Port_GetMapAssetDataByIndex(u32 assetIndex, u32* size);
const u8* Port_GetSpriteAnimationData(u16 spriteIndex, u32 animIndex);

/* Try to mmap every assets dir's .pak file under the given path.
 * Returns the number of archives successfully mounted (0 if pak
 * loading is disabled, no .pak files exist, or any open failed). */
int Port_MountAssetPaks(const char* assetsRoot);

/* Unmount any previously-mounted paks and switch the loader back
 * into loose-file mode. Safe to call when nothing is mounted. */
void Port_UnmountAssetPaks(void);

/* True if at least one pak is currently mounted. */
bool32 Port_PaksMounted(void);

/* Total number of entries across all mounted paks. */
int Port_PakEntryCount(void);

/* Drop the cached asset-group state and re-scan from disk. tmc_pc
 * calls this after a fresh first-launch extraction so the engine
 * picks up assets that didn't exist when Port_LoadRom first probed
 * the disk. Idempotent. */
void Port_AssetLoader_Reload(void);

#ifdef __cplusplus
}
#endif
