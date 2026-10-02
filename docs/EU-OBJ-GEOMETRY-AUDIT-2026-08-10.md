# European OBJ Geometry Audit

This audit records the root cause of the European rendering corruption fixed
for the Nintendo 3DS v0.24 build. It is based on clean retail USA and European
ROMs, four physical New Nintendo 3DS dumps from v0.23, the matching
`zeldaret/tmc` European build, and the `samyost1/tmc-android` source base.

No ROM data, extracted assets, saves, dumps, or private screenshots are stored
in this repository.

## What the dumps proved

The background layers were coherent while title objects, file-select elements,
Link, NPCs, and HUD objects were clipped or displaced. The title dump made it
possible to isolate each stage of the OBJ path:

- The European graphics groups copied into VRAM matched the European retail ROM
  byte-for-byte.
- The dynamic title graphics selected by fixed-GFX entries 511 through 514 also
  matched the European ROM byte-for-byte at their expected OBJ VRAM slots.
- The emitted OAM pieces used the European frame-object table. For example,
  European sprite 510 contains the same title frames that USA stores at sprite
  511, confirming the known one-entry regional shift.
- Despite correct tiles and frame records, most pieces were rejected or moved
  by the renderer's clipping calculation.

The final point ruled out ROM detection, the European language slot, the
PICA200 presenter, texture conversion, and a global sprite-index remap as the
primary cause.

## Exact root cause

`RenderSpritePieces` uses a 240-byte table containing the anchor, width, and
height for every GBA OBJ shape/size combination. The 3DS regional profile
loaded that table from European ROM offset `0x0B25E8`. That address is not the
geometry table: it is inside an adjacent function-pointer table.

Consequently, values such as European code pointers were interpreted as OBJ
anchors and dimensions. One directly observable example is the four-piece
European `PRESS START` sprite:

- With the real geometry table it produces four visible pieces at Y=144.
- With the bytes at `0x0B25E8`, the first piece is clipped and the remaining
  three are moved to Y=90.
- The v0.23 physical OAM dump contains exactly those three pieces at Y=90.

The actual European geometry table begins at ROM offset `0x0B2310`. Its full
240 bytes are identical to the USA table at `0x0B2BE8`.

## Why the Android port did not show the failure

The Android source base contains the same stale European offset in its regional
metadata, but its runtime path does not consume that field. Android always
initializes OBJ geometry from the canonical compile-time table.

The 3DS v0.16 region work changed this path to read the active ROM, which was a
reasonable design goal but made the previously unused bad offset live. Later
sprite-table and language fixes could not repair the result because they acted
before the final clipping stage.

## Fix and hardening

The European profile now uses `0x0B2310`. The loader also validates all 240
bytes against the canonical region-invariant table. If a future profile points
at unrelated ROM data, the engine reports the invalid table and uses the
canonical copy instead of silently corrupting every OBJ.

The complete European regional-offset profile was then checked against symbols
from a matching `zeldaret/tmc` European build. Graphics groups, palettes, frame
objects, extra frame offsets, fixed graphics, sprite pointers, text, UI, map,
area, exits, flags, collision, figurines, rails, and song data all resolved to
their expected European symbols. The OBJ geometry address was the outlier.

The v0.23 preserved-coordinate transition fix remains unchanged. The supplied
v0.23 dumps show the recorded room state advancing across the captured room
boundary, while this rendering correction is independent of transition state.
