# 3DS ARM32 Entity and Room-Transition Audit

Date: 2026-08-11

## Scope

This audit covers the story-blocking failures reported around Minish Woods,
Hyrule Castle, intermittent room selection, and Link's bedroom stairs. It
compares the complete local v0.1-v0.31 history, the canonical decompilation,
inherited PC/Android ports, an independent native 3DS implementation, the USA
and European retail layouts, and eight physical-device quick dumps from the
unreleased v0.32 candidate.

No emulator was used. Runtime acceptance remains a physical 3DS test.

## ARM32 Entity Corruption

`RegisterRoomEntity` inherited a workaround written specifically for the
x86-64 `GenericEntity` layout. Under the broad `PC_PORT` definition, it
mirrored the two halves of a room entity's `spritePtr` to raw offsets `0xAC`
and `0xAE`.

That is valid only for the 64-bit host layout:

- x86-64 `sizeof(GenericEntity) == 0xB8`;
- the mirrored bytes occupy alignment padding before the widened union.

The 3DS build also defines `PC_PORT`, but it is ARM32:

- `sizeof(void*) == 4`;
- `sizeof(GenericEntity) == 0x88`;
- `0xAC - 0x88 == 0x24`;
- `0xAE - 0x88 == 0x26`.

The writes therefore escaped the current entity slot and changed the next
slot's `speed` and the first two bytes of `spriteAnimation`. `GetEmptyEntity`
can subsequently allocate that slot without clearing those fields. Dense room
entity lists could consequently start with corrupted movement and animation
state.

The workaround entered the inherited port before local v0.1 and remained
byte-identical through v0.31. Its upstream commit explicitly describes the
x86-64 padding problem:

- [999sian/tmc c95fa4db](https://github.com/999sian/tmc/commit/c95fa4db8e25f666b537d7e1eb9a633bc78be0ae)
- [Native-port predicate separation, ADR 0008](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0008-separate-native-gba-emulation-predicates.md)

v0.33 makes layout workarounds depend on pointer width rather than the
`PC_PORT` platform label. ARM32 uses the original 0x88-byte entity layout;
x86-64 keeps its compatibility path. The temporary manager pool follows the
same rule: 0x40-byte slots on 32-bit targets and 0x80-byte slots on 64-bit
targets. Range checks and clearing use the array's actual size.

## v0.32 Transition Regression

The unreleased v0.32 candidate introduced a global transition gate up to 32
pixels before a room edge. The production scroll only carries Link 15 pixels
horizontally or vertically during its handoff, so an early start can leave him
outside the destination room.

The device dumps establish two exact failures:

- Castle `80/04 -> 80/02` left Link at X=675 while the destination ends at
  X=656. He was 19 pixels east of the room and trapped behind its generated
  border.
- Link's house `22/10 -> 22/11` left Link at X=223 while the destination starts
  at X=240. Continuing to press east then underflowed the unsigned local-X
  calculation to `0xFFFFFFEF`; the next adjacent-room probe selected `22/12`
  at X=496, explaining the intermittent transition to Dampe's house.

v0.33 removes both the 32-pixel gate and the earlier `ACT_TILE_41` bridge. The
production 10-pixel edge selector, collision-255 probe, explicit-exit resolver,
and adjacent-room resolver again own the transition. A fail-closed bounds check
prevents an already out-of-room coordinate from starting another transition
and turning one bad handoff into an unrelated-room chain.

The check has regression cases for the exact dump coordinates `(223, 524)` and
`(675, 132)`, every room edge, zero-sized rooms, and the valid 15-pixel carry.

## Castle South Edge

Castle room `80/02` has no exit-list warp at its south arch. Its retail
contract is an adjacent-room vertical scroll to `80/01`.

An older snapshot at room-relative Y=325 is not evidence that the retail edge
resolver failed: the room is 352 pixels high, the normal selector begins only
after Y=342, and the south collision probe was still on fully passable shape-0
floor. The snapshot also contained no held D-pad input. No castle-specific
warp, widened hitbox, or speculative early gate is therefore retained.

The verified ARM32 out-of-bounds entity writes are removed, but the snapshot
does not capture the native entity pool and cannot prove that a particular
actor caused that historical report. If physical testing still stops at the
actual south edge, the next useful evidence is a held-south trace of raw input,
player mobility flags, collision probe, selected edge, adjacent-room result,
and scroll action—not another geometric fallback.

Independent descriptions of the retail transaction:

- [Castle departure ADR 0174](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0174-complete-castle-departure-state.md)
- [Production room-exit contract ADR 0102](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0102-use-room-exit-side-effects-as-the-transition-contract.md)
- [Production scroll completion ADR 0112](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0112-complete-the-production-room-scroll-frame.md)

## Stairs and Minish Woods

The v0.32 stair disappearance was an OBJ-priority regression. It restored
retail priority 3 even though the port does not yet draw Object70's head
overlay; both background layers had priority 2, so Link was hidden. v0.33
restores the existing native-port priority-2 workaround until that overlay is
implemented.

- [Upstream workaround](https://github.com/999sian/tmc/commit/789345887db68a9fafb4a431a756488c99430d46)
- [Issue 116](https://github.com/999sian/tmc/issues/116)

The retail Minish Woods route is a same-area scroll from Hyrule Field `03/03`
to `03/02`, followed by an explicit cross-area transition into `00/00`. The
preserved-axis decoder introduced in v0.23 remains unchanged. No hardcoded
warp or substitute entity list was added.

- [Exact Minish Woods boundaries, ADR 0179](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0179-enter-minish-woods-through-exact-room-boundaries.md)
- [Initial Minish Woods actor graph](https://github.com/kfhammond/tmc-3ds/commit/293203b28b38ad5cf9456e8d7f59e608ee6bef4f)

## Regional and Platform Corrections

- A `uintptr_t >> 48` plausibility check is compiled only when pointers are
  actually 64 bits, removing undefined behavior from the ARM32 build.
- Compiled USA packed-pointer tables for guards, the inn, Simon's Simulation,
  and the Gust Jar now select exact per-symbol offsets from the active ROM. No
  global regional delta is used.
- The extracted area-table cache is allowed only for its matching USA layout.
  This is shared desktop-code hygiene, not a claimed 3DS root cause: the 3DS
  target already links ROM-backed asset stubs.
- The title/file-select bottom card is procedural: black background, gold
  frame, and a pulsing Triforce. It contains no copied or ROM-derived asset.
  Touch opens the existing Minish Cap settings page. UI state, hit targets,
  engine snapshots, refresh requests, and live configuration values are
  synchronized across the main and bottom-worker threads.

## Verification Contract

Static and build verification for v0.33 includes:

- compile-time 32-bit/64-bit structure size and offset assertions;
- an ARM11 ELF check for 0x88-byte entities and 0x40-byte manager slots;
- ARM disassembly confirming that the 3DS `RegisterRoomEntity` path emits no
  stores to offsets `0xAC` or `0xAE`;
- focused tests for captured transition coordinates, preserved axes, regional
  runtime data, language selection, ARM11 host pointers, GBA memory, debug
  actions, RNG, memory-watch behavior, and the procedural
  bottom card;
- complete v0.33 CIA and 3DSX builds with the development boot console kept.

Physical 3DS validation should cover at minimum:

1. both `22/10 <-> 22/11` house transitions without leaving room bounds;
2. both bedroom stair directions with Link visible throughout;
3. `80/04 -> 80/02`, then the south `80/02 -> 80/01` edge and return;
4. `03/03 -> 03/02 -> 00/00` into Minish Woods;
5. ordinary doors, the inn, Castle Garden guards, Simon's Simulation, and the
   Gust Jar on the European ROM;
6. title/file-select Triforce, touch-to-settings, BACK, and return to gameplay.
