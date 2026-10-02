#ifndef REGIONTRIGGERMANAGER_H
#define REGIONTRIGGERMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    u8 unk_20[0x14]; // unused
    u16 radiusX;
    u16 radiusY;
    s16 posX;
    s16 posY;
    u8 unk_3c[2]; // unused
    u16 playerInRegionFlag;
} RegionTriggerManager;

PORT_STATIC_ASSERT_SIZE(RegionTriggerManager, 0x40, 0x58, "RegionTriggerManager size incorrect");
PORT_STATIC_ASSERT_OFFSET(RegionTriggerManager, radiusX, 0x34, 0x4c, "RegionTriggerManager radiusX offset incorrect");
PORT_STATIC_ASSERT_OFFSET(RegionTriggerManager, posX, 0x38, 0x50, "RegionTriggerManager posX offset incorrect");
PORT_STATIC_ASSERT_OFFSET(RegionTriggerManager, playerInRegionFlag, 0x3e, 0x56,
                          "RegionTriggerManager flag offset incorrect");

#endif // REGIONTRIGGERMANAGER_H
