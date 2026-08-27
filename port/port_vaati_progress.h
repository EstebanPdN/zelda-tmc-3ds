#ifndef PORT_VAATI_PROGRESS_H
#define PORT_VAATI_PROGRESS_H

#include "save.h"

/*
 * Retail progression invariant for the Dark Hyrule Castle finale:
 * defeating Vaati 2 is only reachable after Vaati 1's intro was committed.
 * A premature Vaati 2 completion flag skips Vaati 1 and loops the second
 * phase. E10 attempted the opposite repair; its permanent backup lets this
 * version identify and safely undo that legacy mutation.
 */
bool32 Port_VaatiProgressBackupProvesLegacyRepair(const SaveFile* save, const SaveFile* backup);
bool32 Port_VaatiProgressNeedsRepair(const SaveFile* save, bool32 legacyRepairEvidence);
bool32 Port_RepairVaatiProgress(SaveFile* save, bool32 legacyRepairEvidence);

#endif /* PORT_VAATI_PROGRESS_H */
