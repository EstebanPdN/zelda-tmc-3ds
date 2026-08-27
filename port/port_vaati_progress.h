#ifndef PORT_VAATI_PROGRESS_H
#define PORT_VAATI_PROGRESS_H

#include "save.h"

/* Retail progression invariant for the Dark Hyrule Castle finale:
 * Vaati 2 completion implies that Vaati 1's intro flag is committed. */
bool32 Port_VaatiProgressBackupProvesLegacyRepair(const SaveFile* save, const SaveFile* backup);
bool32 Port_VaatiProgressNeedsRepair(const SaveFile* save, bool32 legacyRepairEvidence);
bool32 Port_RepairVaatiProgress(SaveFile* save, bool32 legacyRepairEvidence);

/* A freshly allocated Vaati Reborn is action 0 and its phase byte has not
 * been initialized yet. Only an already-running entity can be a restored
 * phase-3 defeat that needs to resume its death sequence. */
bool32 Port_VaatiRebornNeedsResumeDefeat(u32 action, u32 phase);

#endif /* PORT_VAATI_PROGRESS_H */
