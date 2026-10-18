#include "definitions.h"

// TODO deduplicate using sprite indices enum

#define MULTI_FORM(definition_ptr)         \
    {                                      \
        { 2, 0, 0, 0, 0, 0, 0 }, {         \
            .definition = (definition_ptr) \
        }                                  \
    }

/*
 * NPC definitions diverge between EU and USA/JP (sprite indices, and two
 * EU-specific sub-tables). The data section lives in npcDefinitions_data.inc
 * and is compiled with two knobs (see objectDefinitions.c for the same idiom):
 *   - NAME(x)        — symbol-name decoration (identity, or *_eu twin)
 *   - NPCDEF_EU_PASS — when defined, the .inc emits EU-specific bodies
 *
 * Native EU builds set NPCDEF_EU_PASS so the canonical table holds EU data.
 * The multi-region fat binary (MULTI_REGION, USA/JP baseline) emits the USA/JP
 * table into the canonical symbols AND a second EU table into *_eu symbols;
 * npcUtils.c selects gNPCDefinitions_eu at runtime when an EU ROM is active.
 */

#if defined(EU) && !defined(MULTI_REGION)
#define NPCDEF_EU_PASS
#endif

#define NAME(x) x
#include "npcDefinitions_data.inc"
#undef NAME

#ifdef MULTI_REGION
#define NAME(x) x##_eu
#define NPCDEF_EU_PASS
#include "npcDefinitions_data.inc"
#undef NPCDEF_EU_PASS
#undef NAME
#endif
