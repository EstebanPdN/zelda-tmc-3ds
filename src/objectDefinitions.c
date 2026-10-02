#include "entity.h"
#include "hitbox.h"
#include "definitions.h"

// TODO
const Hitbox* const gObjectHitboxes[] = { NULL, &gHitbox_0, &gHitbox_30, &gHitbox_2, &gHitbox_3 };

#define MULTI_FORM(definition_ptr)       \
    {                                    \
        { 2, 0, 0, 0, 0, 0, 0 }, {       \
            .definition = definition_ptr \
        }                                \
    }

/*
 * Object definitions diverge between EU and USA/JP (sprite indices, and a few
 * object/sub-table variants). The data section lives in objectDefinitions_data.inc
 * and is compiled with two knobs:
 *   - NAME(x)        — symbol-name decoration (identity, or *_eu twin)
 *   - OBJDEF_EU_PASS — when defined, the .inc emits EU-specific bodies
 *
 * Native single-region builds emit one table into the canonical symbols
 * (EU build sets OBJDEF_EU_PASS so the canonical table holds EU data).
 *
 * The multi-region fat binary (MULTI_REGION, USA/JP baseline) emits the USA/JP table into the
 * canonical symbols AND a second EU table into *_eu symbols; the consumer in
 * objectUtils.c selects gObjectDefinitions_eu at runtime when an EU ROM is active.
 */

#if defined(EU) && !defined(MULTI_REGION)
#define OBJDEF_EU_PASS
#endif

#define NAME(x) x
#include "objectDefinitions_data.inc"
#undef NAME

#ifdef MULTI_REGION
#define NAME(x) x##_eu
#define OBJDEF_EU_PASS
#include "objectDefinitions_data.inc"
#undef OBJDEF_EU_PASS
#undef NAME
#endif
