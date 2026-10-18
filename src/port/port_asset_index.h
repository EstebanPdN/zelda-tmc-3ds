#pragma once

#include "global.h"

typedef struct {
    u32 offset;
    u32 size;
    const char* path;
} EmbeddedAssetEntry;

const EmbeddedAssetEntry* EmbeddedAssetIndex_Get(void);
u32 EmbeddedAssetIndex_Count(void);
