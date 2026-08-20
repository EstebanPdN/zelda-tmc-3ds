#ifndef TMC_BOTTOM_MAP_ANIM_3DS_H
#define TMC_BOTTOM_MAP_ANIM_3DS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MAP-tab repaint skipping.
 *
 * The bottom screen repainted on every cadence tick and never skipped: a dump
 * measured 2230 paints at 11.679 ms with 0 static skips, about 1.95 ms per
 * presented frame, on a build whose average frame interval overran 60 Hz by
 * 1.55 ms. The picture is usually identical between those paints, so the work
 * is mostly wasted -- but only *mostly*, because a handful of MAP elements
 * animate purely from the tick.
 *
 * These helpers reduce a tick to the quantised value of every MAP animation,
 * so a paint can be skipped when the next tick would draw the same picture.
 *
 * Two properties matter more than the saving, and both are covered by
 * bottom_map_anim_3ds_test:
 *
 *   1. The tick handed in MUST be free-running. When it advanced only on an
 *      actual paint, the signature was self-referential: skip one paint, the
 *      tick freezes, the signature can never change again, and the animation
 *      stops permanently instead of merely going stale.
 *   2. No signature over the MAP tab can be complete. Several inputs are live
 *      engine globals sampled during the paint (gAreaRoomHeaders through
 *      GetRegionGeometry, GetRoomProperty/gAreaTable, and the lazy
 *      Port_GetSpriteSizeTable latch). So the predicate also forces a repaint
 *      on a fixed period: bounded staleness, rather than a complete signature
 *      whose failure mode is a frozen screen.
 */

/* Repaint at least this often regardless of the signature. ~0.8 s at the
 * 20 Hz bottom-screen paint cadence. */
#define BOTTOM_MAP_ANIM_FORCE_TICKS 16u

/* Quantised value of every tick-driven MAP animation. Equal signatures mean
 * the two ticks draw the same picture, given the same engine snapshot.
 * `isDungeon` selects the dungeon terms; width/height are the paint surface
 * so the pulse term can be quantised at the same `u` the painter uses. */
uint32_t BottomMapAnim_Signature(uint32_t tick, int isDungeon, int32_t width, int32_t height);

/* Whether a cadence-due MAP paint at `tick` must actually run, given that the
 * last scheduled paint used `paintedTick`. Engine-state changes are handled
 * separately by the caller's snapshot comparison; this covers animation only. */
int BottomMapAnim_NeedsPaint(uint32_t tick, uint32_t paintedTick, int isDungeon, int32_t width,
                             int32_t height);

#ifdef __cplusplus
}
#endif

#endif /* TMC_BOTTOM_MAP_ANIM_3DS_H */
