# Layer and Collision Fidelity Audit

This audit records the systemic layer, doorway, roof-priority, water, and tile
hazard faults corrected after the Nintendo 3DS v0.25 hardware captures. Ten
physical-console dumps were inspected privately. No ROM, dump, save,
screenshot, diagnostic file, or extracted game asset is included here.

## What the captures isolated

The failures were not room-specific map defects. They appeared wherever the
engine had to select or change a collision layer: exterior gates, interior
doorways, roof thresholds, water, drops, and Zelda's follower path. The dumps
contained coherent maps and act-tile data, but the runtime selected the wrong
layer for that data.

Two independent implementation errors produced the same visible family of
symptoms.

## European tile-property relocation

`sub_080B1B84` and `sub_080B1BA4` read a `u16` property for the tile type under
an entity. Those bits control collision-layer selection, roof/draw priority,
safe-path memory, climbing, lantern interactions, and projectile behavior.

The universal port always read that table from USA ROM offset `0x00000360`.
The equivalent European ARM routines instead reference `0x080003A8`, so the
European table begins at ROM offset `0x000003A8`. The actual 0xAE4-byte table
payload is identical in the two clean retail ROMs; only its location changes.
At European offset `0x360` there are startup pointer bytes, not tile
properties.

The captured states demonstrate the consequence directly:

- The Hyrule Castle gate tile should carry property `0x0022`; the stale offset
  returned `0x0300`, losing the forced lower layer and safe-path flags.
- The water tile should carry property `0x0000`; the stale offset returned
  `0x000C`, forcing Link onto the upper map layer. That upper tile was empty,
  so water behavior disappeared.
- Empty upper-layer tile type zero should carry `0x0000`; the stale offset
  returned `0x57CC`, repeatedly reinforcing the wrong layer.

The regional ROM profile now carries the table offset, and both property
readers resolve it from the active ROM with bounds checks. The USA path remains
at `0x360`; Europe uses `0x3A8`.

## Reversed `CheckOnLayerTransition`

The original Thumb routine compares the current collision layer with byte two
of each transition record. If they are equal, it preserves the current layer;
otherwise it writes byte three as the destination. In pseudocode:

```text
if currentLayer != preservedLayer:
    currentLayer = destinationLayer
```

The first Android native-port implementation reproduced that condition. A
later collision-management commit changed only `!=` to `==`, reversing the
meaning of every transition record. The 3DS source inherited that regression.
Both clean USA and European ROMs contain the same seven transition records, so
this fault also explains the pre-existing USA doorway and draw-order problems.

The most severe records are act tiles `0x52`, `0x27`, and `0x26`, whose two
layer bytes are both 3. Correct code moves an entity from layer 1 or 2 to layer
3. The reversed condition changes nothing unless the entity is already on
layer 3, making those transitions effectively inert. The corrected helper is
covered for both directional pairs and all layer-3 entry cases.

## Preserving the original act tile

The original `UpdateCollisionLayer` preserves the return value from
`CheckOnLayerTransition` while updating sprite priority. `GetTileHazardType`
then classifies that exact pre-transition act tile.

The native C port declared `UpdateCollisionLayer` as `void`, discarded the
value, and queried the tile again after changing the entity's layer. At a
layer boundary this can read a different floor, incorrectly treating an
entity as entering water, lava, swamp, or a hole. The port now returns and
uses the original act tile exactly as the GBA routine does.

## Related fidelity repairs

- Special tile types now use `gMapSpecialTileToActTile`; the previous code
  incorrectly reinterpreted the separate special-property table as bytes.
- The Android other-layer workaround remains available only for its intended
  lantern mask `0x40`. Movement, roof priority, climbing, and projectile masks
  no longer borrow properties from a different collision layer.
- No doorway, room map, water tile, roof, entity position, or script receives
  a special-case patch.

## Regression coverage

The region-runtime-data regression verifies:

- both directional transition pairs preserve and select the original layers;
- act tiles that require layer 3 reach it from layers 1 and 2 and preserve it
  once selected;
- only the lantern compatibility mask may inspect the other layer;
- synthetic USA and European ROM layouts resolve their own tile-property
  records; and
- truncated property records are rejected.
