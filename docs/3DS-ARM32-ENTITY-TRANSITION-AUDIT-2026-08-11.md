# 3DS ARM32 Entity and Room-Transition Audit

Date: 2026-08-11

## Scope

This audit covers the story-blocking failures reported around Minish Woods,
the Hyrule Castle throne-room south doorway, intermittent room/door behavior,
and the bedroom stair overlap. It compares the complete local v0.1-v0.31
history with the canonical decompilation, the inherited PC/Android ports, an
independent native 3DS implementation, and the USA/Europe retail data layouts.

No emulator was used. The runtime acceptance step remains a physical 3DS test.

## Primary Root Cause

`RegisterRoomEntity` inherited a workaround written specifically for the
x86-64 `GenericEntity` layout. Under `PC_PORT`, it mirrored the two halves of a
room entity's `spritePtr` to raw offsets `0xAC` and `0xAE`.

That is valid only for the 64-bit host layout:

- x86-64 `sizeof(GenericEntity) == 0xB8`;
- the mirrored bytes occupy alignment padding before the widened union.

The 3DS build also defined `PC_PORT`, but it is ARM32:

- `sizeof(void*) == 4`;
- `sizeof(GenericEntity) == 0x88`;
- `0xAC - 0x88 == 0x24`;
- `0xAE - 0x88 == 0x26`.

The writes therefore escaped the current entity slot and changed the next
slot's `speed` and the first two bytes of `spriteAnimation`. `GetEmptyEntity`
can subsequently allocate that slot without clearing those fields. Rooms with
several consecutive NPCs, objects, or managers could consequently start with
deterministically corrupted movement and animation state.

The workaround entered the inherited port before local v0.1 and remained
byte-identical through v0.31. Its upstream commit explicitly describes the
x86-64 padding problem:

- [999sian/tmc c95fa4db](https://github.com/999sian/tmc/commit/c95fa4db8e25f666b537d7e1eb9a633bc78be0ae)
- [Native-port predicate separation, ADR 0008](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0008-separate-native-gba-emulation-predicates.md)

v0.32 makes all layout workarounds depend on pointer width, not on the broad
`PC_PORT` platform label. ARM32 now uses the original 0x88-byte entity layout;
the x86-64 compatibility path remains intact.

## Transition Findings

### Hyrule Castle

Hyrule Castle room `80/02` has no exit-list warp at its south doorway. The
retail contract is an adjacent-room vertical scroll to `80/01`, followed by
the normal `RELOAD_ALL` entity handoff.

The v0.31 bridge could not handle its own captured failure state. For a room
height of 352 and Link at room-relative Y 325, the original 10-pixel edge test
requires `342 < 325`, which is false. The bridge was placed after that test and
was therefore unreachable before collision stopped Link.

v0.32 does not create a castle-specific warp, move Link out of bounds, or
enlarge his hitbox. It admits an early adjacent-room opportunity only when all
of these facts agree:

1. Link is on or immediately facing a real `ACT_TILE_41` doorway floor.
2. A cardinal input faces exactly one nearby edge; only `DIR_NONE` may fall
   back to the last animation facing.
3. His coordinates remain strictly within the current room.
4. The production resolvers below still prove either an explicit exit or a
   valid adjacent room; otherwise the normal blocking result is preserved.

The existing `DoApplicableTransition` and `sub_0807BD14` functions still own
the transition and its side effects. A regression test includes the captured
`(136, 325)` throne-room state and rejects the retired v0.28 out-of-room
`relX=419` case.

Independent references for the same retail transaction:

- [Castle departure ADR 0174](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0174-complete-castle-departure-state.md)
- [Production room-exit contract ADR 0102](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0102-use-room-exit-side-effects-as-the-transition-contract.md)
- [Production scroll completion ADR 0112](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0112-complete-the-production-room-scroll-frame.md)

### Minish Woods

The retail route is a same-area scroll from Hyrule Field `03/03` to `03/02`,
then an explicit cross-area transition into Minish Woods `00/00`. The
preserved-axis decoder introduced in v0.23 remains necessary and unchanged.
The v0.32 entity-layout correction is important here because the destination
loads a dense graph of scripted actors, enemies, projectiles, and managers.

No hardcoded substitute entity list or room warp was added.

- [Exact Minish Woods boundaries, ADR 0179](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0179-enter-minish-woods-through-exact-room-boundaries.md)
- [Initial Minish Woods actor graph](https://github.com/kfhammond/tmc-3ds/commit/293203b28b38ad5cf9456e8d7f59e608ee6bef4f)

## Other ARM32 and Regional Corrections

- The temporary manager pool now uses the original 0x40-byte slots on 32-bit
  targets and 0x80-byte slots only on 64-bit targets. Range checks and clearing
  use the array's real `sizeof` rather than a hardcoded 64-bit extent.
- A `uintptr_t >> 48` plausibility check is compiled only when `uintptr_t` is
  actually 64 bits, removing undefined behavior from the ARM32 build.
- `Object70` restores the retail `flipY=3` priority sequence on native 3DS.
  The desktop-only `flipY=2` workaround is the known source of the stair-frame
  overlap: [upstream change](https://github.com/999sian/tmc/commit/789345887db68a9fafb4a431a756488c99430d46),
  [issue 116](https://github.com/999sian/tmc/issues/116), and
  [3DS stair sequence ADR 0227](https://github.com/kfhammond/tmc-3ds/blob/port/3ds-bootstrap/docs/adr/0227-preserve-the-production-bedroom-stair-sequence.md).
- Compiled USA packed-pointer tables for guards, the inn, Simon's Simulation,
  and the Gust Jar now select exact per-symbol offsets from the active ROM.
  No global regional delta is used.
- The extracted area-table cache is allowed only for its matching USA layout.
  This is shared desktop-code hygiene, not a claimed 3DS root cause: the 3DS
  target already links ROM-backed asset stubs and reads area data from the
  active ROM.

## Verification Contract

Static and build verification for v0.32 includes:

- compile-time 32-bit/64-bit structure size and offset assertions;
- an ARM11 ELF check for 0x88-byte entity and 0x40-byte manager-pool strides;
- ARM disassembly confirming that `RegisterRoomEntity` no longer emits stores
  to `0xAC/0xAE` in the 3DS build;
- focused tests for the captured castle geometry, preserved transition axes,
  regional runtime data, language selection, ARM11 host pointers, GBA memory,
  debug actions, RNG, and memory-watch behavior;
- complete CIA and 3DSX builds with the development boot console retained.

Physical 3DS validation should cover at minimum:

1. close the castle map hint and regain control;
2. walk south from `80/02` into `80/01`, then return north;
3. travel `03/03 -> 03/02 -> 00/00` into Minish Woods;
4. use both bedroom stair directions in Link's house;
5. revisit ordinary exterior/interior doors, the inn, Castle Garden guards,
   Simon's Simulation, and the Gust Jar on the European ROM.
