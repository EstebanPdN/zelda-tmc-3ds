# Remediation Plan — ~900 truncated/zeroed animations (extractor `infer_asset_size`)

Status: **PLAN ONLY — not yet implemented.** A previous attempt (`28fbfa9ff`) was
reverted (`8731a8075`) as a regression. Read this before retrying so the same
dead-end isn't re-walked.

## Symptom

~900 animations extract with the wrong byte length and fall back to **zeroed
data** at runtime → the sprite renders **blank/static** (e.g. Deepwood held
mushroom #4, blue teleport icon #8, Gyorg/Vaati cutscene frames, MazaalHand,
shop-top frames; CHANGELOG known-issues 267-269/275/303/325, possibly Vaati
2nd-stage #96). These are the animations whose true length differs from the
heuristic by **1–3 bytes**.

## Why the first attempt regressed (the load-bearing lesson)

`28fbfa9ff` tried to make the parser **tolerant**: synthesize `loop_back = 0`
for truncated loop frames, ignore 1–3 trailing bytes, and pass raw `.bin`
through to runtime. Result: animations that previously rendered **blank**
now rendered **garbage** (it broke Zelda's intro). The revert's verdict:

> Zeroed data turns out to be the better failure mode here. Fixing them properly
> requires fixing `infer_asset_size` to determine the correct end-of-animation
> marker rather than relying on heuristics + tolerant parsers.

**Principle for the retry: correctness, not tolerance.** The extracted bytes
must be *exactly* what the runtime decoder expects. Tolerance just converts one
wrong state (blank) into a worse one (garbage).

## Root cause

`infer_asset_size` (`tools/src/assets_extractor/assets_extractor.hpp:1405`)
sizes an asset by either (1) an exact `port_asset_index` lookup, or (2) the gap
to the **next symbol boundary**. For animations neither is byte-exact:

- The gap-to-next-boundary **over-counts** by the 1–3 bytes of alignment padding
  the assembler inserted before the next symbol.
- A frame-count heuristic **under-counts** by the **single trailing `loop_back`
  byte** that follows a loop-terminated final frame.

`WriteEditableAnimation` then strictly parses frames against that off-by size and
**rejects** the mismatch → zeroed fallback.

## The authoritative animation format (ground truth: `port/port_animation.c`)

Frames are **4 bytes**:

| byte | meaning |
|------|---------|
| [0] | `frameIndex` |
| [1..2] | keyframe data (duration etc.) |
| [3] | `frame` — **bit 7 = loop flag** |

When bit 7 of byte [3] is set, the animation **loops**: the *next* byte
(`p[0]` after advancing past the frame) is `loopBack`, and the decoder does
`p -= loopBack * 4`. That trailing `loopBack` byte is the **true terminator**
of a looping animation's data; the bytes after it (to the next symbol) are
alignment padding and are **not** part of the animation.

So the exact extent of an animation starting at `offset` is:

```
walk 4-byte frames from offset, bounded by the next-symbol boundary:
  for each frame f:
    consume 4 bytes
    if (f[3] & 0x80):           // loop flag → this is the terminal frame
        consume 1 more byte     // the loopBack byte
        END                     // exact end; ignore trailing alignment
  if boundary reached with no loop frame:
    END at boundary             // non-looping anim, terminated by next data
```

This mirrors the runtime's own loop-flag logic exactly, so the extracted bytes
decode identically to how the game walks them.

## Plan

1. **Add `compute_animation_extent(offset, boundary)`** to the extractor
   (`assets_extractor.hpp`), implementing the walk above. It returns the exact
   byte length (frames×4 [+1 loopBack]). Use `infer_asset_size`'s
   next-boundary value only as the **upper bound** for the walk (anti-runaway),
   never as the length itself.

2. **Route animation extraction through it.** Replace the
   `infer_asset_size(anim_offset, …)` call at `assets_extractor.hpp:2041`
   (and the size `WriteEditableAnimation` validates against) with
   `compute_animation_extent`. Keep the **strict** parser — with the correct
   length it now succeeds instead of rejecting. No tolerant fallbacks.

3. **Runtime net (`port_asset_loader.cpp:1868`).** The `dataSize % 4 != 0`
   branch currently **skips** the contiguous two-pass packing for odd-sized
   anims — those are precisely the loop-terminated ones (frames×4 + 1 loopBack).
   With exact extraction they're now correct but still odd-sized; verify the
   contiguous-packing path handles the trailing `loopBack` byte (it must remain
   the last byte before the next anim, as on GBA's contiguous ROM). Likely the
   `%4` skip can be narrowed to `< 4` only.

4. **`port_asset_index` exact sizes.** Where an animation offset has an *exact*
   `lookup` hit (`infer_asset_size` step 1), confirm that recorded size already
   includes the `loopBack` byte. If the index was generated with the same
   heuristic, regenerate the animation entries from `compute_animation_extent`
   so the index and the extractor agree.

## Validation (the regression gate)

The retry **must** prove it doesn't reintroduce garbage:

1. **Byte-identical guard on known-good anims.** Before/after the change,
   extract a set of animations that already render correctly (Zelda's intro —
   the one the salvage broke — plus Link's walk cycle, a common enemy). Assert
   the extracted `.bin` bytes are **unchanged**. If any known-good anim changes,
   the extent calc is wrong — stop.
2. **Decode-validity on the ~900.** For each previously-zeroed animation, assert
   the new extent (a) ends on a loop frame + loopBack, or at the boundary, and
   (b) every `loopBack` resolves to an in-bounds frame offset (`p - loopBack*4 ≥
   anim start`). Reject (keep zeroed) any that still don't satisfy this rather
   than emitting garbage — preserve "blank beats garbage."
3. **In-game smoke test.** Held mushroom (#4 Deepwood), teleport icon (#8),
   a Gyorg/Vaati cutscene frame, and Zelda's intro — confirm correct render,
   no garbage, no regression on a previously-OK sprite.

## Risk & rollback

- **Risk:** mis-deriving the terminator re-creates the garbage regression. The
  byte-identical guard (validation #1) catches it before it ships.
- **Blast radius:** extractor-only + the one runtime `%4` branch; gated so the
  GBA decomp is untouched. No `src/` game-logic changes.
- **Rollback:** revert the extractor commit; runtime falls back to zeroed
  (blank) exactly as today. The `28fbfa9ff`/`8731a8075` pair is the reference
  for what *not* to do.

## Effort

Medium. The extent walk is ~30 lines; the validation harness (byte-identical +
loopBack-in-bounds) is the bulk and the part that makes this safe to ship.
Recommend implementing the validation harness **first**, running it against the
current strict extractor to establish the baseline, then landing the extent calc.

## Validation findings — attempt 2026-05-30 (DO NOT ship the extractor-only change)

`compute_animation_extent` (terminator-driven walk, exact mirror of the decoder)
was implemented, built, and validated against all 2883 extracted animations.
Result:

- **1974** extract as clean self-contained loops (`N*4+1`, last frame loop-flagged,
  `loop_back ∈ [1,N]`). ✓
- **865** have no loop frame within the boundary → kept the boundary-gap size
  (unchanged behavior). ✓
- **38** have **`loop_back > N`** — the loop jumps back **beyond the animation's
  own start**, into the **contiguous preceding animation** in ROM. This is a real
  shared-frame technique: e.g. `gSpriteAnimations_12_0` (N=8, loop_back=26) reuses
  frames from the animation packed before it. (Plus a few `loop_back==0`.)

**Why exact extraction REGRESSES those 38.** The old boundary-gap sizes were
even (`%4==0` after alignment), so the runtime net at
`port_asset_loader.cpp:1868` packed those anims **contiguously** and the
loop-back-into-previous reference resolved. Exact extraction makes them
**odd-sized (`N*4+1`)**, which trips that net's `dataSize % 4 != 0` **skip** →
the anim is no longer contiguous with its predecessor → the back-reference reads
detached/garbage data. **The extractor change alone trades 38 working anims for
the ~900.** The validation gate (`loop_back`-in-bounds check on the output)
caught this before it shipped — exactly the failure class the reverted
`28fbfa9ff` salvage hit.

**The real crux is contiguity, not the trailing byte.** The complete fix must
pair the exact extent with a runtime net that packs **all** anims contiguously,
including odd-sized ones, preserving ROM order so a `loop_back > N` resolves into
the genuine preceding frames. Concretely:

1. **Runtime side first.** Change `port_asset_loader.cpp:1868` so the
   contiguous two-pass packing covers `N*4+1` (odd) anims too — pack every anim
   back-to-back in ROM order into one buffer, hand each its own start pointer.
   A `loop_back` that walks before an anim's start then lands in the real
   preceding bytes, as on GBA. Validate the 38 `loop_back > N` anims resolve
   in-bounds within the packed buffer.
2. **Then the exact extent calc** (`compute_animation_extent`) so the strict
   parser accepts the ~900.
3. **Or, simpler interim:** only apply the exact extent to anims whose
   `loop_back ≤ N` (self-contained); leave `loop_back > N` anims at the
   even boundary-gap size so the existing net keeps packing them. This fixes most
   of the ~900 with zero regression risk and defers the contiguity rework.

Option 3 is the recommended next step: lower risk, no runtime-net change, and it
still recovers the large majority of the blank animations. The
`compute_animation_extent` implementation from this attempt is in the git history
(reverted from `tools/src/assets_extractor/assets_extractor.hpp`) for reuse.

## Option 3 — IMPLEMENTED & validated (2026-05-30)

Shipped: `compute_animation_extent` now returns the exact `N*4+1` extent **only**
for self-contained loops (`loop_back ∈ [1, N]`); everything else (shared-frame
`loop_back > N`, `loop_back == 0`, no-loop) falls back to the original
boundary-gap size, so the runtime net is untouched for those.

**Regression proof** (diff of Option-3 output vs the original extractor, all 2883
anims):

- **141 files changed; all 141 are byte-prefixes of the original** (only trailing
  alignment trimmed — the runtime never reads those bytes, since a self-contained
  loop's `loop_back` jumps back within its own frames).
- **all 141 changed files are valid self-contained loops**; **0** are non-prefix,
  **0** are non-self-contained. Every shared-frame / fallback anim is byte-
  identical to before.

So Option 3 **recovers 141 animations** (the over-counted subset the strict
parser had been rejecting into blank/zeroed data) with **provably zero
regression**. The remaining shared-frame anims (`loop_back > N`) still need the
contiguous-packing rework (the full fix above) and are deliberately left at their
original size.
