/*
 * MAP-tab repaint-skip regression test.
 *
 * Guards the three ways this optimisation can go wrong:
 *   - the signature misses an animation, so the screen goes stale or freezes;
 *   - the hash collides, so a real animation change is skipped;
 *   - the skip loop stops producing paints entirely (the self-referential
 *     tick trap that this design exists to avoid).
 */
#include "bottom_map_anim_3ds.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define W 320
#define H 240

/*
 * Reference terms, re-derived here independently of the implementation so the
 * two must agree. Each mirrors one expression in the draw code.
 */
static uint32_t RefBlink(uint32_t tick) {
    /* port_second_screen.c:913, :1164, :1331; dungeonmap.c:499 */
    return (tick & 8u) ? 1u : 0u;
}

static uint32_t RefPalette(uint32_t tick) {
    /* port_second_screen_dungeonmap.c:424 */
    return 8u + (((tick * 3u) >> 3) & 7u);
}

static int32_t RefPulse(uint32_t tick) {
    /* port_second_screen.c:1161 */
    const float u = (float)(W < H ? W : H) / 720.0f;
    return (int32_t)((6.5f + 1.5f * sinf((float)(tick % 32u) * (6.28318f / 32.0f))) * u);
}

static int SameDrawnPicture(uint32_t a, uint32_t b, int isDungeon) {
    if (RefBlink(a) != RefBlink(b)) return 0;
    if (isDungeon) return RefPalette(a) == RefPalette(b);
    return RefPulse(a) == RefPulse(b);
}

/* The signature must partition ticks exactly as the drawn picture does: no
 * missed animation (would freeze/stale the screen) and no hash collision
 * (would skip a real change). Checked over 256 ticks, several full periods of
 * every term (blink 16, pulse 32, palette 64). */
static void TestSignatureMatchesDrawnPicture(int isDungeon) {
    for (uint32_t a = 0; a < 256u; ++a) {
        for (uint32_t b = 0; b < 256u; ++b) {
            const int sameSig = BottomMapAnim_Signature(a, isDungeon, W, H) ==
                                BottomMapAnim_Signature(b, isDungeon, W, H);
            assert(sameSig == SameDrawnPicture(a, b, isDungeon));
        }
    }
}

/* The pulse term only earns its place if it actually collapses at the bottom
 * screen's u. If it ever takes many values, quantising it stops being a
 * saving and the comment in the implementation is wrong. */
static void TestPulseCollapses(void) {
    int seen[8] = { 0 };
    int distinct = 0;
    for (uint32_t t = 0; t < 32u; ++t) {
        const int32_t v = RefPulse(t);
        assert(v >= 0 && v < 8);
        if (!seen[v]) {
            seen[v] = 1;
            ++distinct;
        }
    }
    assert(distinct == 2);
}

/* Drive the real scheduling loop with a free-running tick and confirm it keeps
 * painting, never stalls longer than the forced period, and actually saves
 * work. Returns the paint count. */
static unsigned RunSkipLoop(int isDungeon, unsigned ticks, unsigned* maxGapOut) {
    uint32_t paintedTick = 0;
    uint32_t lastPaint = 0;
    unsigned paints = 0;
    unsigned maxGap = 0;
    for (uint32_t tick = 1; tick <= ticks; ++tick) {
        if (BottomMapAnim_NeedsPaint(tick, paintedTick, isDungeon, W, H)) {
            const unsigned gap = (unsigned)(tick - lastPaint);
            if (gap > maxGap) maxGap = gap;
            lastPaint = tick;
            paintedTick = tick;
            ++paints;
        }
    }
    *maxGapOut = maxGap;
    return paints;
}

int main(void) {
    TestPulseCollapses();
    TestSignatureMatchesDrawnPicture(0);
    TestSignatureMatchesDrawnPicture(1);

    const unsigned ticks = 4096u;
    unsigned owGap = 0, dunGap = 0;
    const unsigned ow = RunSkipLoop(0, ticks, &owGap);
    const unsigned dun = RunSkipLoop(1, ticks, &dunGap);

    /* Bounded staleness: an unenumerated engine input can never go unnoticed
     * for longer than the forced period. */
    assert(owGap <= BOTTOM_MAP_ANIM_FORCE_TICKS);
    assert(dunGap <= BOTTOM_MAP_ANIM_FORCE_TICKS);

    /* Liveness: the loop must keep painting forever. Zero paints here is the
     * permanent-freeze bug this design exists to prevent. */
    assert(ow > 0u);
    assert(dun > 0u);

    /* And it has to be worth doing. Overworld is capped by the 8-tick blink,
     * the dungeon by its 3-changes-per-8-ticks palette rotation. */
    assert(ow < ticks / 4u);
    assert(dun < ticks / 2u);

    /*
     * Documents the trap rather than the fix: before this change the tick
     * advanced only when a paint happened (`sBottomWorkerTick = sBottomTick++`
     * inside the schedulePaint branch). Feed the predicate such a tick and it
     * never paints again -- the signature can no longer change, so the
     * animation dies permanently instead of going stale. This is why
     * port_ppu_3ds.c advances the tick on the cadence instead.
     */
    uint32_t frozenTick = 5u;
    uint32_t frozenPainted = 5u;
    unsigned frozenPaints = 0;
    for (unsigned i = 0; i < 1000u; ++i) {
        if (BottomMapAnim_NeedsPaint(frozenTick, frozenPainted, 0, W, H)) {
            frozenPainted = frozenTick;
            ++frozenTick; /* only advances on a paint: the bug */
            ++frozenPaints;
        }
    }
    assert(frozenPaints == 0u);

    printf("bottom_map_anim_3ds_test: PASS (overworld %u/%u paints, max gap %u; "
           "dungeon %u/%u paints, max gap %u)\n",
           ow, ticks, owGap, dun, ticks, dunGap);
    return 0;
}
