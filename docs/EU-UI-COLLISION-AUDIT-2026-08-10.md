# European UI and Collision Pointer Audit

This audit records the two remaining European-region failures corrected after
the Nintendo 3DS v0.24 hardware retest. Four physical-console dumps were used
privately; no dump, screenshot, save, ROM, or extracted asset is included in
the repository.

## Confirmed v0.24 improvements

The first two captures confirm that the European intro, normal gameplay
sprites, and the Smith-house dialogue path now render coherently. This is
consistent with the corrected OBJ geometry table introduced in v0.24.

## HUD R button

The top-screen R bubble and the bottom-screen copy were both malformed because
they shared the same USA-baseline sprite selection. The fat binary rebuilt its
native UI definitions with sprite 505 for all three A/B/R button frames, and
the second-screen theme independently used the same raw index.

The original regional UI definitions make the required mapping explicit:

- USA A/B/R definitions use sprite `0x01F9` (505).
- European A/B/R definitions use sprite `0x01F8` (504).
- USA item and label definitions use sprite `0x0142` (322).
- European item and label definitions use sprite `0x0141` (321).

The fixed-UI remap now covers exactly these two known entries. It remains
scoped to UI/menu callers; active-ROM gameplay entity indices are unchanged.
The second-screen compositor resolves the same region-correct button index, so
both screens use one consistent frame definition.

## Link's house doorway

The fourth capture is room `0x22/0x10` (Link's house entrance). Link is stopped
at world position `(188, 529)`, immediately before the east archway into room
`0x22/0x11`. The dumped bottom-layer collision map shows extended collision
type `0x5C` on the doorway threshold. That collision type is legitimate and
does not need a room-specific override.

For a normally walking Link, collision type `0x5C` selects mask index zero.
The mask is a 16-row pixel bitmap:

- USA packed pointer table: ROM `0x00823C`, entry zero -> `0x08007FDC`.
- European packed pointer table: ROM `0x0082D4`, entry zero -> `0x08008074`.
- The intended mask at both region-native targets is entirely clear.
- Reading the stale USA target `0x08007FDC` from the European ROM instead lands
  on unrelated preceding data containing solid `0xFFFF` rows and partially
  solid `0xFF00` rows.

The port used the USA compile-time pointer bytes with every active ROM. On the
European ROM, the resulting false wall exactly explains why Link stopped
before the threshold even though the room map and archway object were correct.

Both PC-port consumers of the collision-mask pointer table now resolve the
packed pointer through the active regional ROM profile. Bounds checks cover
the 40-entry pointer table and each complete 16-row target. No map tile,
collision byte, transition, or room script is patched.

## Regression coverage

The new region-runtime-data test verifies:

- USA fixed UI indices remain unchanged.
- European item/label sprite 322 maps to 321.
- European HUD button sprite 505 maps to 504.
- Neighboring sprite 506 is not broadly shifted.
- Synthetic USA and European packed collision tables resolve their own target
  masks.
- Null, truncated, unaligned, and out-of-range collision-table inputs are
  rejected.
