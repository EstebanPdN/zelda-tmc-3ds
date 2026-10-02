#ifndef PORT_BOMB_COMPAT_H
#define PORT_BOMB_COMPAT_H
#include "save.h"
bool32 Port_BombInventoryNeedsRepair(const SaveFile* save, bool32 nonstandardProfile);
bool32 Port_RepairBombInventory(SaveFile* save, bool32 nonstandardProfile);
#endif
