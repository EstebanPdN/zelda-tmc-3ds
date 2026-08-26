#ifndef PORT_VAATI_PROGRESS_H
#define PORT_VAATI_PROGRESS_H

#include "save.h"

/*
 * Retail progression invariant for the Dark Hyrule Castle finale:
 * defeating Vaati 2 is only reachable after Vaati 1's intro was committed.
 * A missing intro flag with the later completion flag set makes the room
 * loader re-create mutually exclusive cutscenes and can loop Vaati 2.
 */
bool32 Port_VaatiProgressNeedsRepair(const SaveFile* save);
bool32 Port_RepairVaatiProgress(SaveFile* save);

#endif /* PORT_VAATI_PROGRESS_H */
