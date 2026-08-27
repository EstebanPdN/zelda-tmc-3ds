# Changelog

## v0.8.3 (2026-07-19)

### Fixed

- **Vaati fight 1 no longer freezes, drifts away, or leaves Gust Jar/laser
  spinner eyes invulnerable.** The PC dark-magic projectile struct was missing
  two padding fields, so its death callback wrote controller and eye state four
  to eight bytes early. The overlay now matches both parents, allocation
  failures run normal eye cleanup, and impossible phase-3 states self-heal.
- **Incompatible v0.8.1/v0.8.2 quicksaves and autosaves are now rejected.**
  Their live entity layouts predate the corrected 64-bit overlays and cannot be
  restored safely. Normal `tmc.sav` progress is unaffected.
- **Fixed seven additional 64-bit entity-overlay mismatches.** Pesto's pot and
  dirt-ball state, Scissors Beetle mandibles, ball-and-chain geometry, Vaati
  Wrath electric attacks, Enemy64's Gyorg tail, and Big Green ChuChu start
  particles now use the real PC fields while retaining the original GBA layouts.

## v0.8.2 (2026-07-18)

### Fixed

- **Android crash on launch (v0.8.1).** The CI's unpinned SDL3 package
  floated from 3.4.4 to 3.4.12 between releases, and SDL 3.4.12 changed the
  `onNativePadDown` JNI signature (added a `scancode` argument). The vendored
  Java glue still declared the old signature, so JNI registration failed and
  ART aborted with `NoSuchMethodError` before the game could start — on every
  device. The Java glue is now synced to SDL release-3.4.12 (including the
  new `SDLSensorManager.java`) and the `libsdl3` package is pinned to 3.4.12
  so the two sides can only be bumped together. Verified on a Galaxy Tab A7:
  release 0.8.1 APK aborts at registration, fixed build reaches `SDL_main`
  and runs.
- **Vaati fight 1 (Vaati Reborn) freeze / boss drifting out of bounds.** When
  the final phase threshold fires, the boss enters its defeat sequence — but
  another eye could still hold a pending stagger timer or take a hit that
  frame. Its timer expiry rewrote the boss back into fight state with a phase
  counter (3) the attack-selection switch has no case for, leaving Vaati
  inert and drifting; any further eye hit also read one byte past the
  3-entry phase-threshold table. Both verified live under a sanitizer via
  state injection; eyes now stand down once the defeat has triggered.
  (Latent on GBA too — the stray read hit adjacent ROM there.)
- **Crash entering trees (e.g. North Hyrule Field fairy fountain).** Unused
  rooms in some area room-header tables carry a junk tileset id (`0xFFF3`);
  the GBA read it as harmless garbage, but on PC it indexed ~512KB past the
  64-slot host tileset table — an out-of-bounds read that crashed or not
  depending on heap layout, which is why the crash was intermittent.
  `ReadAreaSubTableEntry` now rejects indices past the table and falls back
  to the bounds-checked ROM resolver.
- **Out-of-bounds sweep for the same bug family** (data-driven index into a
  fixed host-side table; GBA read harmless garbage, PC faults). Found via
  parallel static audit plus an ASan warp tour over every area:
  - Room exit lists are now resolved from the ROM on all regions (previously
    EU/JP only) — the compile-time `gExitLists` sub-arrays are sized to the
    rooms the decomp names, and e.g. Hyrule Town's ROM header table has more
    rooms than exit entries, so room-info init read past the const array
    (caught live by ASan entering area 0x02/0x08).
  - `GetAreaRoomPropertyList` and `GetCurrentRoomProperty` host-table reads
    now bound the room/property index like the tileset fix.
  - `GetCurrentRoomInfo` clamps a junk room id instead of handing out a
    garbage `RoomResInfo` whose `properties` pointer gets dereferenced.
  - `InitRoom` clamps a junk area id (crafted saves) before it feeds the
    metadata/flag-bank/dungeon-key chain; `dungeon_idx` can no longer wrap
    and write past `gSave.dungeonKeys`.
  - The HUD item renderer bounds save-derived item ids against the 128-slot
    animation table (the pause menu already did).
  - `LoadGfxGroup` rejects out-of-range gfx group ids (including the 0xFF
    "none" sentinel) instead of walking a garbage GfxItem list.
  - Text glyph lookup clamps font bank nibbles 9–15 to bank 0 (9 host font
    banks exist; GBA read adjacent ROM).
  - Physics surface-flag table: tiles mapping to rows 60–68 read past the
    60-row table even on GBA (landing in the player-macro bytes that follow
    in ROM); the PC build now appends those ROM bytes verbatim so behavior
    stays GBA-identical instead of linker-dependent.
  - Asset loader force-terminates map-definition chains so a truncated or
    modded `area_*.json` cannot walk the engine off a heap array.
  - The special-layer scroll copy (`ram_sub_080B197C_c`) clamps its 64-byte
    rows to the map buffer: on GBA the two special tilemaps are adjacent
    EWRAM and deep scroll offsets deliberately walked off one into the
    other; on PC they are separate globals (caught by ASan in Hyrule Town
    Underground).
- **Nine per-room entity lists were never filled from ROM** (zero `.bss`
  stubs): the Mayor's house state change, the Mayor's cabin on the Minish
  path, and all seven Happy Hearth Inn 2F oracle lists (the Din/Nayru/Farore
  house-renting sidequest). The `kind != 0xFF` walk ran off the empty arrays
  (caught by ASan in the Mayor's house) and the real NPCs never spawned.
  Filled from ROM like the other per-room lists; two stubs were also too
  small to hold their terminator and were resized.
- ASan builds no longer alias entity-data stubs to a 16-byte typed view —
  the alias made every legitimate walk past the first entry report a bogus
  global-buffer-overflow, hiding real findings.
- **ChuChu-family allocation hardening.** Big Green ChuChu now abandons
  construction cleanly if any linked body segment cannot be allocated;
  previously a missing middle segment was dereferenced immediately. Full
  entity-pool allocation now returns `NULL` directly instead of forming a
  member address from a null pointer (an UBSan abort).

## v0.8.1 (2026-07-15)

### Follow-up crash hardening

- Fixed four additional GBA-to-64-bit hazards found while auditing the boss
  crashes: Gentari's curtain now searches the auxiliary-player and entity pools
  separately instead of reading past the first array; Octorok and Moldworm
  layouts now preserve their pointer/tail offsets and enforce them with static
  assertions; Postman's route selection handles empty route tables without
  modulo-by-zero; and the manager pool is now a real 32-entry `Temp` array
  instead of a byte buffer accessed through a mismatched declaration.

### Codebase-wide sweep for the same GBA-to-64-bit bug family

- Audited `src/`, `include/`, and `port/` for the failure patterns behind the
  boss crashes (fixed-array overflows, GBA/64-bit layout offsets, cross-TU type
  mismatches, host-trapping arithmetic). Two live host bugs fixed:
  - **Fireball Guy split (`fireballGuy.c`).** A type-3 Fireball Guy spawns 5
    children, but the child-pointer array and position-offset table held only 4
    entries — the 5th write/read ran one slot past a stack array and past a
    rodata table (SIGSEGV / corruption on x86/ARM; the sibling `slime.c` splitter
    was already correctly sized). Both are now 5 entries; the 5th offset is the
    real ROM byte at `0x080D1818` (`{0, 2}`), so behavior matches GBA exactly.
  - **Room-tile manager spawn (`beanstalkSubtask.c`).** The generic manager
    spawner wrote a manager's world x/y through `&manager[1].timer + 10/12`,
    which lands on GBA offset 0x38/0x3a but 0x58/0x5a on the larger 64-bit
    `Manager` — so e.g. a tile-placed Flame Manager loaded coordinates of 0 and
    mishandled its torch tile. It now writes the translated tail offset
    (`sizeof(Manager)+0x18/0x1a`), matching the `tileChangeObserveManager` pattern.
- Added a Linux x86_64 GCC LTO CI gate that turns cross-translation-unit
  declaration drift into build failures; reconciled the mismatches it exposed
  while excluding raw ROM blob storage that is intentionally type-punned.

### Deepwood Shrine: crash when the first boss dies (Big Green ChuChu)

- **Killing the Deepwood Shrine boss no longer crashes.** Two decomp bugs in
  the death sequence, both latent on GBA and fatal on x86:
  - **SIGFPE (divide by zero)** in the ChuChu wobble math (`chuchuBoss.c`). The
    boss's gather/merge state — part of the death animation — seeds a wobble
    divisor to 0 from a `{0,0}` data-table entry; the split ChuChu then divides
    by it. GBA's soft-division returns 0 without trapping; x86 raises SIGFPE.
    Both divide sites now go through `Div()` (the port's hardware-faithful
    divide, 0 on divide-by-zero) instead of a raw `/`.
  - **SIGSEGV** in the post-boss element-get cutscene (`elementsBackground.c`).
    `sub_080A04E8` was declared and called with no arguments but its definition
    takes `Entity* this` and dereferences `this->parent`. On GBA the entity
    pointer happened to survive in a register across the call; on x86 the
    "parameter" read garbage and faulted. The declaration and both call sites
    now pass `this`.
  - Verified with a deterministic headless repro (warp into the boss arena,
    force the boss to 0 HP): SIGFPE→SIGSEGV chain before, full death + element
    cutscene to the cleared room after.

### Cave of Flames: cane-flip freeze fixed (Gleerok soft-lock)

- **Using the Cane of Pacci on Gleerok's cooled, downed head no longer freezes
  the fight.** The flip is a real vanilla mechanic: the head pops up, lands
  flipped, and the boss lays its neck flat so the back crystal becomes
  attackable. The neck-extension array really has six segments, but the decomp
  declared five and reached the sixth by indexing one past the array into the
  adjacent struct fields — undefined behavior the GBA tolerated. On the PC
  build the optimizer was free to misplace that sixth-segment access, which
  turned the neck lay-out solver into a non-converging tug-of-war: the boss
  spun forever "laying out" the neck while Link could still walk around. The
  array is now declared with all six segments (identical byte layout, pinned
  by a static assert), and the lay-out converges — verified with a
  deterministic headless repro (3/3 freezes before, 3/3 clean crystal-phase
  progressions after).
- The Gleerok flip/lay-out state machine now has `TMC_VERBOSE`-gated stderr
  tracing (`[gleerok]` lines) covering the cane hit, the flip handshake, and
  the segment solver, for future fight triage.
- **Follow-up crash after the flip fixed too (SIGFPE).** The flip-debris
  particles (`gleerokParticle.c`) divide by their affine scale to size their
  hitboxes; the settled state left the divisors uninitialized (GBA read stack
  garbage through its non-trapping soft division; x86 faults when that garbage
  is 0), and the shrink path can legitimately pass through scale 0 for one
  frame. The divisors are now seeded from the live scales and the zero frame
  keeps the previous hitbox size, matching GBA intent without the trap.

### Fortress of Winds: Mazaal boss-entry crash fixed (issue #162, regression in v0.7.0)

- **Re-entering the Mazaal arena no longer crashes.** The boss head and its
  bracelets link to each other through type-punned struct fields (GBA offsets
  0x74/0x78). v0.7.0's #127-class fix converted the bracelet's links to 4-byte
  EntityRef slots, but the head kept 8-byte raw pointers — so when the head
  woke its arms (mid-fight re-entry from Inner Mazaal, or restoring a
  save state taken in the arena), it read a bracelet's slot *index* as a
  pointer and dereferenced it: SIGSEGV in `mazaalHead.c` the moment the hands
  should rise. The head now uses the same EntityRef slots at the same PC
  offsets, pinned by cross-struct static asserts on both sides; arm commands
  also skip cleanly when a link never spawned (full entity pool).
- This also fixes silent bracelet-state corruption: the head's old 8-byte
  pointer write clobbered the bracelet's adjacent fields (`unk_7c`/`unk_7e`),
  and the bracelet palette-sync path read back a garbage head link.

### More GBA-to-PC fixes (PR #169 + follow-ups)

- **Royal Valley light-overflow softlock fixed** (`miscManager.c`, #169).
  `MiscManager_Type7`'s light-region loop relied on a separate global sitting
  immediately after its data table as a terminator; PC layout broke that
  adjacency, so the loop ran off the end, read garbage into `gRoomVars.lightLevel`
  and froze Link. An explicit in-array `{0xFFFF, …}` sentinel now stops the loop
  in bounds. (Contributed by @alfonsoalvarohervas-sudo.)
- **Minish portal cutscene skip restored** (`enterPortalSubtask.c`, #169).
  `sub_0804AD18` now returns `TRUE` explicitly on PC instead of relying on the
  GBA implicit-register return, so pressing R/B skips the transition again.
  (Contributed by @alfonsoalvarohervas-sudo.)
- **F8 Debug Menu -> Warp: "Shrink to Minish" toggle** (#169) transforms Link in
  and out of Minish size, wiring scale, hitboxes, priority and shadows through
  the engine's `PLAYER_MINISH` state machine. (Contributed by
  @alfonsoalvarohervas-sudo.)
- **Same-class follow-ups from the audit:** the steam-overlay room handler
  (`sub_08059F9C`) now returns explicitly on its entity-delete paths instead of
  an indeterminate register value, and the demo/attract logo draw
  (`sub_080A30AC`) resolves its frame pointers against the live ROM instead of
  reading megabytes past a 4 KB data slice.

## v0.8.0 (2026-07-12)

### Widescreen in more scenes, never stretched

- **Rooms narrower than the monitor's widescreen target now render true-wide
  at their full room width** instead of dropping back to the native 240px
  view. 256px and 272px rooms — most dungeon rooms and larger interiors —
  show their whole width with only thin pillarbox bars (e.g. a 272px room on
  a 16:9 monitor leaves ~6px per side) rather than none of the extra world.
- **Scenes that cannot be widened are presented at their correct aspect,
  never stretched.** The title screen, file select, pause/menu overlays, and
  one-screen 240px rooms (Link's house and other small interiors) have no
  extra world data to reveal, so they render the GBA-native canvas centered
  and undistorted.
- **The space the game can't fill now defaults to a blurred ambient fill**
  (a soft stretched copy of the scene behind the sharp frame), so every
  scene fills the whole monitor without stretching the game itself. The
  fill also covers the window in the default Native aspect mode, where it
  previously never showed. F8 → Display → "Background" switches back to
  plain black bars or a solid color (existing configs that explicitly saved
  a style keep it).

### Game speed decoupled from the framerate

- **Game logic now runs on its own fixed clock, independent of Target FPS and
  VSync** (`decouple_render`, default on; F8 → Display → "Decouple game
  speed"). The engine ticks at 60 Hz — or the GBA-exact 59.7275 Hz in
  Console-Parity mode — while the Target FPS preset only paces how often
  frames are presented: above 60 the port re-presents (or interpolates)
  between ticks, below 60 it skips whole presents without slowing the game.
  Previously the FPS cap paced the engine directly, so a 120 preset ran the
  game at 2x and a 30 preset at half speed; that behavior remains available
  by turning the toggle off (and is what the capture/repro harnesses pin via
  `TMC_LEGACY_PACING`/`TMC_PERFCAP`/`TMC_ROOMCAP`/`TMC_CAPTURE_FRAME`).
  Anyone who used a >60 preset as a speed boost should use fast-forward (TAB)
  or the uncapped preset instead. The window title now shows both rates
  ("FPS / TPS"), and `TMC_PACE_LOG=1` prints them per second for testing.
- **Fast-forward is much faster.** Uncapped ticks no longer rasterize every
  frame — presents are throttled to a real-time 60 Hz cadence, so TAB /
  uncapped speed is bound by the engine alone (~13,500 ticks/s headless vs.
  render-bound before).
- LCD-persistence ghosting now accumulates once per game tick instead of once
  per present, so its decay no longer speeds up at high render rates; paused
  practice re-presents also rewind the HBlank-DMA line clock correctly (fixes
  per-line affine effects rendering wrong while paused).
- **VSync is no longer force-disabled above 60 FPS on high-refresh displays.**
  The "VSync would cap the FPS preset" override (#26) now compares the target
  against the actual refresh rate of the display the window is on, not a
  hardcoded 60 — so 75/90/120 targets on a 120 Hz panel keep VSync and stop
  tearing. On a fixed-refresh panel a target matching the panel rate (e.g.
  120 on 120 Hz) gives the most even cadence: a 90-in-120 cadence cannot
  divide evenly and shows as slight judder even without tearing. With VSync
  active the pacer presents whenever the render grid is due (the blocking
  present is the throttle) instead of second-guessing the tick deadline with
  a cost estimate inflated by the vsync wait; a genuinely overloaded tick
  (more than half a tick late) falls back to conservative pacing briefly.

### Randomizer: fixed Hyrule Town softlock in the Picori-festival window

- **A story-skipped file could softlock on entering Hyrule Town.** Until Deepwood
  Shrine is cleared the engine keeps `global_progress == 1`, and Hyrule Town's
  room init treats that as "Picori Festival in progress" — redirecting the town
  to the festival area and re-arming the Zelda-chase. In the randomizer the intro
  is already over (the story-skip sets `TABIDACHI`), so the town loaded in
  festival state while its exits were post-festival: walking out the north edge
  stranded the player. Town entry now suppresses the festival redirect once the
  intro is over (`TABIDACHI` set), loading the normal post-festival town. GBA
  behavior is unchanged (the guard is PC-port only); regression probe
  `TMC_ROOMCAP_TOWN_PROBE` covers both the fix and the intact intro path.

### First-launch extraction message on Android; dead-code cleanup

- **The first-launch asset-extraction screen no longer tells Android users to
  "See terminal for detail"** — a phone has no terminal, and a minutes-long
  bar with no context read as a hang. Android now shows "One-time setup - this
  can take a minute on first launch."; desktop keeps the terminal hint.
- Removed dead code: `Port_PaintPrelaunch` (a countdown-splash renderer with
  zero callers - the live prelaunch is the ImGui card).

### Randomizer: shared seeds no longer silently mismatch between the two setups

- **The file-select sidebar seed field now accepts the same characters as the
  F8 tab.** It filtered input to letters and digits only, silently dropping
  spaces and punctuation — so a friend's phrase seed like `cool seed` became
  `coolseed` and generated a different world, with no error. Both fields now
  hash the raw text identically. The sidebar field also matches the F8 buffer
  length (63 chars, was 32), so a long shared phrase can't truncate differently
  either.

### Settings menu & hotkeys are now discoverable

- **First-launch "Settings (F8)" hint.** Nothing in-game ever told a new
  player the settings menu existed or which key opens it. The always-present
  corner trigger now renders a bold "Settings (F8)" label until the menu is
  opened once (by any path — F8, gamepad Select+Start, or clicking it), then
  reverts to the faint corner glyph. Persisted as `menu_hint_seen` so it
  never nags a returning player.
- **Keyboard shortcuts reference.** The F8 → Controls tab gains a "Keyboard
  shortcuts" section listing every hotkey (F1-F12, Tab fast-forward, practice
  keys) with what each does — previously the ~10 function keys were documented
  nowhere in-game. Notes that F1-F6 are disabled in Console-Parity mode.
- **Save/load hotkeys in the menu footers.** Both the ribbon and classic F8
  menu footers now show "F5/F6 quicksave/load, F9 bug report" and point to the
  Controls tab; previously only the off-by-default classic renderer hinted at
  save/load and the default ribbon footer listed none.

### Region-mismatched ROM errors are now visible in the GUI, not just stderr

- **A version-mismatched ROM no longer fails invisibly** on single-region
  (developer / CI-guard) builds. Previously the region cross-check only wrote
  to stderr — invisible to a double-click or Android user: Console-Parity
  mode did `return 1` (window silently vanished) and casual mode played a
  broken USA/EU hybrid with no notice. Now Console-Parity shows a fatal
  message box explaining the mismatch before exiting, and casual mode shows a
  non-fatal warning box ("graphics, text, and RNG may be wrong") then
  continues. The shared `Port_FatalRomError` helper (stderr + SDL message box)
  is exported from `port_rom.c` and reused for both.
- **A Japanese (BZMJ) ROM on a build without populated JP data tables now
  shows a clear message box** ("JP not yet supported - use a USA or EU ROM")
  instead of exiting with only a stderr `FATAL` line and a vanished window.

### Randomizer setup UX: roll-lock on file screen, L hint, unified labels

- **Rolling from the F8 tab on the file-select screen no longer silently
  discards the roll.** There the roll button activated a transient global
  seed, but creating the new file re-rolls from the sidebar's own state — so
  the F8 "verified beatable" result was a lie on that screen. The F8 roll/reset
  buttons are now locked while on the file screen (as they already are during
  gameplay), with a note pointing to the L sidebar — the path that actually
  binds the seed to the new save slot.
- **The file-select screen now shows a "Press L for Port & Randomizer setup"
  hint.** The entire randomizer was previously reachable only by pressing L by
  luck or reading docs; the hint appears whenever the sidebar is closed and the
  port settings menu is enabled.
- **The F8 tab and the file-select sidebar now share one label/tooltip table.**
  The two entry points had drifted apart — the sidebar showed bare "Normal /
  Hard / Chaos" and "Ocarina Glitch" with no explanation while F8 had the full
  descriptions. Both now render the same self-explanatory pool/accessibility
  combo strings, glitch-trick labels, and help tooltips.

### Randomizer: dungeon-item shuffle, accessibility modes, seed fingerprint

- **Dungeon items (maps, compasses, big keys) can now join the shuffle.**
  New `shuffle_dungeon_items` setting (default off = pinned vanilla) adds 17
  dungeon-item chests to the pool (211 → 228 locations). A dungeon item found
  outside its home dungeon credits its ORIGIN dungeon, not the current area:
  the origin rides in the item subtype byte (`0x80 | dungeon_idx`) through the
  give path (`itemUtils.c` cases 5/6, `itemOnGround.c`), and the solver tracks
  each as a virtual item id (0x80..0xBF) so reachability tells Palace's big key
  from Deepwood's. Sidecar bumps to v7. Toggle it in the file-select overlay
  or the F8 → Randomizer tab (**"Shuffle dungeon items"**); persisted as
  `rando_dungeon_items` (default off).
- **Accessibility modes** matching the upstream dropdown: `GOAL` (historical —
  only the final boss must be reachable), `ALL_NONKEYS` (every enabled non-key
  check reachable), `ALL_LOCATIONS` (every enabled check reachable). Verifier
  is strengthening-only: a stronger mode never accepts a seed `GOAL` rejects.
  Selectable in the file-select overlay and the F8 → Randomizer tab; persisted
  as `rando_accessibility`.
- **Seed fingerprint** (`Rando_SettingsFingerprint`): FNV-1a over every
  placement-affecting setting, shown in the F8 tab **and the file-select
  sidebar before you generate**, so racers can confirm settings parity up
  front. Two players with the same seed AND the same fingerprint are
  generating the identical placement. Pure cosmetics and runtime-only QoL are
  excluded (they never change placement).
- **Glitch tricks are now individually selectable** in the F8 tab when
  glitchless logic is off (Ocarina Glitch / Crenel Clip / Portal Jump Storage),
  persisted as `rando_tricks`.
- **Fixed non-reproducible seeds from the file-select overlay.** The commit
  path built its settings from uninitialized stack memory for
  `shuffle_dungeon_items`, so the same seed+settings could generate a
  different world and a different fingerprint across launches; it now
  initializes from `Rando_DefaultSettings()`.
- **The file-select seed field no longer defaults to a fixed word.** It was
  pre-filled with `MINISH`, so every untouched file rolled the *same* world;
  it now starts empty and an empty field rolls a fresh random seed (matching
  the F8 tab).
- Verified: `rando_logic_test` (228 locations; accessibility beatable /
  strengthening-monotonic / reachability, and fingerprint stability +
  per-setting sensitivity incl. dungeon-items) ALL PASS.

### Widescreen: textbox borders no longer torn; right-edge black band gone

- **Fixed the centered dialogue box rendering with a missing right border and
  a torn bottom border in widescreen.** The rect published to the PPU for the
  message-box centering remap was one tile short of the engine's real frame on
  every edge (`DispMessageFrame` draws the border *inward* from
  `textWindowPos`, not around it). The shifted copy overdrew its own right
  border and the bottom border row fell outside the remap band, where the
  HUD right-anchor split it in two. Affects both the CPU and GPU rasterizers
  (one shared publisher).
- **Fixed the permanent black band on the right of the frame (which also
  swallowed the right-anchored HUD) when standing near a room's right edge.**
  The per-frame camera follow (`Scroll1`) and the room-entry camera
  (`InitializeCamera`) still centered/clamped for a 240 px view, so in wide
  view the camera parked up to `viewW-240` px short of the room's right edge
  and the remainder rendered as void. Both now center the target in the live
  view width and clamp the view to the room, matching the already-fixed snap
  camera (`sub_080809D4`); the follow target is also clamped so a camera
  parked past the live limit pans smoothly back instead of deadlocking.
  Native-240 builds keep the exact GBA branches.
- **Fixed widescreen cutscene freezes (intro festival pan over Hyrule Town,
  Zelda talk during the Business Scrub scene).** Scripted pans park the camera
  and then busy-wait (`WaitForCameraTouchRoomBorder`) for `scroll_x` to
  **equal** a rest position the script recomputes with GBA 240-px constants —
  while the widescreen follow camera rests at the live-view-width center, so
  the two never matched and the cutscene waited forever. All camera-rest
  producers (`Scroll1`, `InitializeCamera`, `sub_08080974`, `sub_080809D4`)
  and the script wait now share one formula
  (`Port_Widescreen_CameraRestX`), so they can't drift again. Regression
  probe: `TMC_ROOMCAP_PAN_PROBE=1` (deadlocks on the old code, passes now).

### VSync toggle now works on the SDL_GPU backend

- **The F8 → Display → VSync checkbox previously did nothing on GPU builds**
  (the default desktop configuration): `Port_PPU_SetVSync` only knew how to
  set vsync on the SDL_Renderer backend, and the SDL_GPU swapchain stayed in
  its default VSYNC present mode forever. The setter now dispatches per
  backend — SDL_GPU via `SDL_SetGPUSwapchainParameters` (IMMEDIATE preferred,
  MAILBOX fallback when the driver refuses tearing), SDL_Renderer via
  `SDL_SetRenderVSync`, window-surface fallback via
  `SDL_SetWindowSurfaceVSync`. This also makes fast-forward's temporary
  vsync-off actually take effect on GPU builds.

### Audio: crackle/lag on weak Android devices (Moto G4-class)

- **Fixed audio underruns on low-end Android.** On a Moto G4 (Cortex-A53) the
  music/SFX crackled and lagged under load. Profiled on-device: the agbplay/MP2K
  synth needs **~15-17 ms of CPU to render a 20 ms audio buffer** on the A53
  (~75-85% of realtime) once several PCM channels are live, so any scheduler
  jitter overran the buffer. It is **not** GPU-offloadable (audio is a tiny
  serial low-latency stream; a GPU round-trip adds more latency than it saves),
  not a render-thread-count problem (1-4 threads all underran), and not fixable
  by priority alone (Android denies SCHED_FIFO to unprivileged apps — the audio
  thread stays SCHED_OTHER, verified on-device).
- **Two targeted, hardware-scaled mitigations** (desktop and strong devices are
  unaffected): (1) the enhanced resampler drops from SINC to **LINEAR on Android**
  — a fraction of the MAC cost, still interpolated (no raw aliasing), imperceptible
  on a phone speaker; desktop keeps SINC. (2) The audio buffer **scales to the
  CPU**: a weak cluster (max clock < 1.8 GHz, the low-clocked in-order A53 class)
  gets a larger, jitter-tolerant buffer; faster SoCs (e.g. Galaxy Tab A7's 2.0 GHz
  A73s) keep the low-latency default. Together these cut G4 underruns from ~8-35/s
  to ~0-6/s. `TMC_AUDIO_FRAMES` / an `audio_frames` marker still override the buffer.
- **Mixer hot loop reordered** to track-outer / sample-inner so per-track gain
  and pan (loop invariants) are computed once per track instead of once per
  sample, and the muted/silent skip happens before the sample loop. Track
  summation order is preserved, so the float output is bit-identical. Trims
  per-buffer mixing cost, which matters most on the weak-cluster path above.
- GBA-accurate mode is unchanged (NEAREST resampling, hardware-exact) on all platforms.

### GPU rasterizer: the whole PPU now renders on the GPU (CPU is the fallback)

- **The GBA picture processor is reimplemented as a GPU fragment shader.**
  Backgrounds (all 4 text layers + affine BG2), sprites (regular + affine,
  1D/2D, 4bpp/8bpp, mosaic, double-size), windows, alpha/brighten/darken
  blending, priority, and the widescreen shadow-reveal all run on the GPU.
  Previously every pixel was rasterized on the CPU (`port/ppu/src/mode1.c`),
  which on low-end devices was 55–80% of frame time. The CPU freed of that
  work matters most on passively-cooled ARM handhelds.
- **Bit-exact with the CPU rasterizer.** The shader is integer-only and was
  built against a headless CPU-vs-GPU parity harness (`tools/ppu_gpu_parity`,
  42 scenes). On the real ROM the live GPU frames match the CPU golden hashes
  byte-for-byte (intro logo `0x7d7d…b393`, title `0x1dec…112b`).
- **On by default where the GPU backend is active, with automatic CPU
  fallback.** Any device/driver that can't run it — or the rare swamp-sink
  obj-clip the shader doesn't implement — silently uses the CPU rasterizer.
  Toggle under F8 → Display → "GPU rasterizer" (persists as `gpu_raster` in
  `config.json`). The pure-software build (`--gpu_renderer=n`) is unchanged.
- **All platforms:** SPIR-V on Vulkan (Linux/Windows/Android); Metal via MSL
  generated by spirv-cross (`port/shaders/build.sh`) on macOS/iOS, with CPU
  fallback until that MSL is committed. See `docs/gpu-rasterizer-design.md`.
- **Faster sprite path:** a per-line OBJ candidate cull (`port_gpu_obj_cull`)
  precomputes each scanline's visible sprites so the shader loops a short list
  instead of all 128 OAM entries per pixel — bit-exact, and it speeds the Vulkan
  path too.
- **OpenGL ES 3.1 compute backend (opt-in) for devices without Vulkan.** The
  same shader logic runs as a GLES compute shader (`port_gpu_raster_gl`), so
  GLES-only GPUs can rasterize on the GPU too — verified bit-exact and running
  on a Moto G4 (Adreno 405, which has no Vulkan driver at all). It's **off by
  default**: benchmarked there it's ~5× slower than the tuned 3-thread CPU
  rasterizer (the Adreno 405's weak compute + readback stall), so the automatic
  fallback stays Vulkan→CPU. Enable per-device via F8 → Display → "GLES raster
  (exp.)" (`gpu_raster_gles`) on a stronger GLES-only GPU. See
  `docs/gpu-rasterizer-parity-notes.md`.
- **Readback optimized:** the render and framebuffer download now share one
  command buffer / one fence wait (was two), −12% on the GPU raster path; a
  deferred double-buffered readback API (`SDL_QueryGPUFence` ring, 1-frame
  latency) cuts CPU-side cost −62% for callers that opt into the latency.
  Measured + externally corroborated finding: at native 240×160 the CPU
  rasterizer still wins (the frame is too small for upload+readback overhead to
  pay off — the same reason mGBA defaults to software); GPU raster is a latent
  win for high internal-scale / widescreen / a future direct-present path.
  Full numbers and citations in `docs/gpu-rasterizer-parity-notes.md`.
- **GPU supersampling — sharper rotated/scaled scenes at internal-scale.** With
  GPU raster active and internal render scale > 1, the whole PPU shader now runs
  at S× native (e.g. 960×640 at 4×) with true sub-pixel affine sampling, so
  affine backgrounds (rotating rooms, scaled effects) are genuinely finer
  instead of the old nearest-replicate blocks. Desktop-Vulkan only, off on
  GLES/low-end (the G4 can't afford the extra pixels — measured ~16% idle);
  bit-exact at scale 1 (all 76 harness scenes + ROM golden hashes unchanged).
  Skipped under LCD-persistence/perfcap and xBRZ (which owns its own upscaler).
- **GPU rasterizer is now scale-aware (auto CPU raster at native scale).** The
  GPU PPU rasterizer only engages when it produces the supersampled present
  buffer (internal scale > 1, a raw present mode, no LCD-persistence) — the one
  case where its compute amortizes the upload+readback. At scale 1 it now falls
  back to the CPU rasterizer automatically. Measured on-device (Galaxy Tab A7,
  Adreno 610, Vulkan) the old default-on GPU raster cost ~8 ms render / 58 fps
  in the forge; the CPU raster is ~2.6–4 ms / a solid 60 fps with byte-identical
  output — the same small-frame readback-overhead reason mGBA defaults to
  software. Perfcap keeps the GPU path live for its golden-hash parity (gate is
  a no-op there; parity hashes unchanged). Net: the quality win at high scale is
  kept, the silent fps regression at scale 1 is gone, on every device measured
  (G4 no-Vulkan, Tab A7 Vulkan). Manual override still via F8 → Display.
- **CPU rasterizer scales across more cores.** The software-PPU scanline pool
  was hard-capped at 6 threads — a stale 8-core-era value. On many-core hosts
  a heavy widescreen (384px) frame scales ~2× further (measured on a 22-core
  Ultra 7 155H, `ppu_bench`: 0.33 ms at 6 threads → 0.17 ms at 12), so the cap
  now scales with cores up to the workload's knee (12; the fixed 160-scanline
  work stops dividing usefully past ~16, and near-full subscription regresses
  it). Timing-only and bit-exact: `schedule(static)` over independent scanlines
  is thread-count-invariant, and the parity gate hashes are unchanged at every
  thread count. Desktop is normally VSync-bound so fps is unaffected there; the
  win is the CPU-bound case (weak many-core HW, uncapped fps, heavy geometry).
  `TMC_RENDER_THREADS=N` still forces a value. On Android the pool now sizes
  itself to the **detected performance-core count** at runtime — it reads each
  core's `cpuinfo_max_freq` and counts the cores above the slowest cluster (the
  big — and, on tri-cluster SoCs, prime+gold — cores), so it fills the fast
  cluster and never spills scanlines onto the slow in-order LITTLE cores (whose
  barrier wait is a net regression). This tracks the actual SoC instead of a
  hardcoded number: a quad-big phone gets 4, a hexa-big 6, a dual-big 2; a
  uniform SoC falls back to ncores−1. All capped at the 160-scanline knee (12).
  Measured per-device in-game (forge, CPU raster): Galaxy Tab A7 (4× Cortex-A73
  @2.0 + 4× A53) detects 4 and renders 4.50 ms @ 3 threads → **3.13 ms @ 4**
  (−30%; 5+ spill to the A53s and regress); Moto G4 (8× A53) detects 4, a wash
  (3.89 → 3.94 ms), neutral. Both hold 60 fps (VSync-bound) — the win is render
  headroom and cooler thermals (blocktime=0 sleeps the pool once the frame is
  done), not fps. A `render_threads` marker file (a decimal count in the app
  data dir, same convention as `profile`/`verbose`) overrides the detected count
  without a rebuild, for devices the heuristic misreads.

### True widescreen: the view now fits YOUR screen

- **The view width tracks the window's aspect ratio, every frame.** A 16:9
  monitor fills exactly (288×160 — zero letterboxing), an ultrawide caps at
  the framebuffer's 384 (2.4:1), and a 3:2/4:3 window gets the authentic
  GBA 240. Previously the wide frame was a fixed 2.4:1 strip that
  letterboxed ~26% of a 16:9 screen and pillarboxed in narrow rooms.
- **World scale is now constant.** Camera, culling, HUD right-anchor and
  textbox centering all follow the same live view width, so walking from a
  wide field into a 240-px interior no longer changes the zoom level — the
  view narrows, the pixels stay the same size.
- `--widescreen_width=N` now sets only the framebuffer *capacity* (the cap);
  the presented width is dynamic. Resize the window mid-game and the world
  reveal follows. `TMC_WS_VIEW_WIDTH=<px>` pins the width for headless
  captures.

### Widescreen: dialogue no longer snaps the viewport; cutscene overlays fixed

- **Textboxes keep the wide frame.** Opening any dialogue used to snap the
  viewport 384→240 (and back on close) — the single most jarring widescreen
  artifact. The world now stays wide during messages and the PPU centers the
  BG0 textbox in the wide frame (native columns suppressed, box redrawn
  shifted by `(W-240)/2`). Hearts stay top-left, rupees stay right-anchored;
  the right-anchor is suspended on box scanlines so a top-anchored box can't
  double-draw over the rupee rows.
- **Overlay screens no longer show a black right third.** The prologue
  storybook (and any screen that swaps the map BGs out, e.g. pause) now
  falls back to a native 240 crop via a shadow-liveness check
  (`Port_Widescreen_ShadowsLive`) instead of presenting a wide frame with
  dead columns.
- **Enemy edge behavior widened.** cuccoAggr despawn, gyorgChild fly-off,
  bombPeahat/takkuri re-entry now key off the live view width instead of
  hardcoded 240-based literals — no more enemies vanishing or popping in
  mid-screen inside the widescreen reveal. All collapse to the GBA values at
  native width.
- F9 bug-report captures now match the presented width during overlays.
- **Widescreen is now ON by default.** With the dialogue snap gone, overlay
  fallback in place, choice dialogs verified (the `▶Buy / Don't buy`
  selector is BG0 text and centers with the box), and per-room narrow
  fallback already shipping, the reveal is default-enabled on wide builds.
  Existing `config.json` preferences are respected; Console-Parity mode
  still forces it off; F8 → Display toggles it as before.
- **Reveal no longer leaks stale tiles at room edges.** The shadow-tilemap
  fill clamped only to the 128-tile map buffer, but the buffer is reused
  across rooms without clearing — reveal columns past the current room's
  rect could sample the *previous* room's tiles (stray bushes in the void).
  Now clamped to the room rect; outside = transparent, force-blacked like
  GBA's own void.
- **Per-room content-width guard.** If a room's painted map can't fill the
  wide view even with the camera pinned left (rightmost non-empty tile
  column < view width), widescreen falls back to stretched-240 for that
  room — GBA-identical framing instead of a permanent void strip. Ratcheted
  per room so late-streaming map data can't flip modes mid-room. (Survey of
  the suspect rooms — field 480px, festival 400px — showed TMC paints
  edge-to-edge, so this is a safety net, but `TMC_WS_TRACE=1` logs the
  signal per room for anyone hunting a counterexample.)

### Android: the port now runs on phones and tablets

- **The Minish Cap PC port is now a native Android app.** The whole engine,
  software PPU, agbplay audio and ImGui UI cross-compile for the NDK
  (arm64-v8a / x86_64) and ship as an APK. First launch self-extracts assets
  from a ROM on device; the game boots to title, audio, file-select and into
  gameplay entirely by touch.
- **Releases now ship a ready-to-install APK** (`tmc-multi-android-vX.Y.Z.apk`,
  arm64-v8a + x86_64 in one file, multi-region like the desktop builds).
  Install it, place your ROM via the in-app file picker (or push
  `baserom.gba` to `Android/data/dev.picori.tmc/files/`), and the first
  launch extracts assets on device. Release APKs share one signing key, so
  future versions install straight over the old one.
- **Touch controls.** A floating analog stick, a face-button cluster with the
  R button and a context glow, and a finger-draggable, scaled F8 debug menu —
  designed so a single tap that arrives as a down+up in one input batch still
  registers.
- **SAF ROM picker.** Point the app at a ROM through the system file picker;
  it's copied into app storage and detected on the next launch.
- Fixes found on real hardware: a first-launch display freeze (a
  use-after-free that destroyed the live renderer after extraction), and
  NPC-talk / collision / animation guards that were dead on 39-bit-VA-kernel
  devices (the pointer check assumed a 64-bit desktop address layout).

### Performance & audio: locked 60 on low-end ARM, VSync on by default

- **Sustained 60 FPS on a 2016 Moto G4 (Snapdragon 617).** Four independent
  causes fixed: C files were building `-O0`, the OpenMP scanline render
  oversubscribed the little cores (now capped to 3 workers on the big
  cluster), widescreen was defaulting on (more pixels than the chip can
  afford — off on Android), and the GBA-LCD colour filter's per-pixel pass is
  now off by default there.
- **Frame pacer no longer busy-waits.** A sleep-based pacer plus passive
  OpenMP waits replaced two spin loops that burned ~590% CPU on the G4 —
  heat, throttle and jank on passively-cooled devices.
- **Software PPU rasteriser optimised, byte-exact.** A per-frame ABGR palette
  LUT (replaces per-pixel 5→8-bit conversion), the composite layer search
  collapsed from a per-pixel priority×background double loop into one merged
  pass (~25% off heavy scenes: message boxes, multi-BG rooms), and per-tile
  constants hoisted out of the background pixel loop. All gated by a
  four-scene checksum oracle on x86_64 and ARM — zero rendering change.
- **VSync is enforced on by default.** The 60 FPS cap now applies to any
  config without an explicit frame-rate choice, so VSync is actually
  effective out of the box (an uncapped default silently disabled it). Pick
  "uncapped" in the FPS menu to opt out; fast-forward still bypasses it.
- **Item-get audio no longer freezes, lags, or crackles on Android.** The
  game thread is decoupled from the audio render lock via a try-lock fast
  path (arms the fanfare on the same tick when the lock is free, never blocks
  the game tick), and SDL's audio thread is promoted to realtime scheduling
  so it isn't starved by the render workers on contended low-core devices.
  A `TMC_AUDIO_FRAMES` override (env or an `audio_frames` file in the data
  dir) lets a stronger device trade buffer size for lower latency.

## v0.7.1.1 (2026-07-02)

### Cave of Flames: rollobite-switch gate fixed (issue #159, regression in v0.7.0)

- **Hitting the seated cog-lever in the Cave of Flames rollobite room now
  opens the bollard gate again.** v0.7.0 moved `HittableLeverEntity.hitFlag`
  to the 64-bit-safe offset (PC 0xB2, aliasing the loader's
  `field_0x86` union) but missed the *runtime* spawn path: when the pushed
  cog seats and converts itself into a hittable lever
  (`objectOnPillar.c` → `CreateObject(HITTABLE_LEVER)`), the flag was still
  written at the old offset (0xAE). The lever then read hitFlag = 0, toggled
  local flag 0 instead of the gate flag, and the SECRET_MANAGER → bollard
  chain never fired — blocking dungeon progress. ROM-placed levers (e.g. the
  rail switch in the main cart room) were unaffected, which is why only this
  room broke. `EntityWithHitFlag` now mirrors the lever struct's PC padding,
  pinned by a static assert.
- New env-gated repro probes (`TMC_ROOMCAP_COF_PROBE`,
  `TMC_ROOMCAP_LEVER_PROBE`) verify the seated-chest open path and the
  lever → secret → bollard chain headlessly.

## v0.7.1 (2026-07-02)

### EU: global color corruption fixed (every faded palette, all EU rendering)

- **The long-standing "garbled EU title screen" was two stacked bugs and was
  never title-specific** — colors were subtly-to-badly wrong across all EU
  rendering:
  - The palette fade engine resolved its brightness/fade lookup tables at the
    USA/JP ROM offset (`0xF84`); EU's tables sit at `0xFCC`. Every palette row
    that passed through a fade on EU (i.e. nearly everything) was translated
    through garbage tables. Now region-corrected.
  - The extracted EU asset cache slices palette-group data half a palette off;
    EU palette groups now load from the ROM directly (always exact).
- EU title + intro now render pixel-identical to USA. USA/JP were never
  affected.

### Multi-region is now the default everywhere (M7)

- `xmake` builds default to the fat multi-region binary (`--multi_region=n`
  restores the classic per-region build); dev builds now match releases.
- Release artifacts renamed **`tmc-usa-*` → `tmc-multi-*`** — the binary has
  been multi-region since v0.7.0; the filename now says so. One download plays
  USA, EU, and JP ROMs.
- CI now boots the built binary headless against every available region ROM
  and requires each to reach the game main loop, and runs the PPU parity gate
  for JP in addition to USA/EU.

### Multi-region correctness (EU/JP)

- **Fusion rewards fixed on EU/JP**: kinstone fusion reward flags (chest
  spawns, map markers, world events) were written to the wrong save bits.
- **Pause-menu dungeon maps fixed** (EU everywhere; JP in Palace of Winds and
  Dark Hyrule Castle), **EU Hyrule Town tiles**, and **EU+JP Gyorg boss room
  layouts** — compiled USA data offsets are now remapped to the loaded
  region's layout at runtime.
- **Dust/poof effects restored on EU/JP** (a generated script-function table
  dropped `CreateDust` for both regions).
- **Picolyte drop bonuses actually work now** — the drop-modifier table
  lookup was reading unrelated data on all regions of the PC port.
- ROM-sourced NPC scripts no longer mis-translate through the USA script
  address table (30 EU / 5 JP address collisions).
- EU in-game language choice is no longer overwritten every frame.

### Randomizer correctness

- **Verified-beatable seeds are now actually beatable on Hard/Chaos**: items
  at named locations were re-randomized a second time at pickup, desyncing
  the spoiler log and the beatability check from what you actually receive.
- **Entrance shuffle: Temple of Droplets door gate fixed** — the Flippers
  requirement now applies to the physical door in Lake Hylia's water, not to
  whichever dungeon got shuffled there (seeds could otherwise place required
  progression behind an unreachable door).
- Fresh vanilla saves no longer inherit entrance/music shuffle from a
  previously played seed; sidecar saves persist and restore the full settings
  (homewarp, obscure locations, starting sword, ...).
- A crash between seed creation and the first save no longer leaves a
  softlocked grant-less file (self-heals on next load).
- "Randomize" seed button no longer produces the same sequence on every
  install.

### Save & stability (PC port)

- **In-game saves no longer hitch**: one save wrote the save file ~324 times
  back-to-back (up to multi-second stalls on slow disks); it now writes once.
- Disk-full / IO errors during save are reported as a failed save in-game
  instead of silently pretending success.
- Savestates are region-tagged with per-region filenames — a USA state can no
  longer load into a JP session and contaminate the JP save.
- Entity collision list capacity now matches the GBA (fixes rare
  non-collidable entities in packed rooms); Moldorm no longer crashes when
  the entity pool is full mid-split.

### PPU accuracy

- Sprite-vs-sprite overlap now resolves by OAM index like hardware (fixes the
  classic wrong-sprite-on-top quirk).
- Semi-transparent sprites now fade correctly during brightness fades.
- OBJ-window contents no longer go stale when sprites are disabled mid-frame.
- Affine backgrounds pinned by constant HBlank-DMA now latch like hardware.
- **OBJ mosaic implemented** (attr0 bit 12 — previously ignored).
- Perf: wrap-around affine backgrounds dropped two divisions per pixel;
  ~150K dead stores removed per frame.

### Roll attack macro (PC port QoL)

- One-button **start-of-roll attack**: hold a direction and press the bind
  (default **D** / **R3** right-stick click). Uses your best owned sword
  without changing A/B equip — same virtual-item pattern as soft slots.
- Toggle in **F8 → Controls** (`roll_attack_macro` in `config.json`).
- Rebindable under **F8 → Controls → Roll attack**.

## v0.7.0 (2026-06-26)

### JP/EU: intro cutscene & prologue Business Scrub fixed (full script-address translation)

- **Fixed two JP-only bugs**: the opening cutscene placed Zelda in the wrong
  position (the camera follows her, so the whole framing was off), and the
  prologue Business Scrub never spat its Deku seeds. Root cause was a single
  gap: the per-region script-address translation (`Port_TranslateScriptAddr`,
  `port/port_script_addrs.c`) only covered the ~100 `GBA_script_*` macros in
  `port/port_scripts.h`. Raw entity-data blobs in `port/data_const_stubs.c`
  embed baked **USA** addresses for many *other* scripts — e.g.
  `script_ZeldaOutsideLinksHouse` and the scrub orchestrators
  `script_080157AC` / `script_08015B34` / `script_ZeldaIntroBusinessScrub`.
  On EU/JP those untranslated addresses resolved to the wrong ROM bytes, so the
  affected entities ran garbage bytecode (Zelda never positioned herself; the
  scrub's orchestrator never raised the sync flag its spit script waits on).
- **Fix**: the translation table now covers the **entire script bytecode
  section** — all 576 data scripts, derived by exact symbol lookup across the
  upstream USA/EU/JP maps — so any baked USA script address is remapped to the
  active region before the ROM-data resolve (335 differ on JP, 575 on EU). USA
  is unaffected (`Port_TranslateScriptAddr` early-returns); the 102
  previously-verified entries are unchanged and regression-guarded against
  `port/port_scripts.h`.

### Software PPU vendored in-tree (`port/ppu`); ViruaPPU submodule + patch pipeline removed

- **The software GBA PPU is now first-party source under `port/ppu/`**, licensed
  GPL-3.0-or-later. It replaces the `libs/ViruaPPU` git submodule and the
  `xmake.lua` `before_build` step that re-applied 15 `port/patches/viruappu-*.patch`
  on every build. This **resolves the PPU's license blocker** (the submodule
  published no license — incompatible with the project's GPL-3.0) and eliminates
  the fragile dirty-submodule + `git apply` model (the patch set no longer cleanly
  re-applied onto pinned upstream `5cf5e99`; the `mode2-affine-latch` hunk
  conflicted). Provenance and the 15 patches are recorded in `port/ppu/README.md`
  and git history. See `THIRD-PARTY-LICENSES.md`.
- **Verified byte-exact.** A new two-tier parity oracle (`tools/ppu_parity.c` +
  `tools/ppu_parity_check.sh`, golden in `tools/ppu_golden_hashes.txt`) confirms
  the vendored build produces identical render output to the previous
  submodule+patches build across USA/EU/JP. The renderer is integer-only so the
  hashes are portable; a CI gate runs it on every Linux x86_64 build with ROMs.
  `TMC_PERFCAP_AT_FRAME=N` was added to the perfcap harness to capture
  boot/title screens (incl. the GBA affine title) for the corpus.
- Contributor workflow change: edit `port/ppu/` directly (no patches, no
  submodule reset) and guard changes with `tools/ppu_parity_check.sh`. See
  `CONTRIBUTING.md` → "Renderer changes (port/ppu)".

### Relicensed to GPL-3.0; honest attribution for randomizer & Reborn features

- **The project is now licensed under the GNU General Public License v3.0**
  (previously the Anti-Capitalist Software License v1.4). See `LICENSE`.
- **The randomizer (`port/rando/`) and the "Reborn"-parity QoL features are now
  attributed as derivatives of their GPL-3.0 upstreams** — the Minish Cap
  randomizer (`minishmaker/randomizer`) and Admentus64/The-Minish-Cap-Reborn —
  and are distributed under the GPL-3.0. See `THIRD-PARTY-LICENSES.md` and
  `docs/reborn-parity.md`.
- **Removed the earlier "first-party / clean-room / no GPL-3.0 obligation"
  framing** from `LICENSE`, `THIRD-PARTY-LICENSES.md`, `README.md`, and in-code
  comments. That characterization was incorrect: the features derive from
  GPL-3.0 projects, and the project now honors the copyleft by being GPL-3.0
  with attribution rather than reimplementing to avoid it.
- `libs/ViruaPPU` / `libs/VirtuaAPU` (authored by collaborator MatheoVignaud)
  still need a GPL-compatible license for a fully license-clean GPL build —
  tracked as an open item.

### Multi-region — one PC binary plays USA, EU, and JP ROMs at runtime

- **A single build now runs any retail region.** The region is detected from
  the loaded ROM at boot (`gActiveRegion`) instead of being baked in at compile
  time. The historical `#ifdef EU` / `#ifdef JP` gameplay, data, and text
  divergences are now runtime `REGION_IS_*` branches (`include/region.h`,
  force-included); region-exclusive functions are compiled for all regions and
  dispatched at their call sites, and per-region data tables keep a USA baseline
  plus `_eu`/`_jp` twins selected at the read site. Enabled by default in
  `build.py`; `--multi_region=n` still produces a lean single-region build that
  byte-matches the original decomp.
- **Save data is region-correct.** The EEPROM save signature is chosen at
  runtime (`ZELDA 5` for USA, `ZELDA 3` for EU/JP), and saves stay isolated per
  region (`tmc.sav` / `tmc_eu.sav` / `tmc_jp.sav`).
- **Save-flag ordinals are remapped per region.** The local-flag banks
  (`include/flags.h`) enumerate different flags per region, so the same logical
  flag lands on a different save bit in EU/JP than in USA. A generated
  baseline->region ordinal remap (`tools/generate_flag_remap.py` ->
  `port/flag_remap_generated.c`, identity on USA) keeps compiled C flag
  references in sync with the loaded EU/JP ROM's script/area data — the
  prerequisite for EU/JP correctness and real-hardware save compatibility.
- **Region-accurate behavior restored where the port had drifted:** prologue
  BGM at Castle Garden / Hyrule Field (USA/JP "Climbing the Beanstalk" +
  CASTLE_BGM flag vs EU's festival theme), the EU figurine-shell drop table (an
  off-by-one that silently gave EU the USA drop weighting), per-region
  enemy/projectile/NPC/object data, world-event tables, song-player routing,
  title-screen timers, and area exit lists (EU/JP exits resolved from the active
  ROM rather than the USA compile-time tables).
- Verified: clean build under `--multi_region=y`; USA (BZME), EU (BZMP), and JP
  (BZMJ) ROMs each boot headless to `AgbMain`, sustain autoplay, and load their
  own region save (signature round-trip), with no crashes or sanitizer hits.
  Deep in-game EU/JP gameplay parity still wants a manual play pass.

### Randomizer — embedded logic database; no external file needed (#155)

- **The randomizer no longer needs a separate `.logic` file.** A new
  embedded database (`port/rando/native.logic`, compiled into the binary
  via bin2c) gives every install per-location keyed placement +
  reachability out of the box: 196 items over 244 locations — all 138
  engine-derived ground-item keys and 73 scripted reward keys from
  `rando_keymap.c` bind 1:1 — with the settings the runtime consumes
  (OPENWORLD dropdown, sleep warp, start sword, early wind crests, fast
  text).
- **Licensing**: the randomizer derives from the GPL-3.0 Minish Cap randomizer
  and is distributed under the GPL-3.0 with attribution (see `LICENSE` /
  `THIRD-PARTY-LICENSES.md`); the embedded database is authored for this port
  from the decompilation. A user-supplied upstream file
  (`TMC_RANDO_LOGIC=/path` or `assets/rando/default.logic`) keeps precedence
  and still offers its full 882-location coverage; `TMC_RANDO_LOGIC=none`
  falls back to the old built-in graph.
- Softlock-free by construction: requirements err strict, quest-gated NPC
  rewards only ever hold non-progression items, dungeon keys stay vanilla,
  and the smith's sword is granted at file creation. Open world pre-solves
  obstacles at runtime while generation keeps the strict requirements
  (extra access only — never a stranded item).
- Verified: offline `rando_logic_test` (deterministic beatable seeds over
  244 locations, OPENWORLD override drives the `openWorld` eventdefine,
  binding asserts) and the headless `TMC_REPRO_RANDO=1` harness in all
  three modes — embedded, upstream file, and `none` — including chest
  persistence round-trip and the sleep-warp e2e on the embedded database.

### Randomizer — Open world setting + new-file baseline parity (#155)

- **Open world now actually opens the world.** The `OPENWORLD` World
  Setting (and the "Open world (fast)" preset) previously only relaxed
  generation logic: seeds placed progression behind cut trees, cracked
  blocks, bomb walls, webs, switches, and locked shortcuts that still
  existed in-game — a softlock. The `openWorld` eventdefine is now
  consumed at new-file commit: a 403-entry named-engine-flag table
  (`port/rando/rando_newfile.c`) pre-solves every permanently solvable
  obstacle — cut trees, cracked blocks, bomb walls, boulder shortcuts,
  non-key doors, bean vines, switches, levers, chest spawns, extendable
  bridges — plus marks the world map visited and unlocks Dampé's
  graveyard gate, matching the GBA randomizer's `worldOpen` new-game
  table byte-for-byte (asserted by `rando_logic_test` under USA flag
  numbering; the named flags compile to the correct bits on EU too,
  where a raw byte image would have corrupted saves).
- **Open world is also a built-in option.** Native-graph seeds (no
  `.logic` file) get an "Open world" checkbox in the file-select overlay
  and the F8 Randomizer tab, persisted as `rando_open_world` in
  `config.json` and in the per-slot `.randomizer` sidecar.
- **Every rando file now starts from the upstream new-game baseline.**
  The unconditional `startingFlags` blob is applied as a 47-entry named
  flag table on top of the existing story skip — including baseline
  obstacle removals that default `.logic` reachability assumes (e.g. the
  Royal Valley graveyard bomb wall), which could previously strand
  placements on no-bomb seeds. Upstream QoL parity comes with it:
  first-pickup item popups pre-seen, pause menu usable before any sword,
  world map item + full map reveal, kinstone bag visible, all figurines
  owned (unless the figurine-hunt goal is on), cucco minigame
  pre-skipped per the `CUCCO_*` setting, sanctuary stone NPCs unfrozen,
  unshuffled pot/dig/underwater kinstone locations guarded, pre-opened
  dungeon portals' switches marked pressed, and `blueGinaGrave` honored.
- The headless `TMC_REPRO_RANDO=1` harness now asserts the full chain on
  the real `default.logic`: `OPENWORLD=OPENWORLD_ON` override → reparse →
  beatable generation → obstacles actually open at new-file commit; plus
  baseline/QoL state and the native toggle path.

### Randomizer — native graph made canonical; `.logic` demoted to optional import

- Reframed `port/rando/` so the **native location graph is the canonical
  randomizer** and the public-format `.logic` parser is an explicitly
  **optional importer** behind an alias table. The import alias step
  (`rando_keymap.c`) already no-ops unless a `.logic` file is imported; renamed
  its tables to `kLogicImportGroundKeys` / `kLogicImportScriptedKeys` and
  documented that the leading strings are import-only aliases while the
  canonical identity is the engine-native runtime key (derived from the USA
  baserom + decompilation).
- Provenance language across `port/rando/README.md`, source headers, `LICENSE`,
  `README.md`, and `THIRD-PARTY-LICENSES.md` now describes the randomizer as
  **derived from the GPL-3.0 Minish Cap randomizer (`minishmaker/randomizer`),
  distributed under the GPL-3.0 with attribution** (superseding earlier
  "independent reimplementation / clean-room" wording).
- No generation behaviour change: `rando_logic_test` and the native graph
  (36 locations) pass unchanged; `tmc_pc` builds clean.

### Randomizer — open world, story skip, homewarp (#155)

- **Story skip**: every randomizer file now starts post-intro — Link wakes
  at home with Ezlo on his head, the festival/castle/Minish-door story
  already done (the engine's own canonical post-intro flag set from
  `src/title.c`'s demo save). Items are untouched: the shuffled pool still
  governs what you find.
- **World-opening settings now work.** The `.logic` `m<hex>` eventdefines
  are applied as save pokes (with GBA→PC layout translation), which makes
  the Gold/Red/Blue/Green **"Fusions are Open"** World Settings and the
  Castor Wilds shortcut genuinely take effect, including their beanstalk
  and Cloud Tops tornado side effects. The **Speed Up** flags are consumed
  too: Wind Tribe Tower open, Tingle brothers spawned from the start, and
  the library/book quest active from the start.
- **Homewarp** (`HOMEWARP`, default on): press **SELECT on the Quest
  Status pause screen** to sleep — Link warps back to his bed, matching
  the GBA randomizer's SLEEP option. Refused while minish (that would
  softlock); a `SELECT: SLEEP (WARP HOME)` hint appears on the pause
  overlay when available.
- The headless `TMC_REPRO_RANDO=1` harness gained two stages: a
  world-open/story-skip state check, and an end-to-end homewarp run that
  boots a new rando file from the title screen, warps away, and asserts
  the SLEEP warp lands back at the bed. The CHAOS stage now proves the
  glitchless pin both ways (was silently broken by the #155 glitchless
  guard when no `.logic` file is present).

### Randomizer — glitchless beatability + persistent settings (#155)

- **Glitchless now guarantees a beatable seed on the built-in graph.** The
  seed-scoped item bijection (`Rando_OverrideItem`) applies to every
  non-keyed story give and is not covered by placement verification, so the
  Hard/Chaos pools could scramble story gates — e.g. the shield (needed for
  Deepwood Shrine's Business Scrub) rolling into a boomerang. With
  "Glitchless logic" checked, Hard/Chaos now only scramble non-gating
  collectibles; full major/progression scrambling requires unchecking it.
  The F8 tab and the file-select modal say so inline, and
  `rando_logic_test` gained a regression guard for both directions.
- **Randomizer settings persist** until switched off, instead of resetting
  on every menu open and every launch. "Enable Randomizer Mode", the
  built-in toggles (glitchless / kinstones / dojos / item pool), and all
  `.logic` setting overrides (settings browser edits, presets, cosmetics)
  are stored in `config.json` (`rando_*` keys) and restored at startup.
  Turning "Enable Randomizer Mode" off resets everything back to vanilla
  defaults. The new-file modal no longer resets `.logic` settings to file
  defaults when it opens.

## v0.6.0 (2026-06-11)

### Licensing — remove GPL-3.0 randomizer dependency

- Removed the `libs/randomizer` submodule (GPL-3.0 minishmaker/randomizer)
  and everything that built or invoked it: the `randomizer_cli` xmake target,
  the `port/port_randomizer.{cpp,h}` shell-out, the F8 → Randomizer debug-menu
  page, the `build.py` dist staging, and the now-orphaned `tools/randomizer_usa/`
  USA-patch generator.
- The in-game randomizer is now solely the project's own clean-room
  reimplementation under `port/rando/`, covered by the project license.
- With no GPL-3.0 component bundled, linked, or invoked, the project's own code
  is unambiguously under the Anti-Capitalist Software License. Updated
  `LICENSE`, `THIRD-PARTY-LICENSES.md`, `README.md`, and `AGENTS.md` to match.

### Accessibility — audio navigation cues (Phase 1)

- **Intro/prologue text-to-speech**: the opening "legend of the Picori"
  story panels (`src/cutscene.c`) are now read aloud as each page appears.
  They render through `ShowTextBox` (not the MessageMain dialog pipeline),
  so they previously bypassed TTS; hooked via `Port_TTS_SpeakTextIndex`,
  matching the existing dialog / room-name / menu TTS sites.
- **Surroundings scan (F10)**: a new on-demand cue for blind / low-vision
  players. Press F10 in game (or the F8 → Accessibility "Scan surroundings"
  button) to hear nearby chests, collectible pickups (rupees, hearts,
  kinstones, keys, bombs, arrows, fairies), NPCs and animals, enemies, and
  room exits — each spoken as "label, direction, distance in tiles",
  nearest first (`port/port_a11y_cues.c`). Reads the live entity pool plus
  the room's transition list; a no-op outside gameplay or when TTS is off.
- Headless regression guard `port/port_repro_a11y.c` (`TMC_REPRO_A11Y=1`)
  warps into a room, spawns known points of interest, and asserts the scan
  classifies and locates them (`TMC_A11Y_DEBUG=1` echoes the spoken phrase).

### Randomizer — MinishMaker 1:1 parity pass

- **Full per-location coverage**: every reward location in MinishMaker's
  `default.logic` now has a native keyed identity (331 keyed locations at
  default settings, up from 244). New `rando_keymap.c` rows — all
  triple-verified against USA ROM room entity data and the per-block EU→USA
  address deltas — cover every overworld heart piece, dig spot, rock item and
  rupee-cave item, the Cloud Tops dig kinstones and kill rewards, Royal Crypt
  gibdo/key drops, fight-completion key drops (FallingItemManager), the
  Temple of Droplets entrance ice-block keys, and the Lost Woods chest.
- **New grant-site hooks**: boss heart containers
  (`src/object/heartContainer.c` — Deepwood/CoF/Fortress/Droplets/Palace
  `*_BossItem`), the Fortress prize ocarina (`src/object/bird.c`), the Hyrule
  Town bell heart piece (`src/object/graveyardKey.c`), Tingle's trophy, the
  DHC B2 king reward, and Simon's Simulation heart piece (script command
  dispatcher in `src/script.c`).
- **Smith-house floor items**: the two `.logic` floor locations the GBA
  randomizer creates by rewriting furniture records are spawned natively at
  room load when an active seed places rewards there (`src/roomInit.c`,
  bank-verified free flags 0xE0/0xE1).
- `TMC_RANDO_DEBUG=1` logs keymap binder misses by location name; the real
  `default.logic` diagnostic now asserts the new boss-container, fight-drop,
  ice-block, and one-off bindings.
- **Generation parity with the MinishMaker shuffler** (clean-room, from the
  public `.logic` spec — no GPL code read): dungeon-id tag binding now drives
  the full keysanity matrix (Own Dungeon / Own Region / Vanilla pins / Removed
  for keys, maps, compasses, prizes — all expressed as data in the logic
  file); `!prizeplacement` redirects execute (elements shuffle within their
  dungeon under 'Own Dungeon' elements); prize items place only on
  DungeonPrize locations per spec (fixed a fallback that could drop them
  directly into dungeon slots); `!eventdefine` is parsed and evaluated with
  per-seed `RAND_INT` substitution and a C-like expression evaluator;
  `!color` parses defaults and override-driven defines; dropdowns support 24
  options (THREE_HEART no longer truncated).
- **Eventdefine-driven runtime features** (`rando_runtime.c`): start
  inventory (full `startInventory*` mapping — swords, tools, elements,
  scrolls, bottles incl. randomized contents, kinstones, bomb bags/quivers/
  wallets with counts), wind crest unlocks, dungeon warp states, instant
  text, `dmgMulti`/`heroMode` damage scaling at the ModHealth choke point,
  low-health-beep and music mutes. Applied once at rando file creation and
  refreshed on every save activation.
- **Coupled dungeon-entrance shuffle** (`rando_entrance.cpp`): generation
  records `Items.Entrance.*` assignments; runtime hooks at the engine's
  transition choke points swap dungeon entrances in coupled pairs. Vanilla
  behavior is untouched when no entrance shuffle is active.
- **Cosmetics** (`rando_cosmetic.cpp`): tunic and heart colors from `!color`
  eventdefines via content-addressed palette overrides (verified against the
  vanilla palette bytes), including rainbow hearts at upstream's 12-frame
  cadence.
- **Sidecar format v2**: each slot now persists the `.logic` define overrides
  the seed was generated under plus its entrance assignments, so quitting and
  reloading restores damage multipliers, cosmetics, and entrance shuffle —
  previously these silently reverted to file defaults after a restart. v1
  sidecars are rejected with a clear log line.
- New `rando_logic_test` parity suite: tag binding, prize redirects,
  eventdefine evaluation (incl. RAND_INT determinism), entrance assignment
  recording. Full 882-location `default.logic` generation and the
  `TMC_REPRO_RANDO` harness (save/reload round-trip) stay green.
- **Dropdown option-value flags** (parity bug fix): choosing a dropdown option
  now defines the option's VALUE token as a flag, which is what the file's
  `!ifdef - SMALL_KEYS_STANDARD` / `MUSIC_RANDO` branches key off — without
  it, the keysanity define chains and music section were silently inert.
- **Music shuffle** (`MUSIC_RANDO`): per-area BGM assignments generated from
  the `Items.Music` pool, persisted in sidecar v3, applied at the engine's
  single `queueBgm` reader. Same song-id space as the GBA sound enum;
  out-of-range ids fall back to vanilla with a warning.
- **Ground-item location keys**: 49 curated dungeon rupee/pot/underwater
  locations (triple-verified against USA ROM room data) now resolve
  per-location instead of via the global bijection — keyed coverage in the
  repro probe rose from 161 to 206 locations. Also fixes the ground-item
  hook running only inside the item-get-cutscene branch, which skipped
  repeat-denomination rupee pickups.
- **Cosmetics UI**: the F8 Randomizer tab exposes `!color` settings (heart,
  heart outline, tunic, split-tunic) as live color pickers with per-setting
  enable toggles; edits apply immediately to an active seed. Spoiler log now
  honors `:NoSpoiler` tags.
- **Scripted reward location keys**: added a high-bit runtime-key namespace for
  non-area/room rewards and threaded per-location overrides into Stockwell's
  shop slots, Blade Brothers dojo rewards, Carlov's medal, the Hylia dog
  bottle, the Minish Village barrel-house Jabber Nut, the three library books,
  Melari's broken-sword reward, the shoe-shop Pegasus Boots, the Witch Hut
  mushroom, the Bomb Minish bomb bag, Minish/Crenel/Valley Great Fairy
  rewards, Valley Dampe's graveyard key, Biggoron's mirror shield, the library
  yellow-minish reward, the Town Cafe lady kinstone, the Crypt prize, Gregal's
  shells + light arrows, the Deepwood/CoF/Droplets/Palace dungeon prizes,
  Business Scrub item sales, plus opt-in Goron Merchant sets / Cucco rounds /
  Bomb Minish remote bombs. The real-file diagnostic now binds 38/68 scripted
  keys at stock settings and 67/68 under `GORON_5` +
  `VANILLA_BLUE_FUSIONS` + `VANILLA_RED_FUSIONS` + `BIGGORON_NORMAL` +
  `CUCCO_10`; subtype-aware external rewards persist shell counts, kinstone
  piece ids, and dungeon-item ids through sidecar v4, and the headless repro
  probe rises from 206 keyed / 195 overridden to 244 keyed / 243 overridden at
  stock settings.
- Honest remaining gaps documented in `port/rando/README.md`: `!import`
  approximation (unused by default.logic), remaining NPC-script / fusion
  reward sites without stable native identities, and placement not
  byte-identical to the C# PRNG by design.

### Fixed

- **File-select randomizer setup modal (keyboard + small windows).** Enter now
  generates & starts: pressing Enter in the seed field commits directly
  (`EnterReturnsTrue`), and a window-level Enter fallback fires when no widget
  owns the press — restoring the pre-ImGui overlay's "Enter = generate & start"
  behavior that was lost in the ImGui move (the footer hint claimed it worked).
  The modal now clamps to the viewport and shrinks its settings list on small
  windows, so the Generate/Cancel row can no longer fall off-screen at low
  window scales. Auto-open is gated on `Port_ImGui_CanPresent()`: on the
  surface fallback backend (or if ImGui init failed / is disabled) the
  new-file flow stays vanilla instead of opening an invisible modal over
  masked input (= softlock).
- **File-select "L Settings" sidebar could not be closed with L.** Opening the
  Port & Randomizer setup sidebar with L flips `Port_RandoFileMenu_IsOpen()`
  true, which masks all GBA input and makes `Port_PumpEvents` swallow the next
  L press before the game's own handler sees it — so a second L never closed
  the panel, and the close path mistook the sidebar for the forced new-file
  modal (calling its no-op `Cancel()` and hiding the "Close Sidebar" button).
  The sidebar now toggles shut on a second L press (handled in
  `Port_PumpEvents`, skipped while the seed field has keyboard focus so the
  L-bound key can still be typed), and Esc / Gamepad B / the on-screen
  "Close Sidebar" button work again.
- **Em-dashes in ImGui UI strings rendered as `?`** (the bundled font has no
  U+2014 glyph) — all user-visible menu/modal strings now use ASCII dashes.
- `TMC_REPRO_RANDO` harness gained an ImGui keyboard stage: it now drives the
  real modal with synthetic SDL Return-key events through the live event pump
  and asserts the typed seed goes active, covering the full user-visible
  keyboard path end-to-end.

### Improved

- **F8 → Randomizer tab overhaul.** Settings are now controllable from the tab
  (item pool Normal/Hard/Chaos, glitchless logic, kinstone/dojo shuffles)
  instead of always rolling with defaults; seeds are full 64-bit and accept
  text (hashed, shareable phrases); failed rolls report why (unbeatable after
  32 attempts / bad settings / internal) instead of silently keeping vanilla.
  Status block shows the logic source (.logic file with counts, built-in
  graph, or the parse error) and the active seed's pool. The spoiler log is
  collapsed by default, scrollable, copyable to clipboard, and no longer
  truncated through a 1 KB status buffer. Removed the now-unused
  `Rando_RollSeed` default-settings wrapper.

### Compatibility

- **Save files are now mGBA/VBA-M compatible, both directions.** `tmc.sav` (and
  profiles) are written in the byte order emulators use for EEPROM saves: each
  8-byte block in wire-transmission order, i.e. byte-reversed relative to game
  RAM (the GBA driver shifts units out `data[3]→data[0]`, MSB-first — the
  well-known GBA EEPROM `.sav` quirk). A Minish Cap save from mGBA drops in as
  `tmc.sav` unchanged, and `tmc.sav` loads in mGBA when renamed to match the
  ROM. Saves from older Picori builds are detected via the `AGBZELDA` signature
  block and migrated once on load, with the original preserved as
  `tmc.sav.bak`. Verified by byte-exact round-trip plus headless boots of both
  formats (game accepts the save; file untouched on reload).

### Fixed

- **Save flush no longer silently loses data on write failure.** `FlushEepromFile`
  previously cleared the dirty flag even when `fwrite` came up short (disk full,
  I/O error), so the flush was never retried and the save was lost. The flag now
  survives failed writes (checked `fwrite` + `fclose`), and a truncated `tmc.sav`
  loads as blank EEPROM (`0xFF`) instead of plausible-but-partial garbage.
- **`.logic` location keys with out-of-range components are rejected.** The parser
  masked `area-room-chest` components with `& 0xff`, so a malformed key like
  `1FF-05-02` silently aliased an unrelated location's chest. Such keys now parse
  as invalid with a warning; the chest keeps its vanilla reward.
- **ROM directory scan no longer acts on silently truncated paths.** The
  `rom_data/` page scan and exe-relative ROM probing joined 4 KB directory paths
  into 256-byte buffers; truncation is now detected and the entry skipped with a
  warning instead of stat'ing the wrong file.

### Defensive

- All `fread`/`fwrite` calls across `port_save.c`, `port_quicksave.c`, and
  `port_rom.c` now verify byte counts: short quicksave reads discard the slot
  cleanly, short `rom_gaps.bin`/page reads abort with a warning instead of
  leaving partial state, and a short main-ROM read takes the existing fatal-error
  path instead of booting on garbage.
- **Randomizer sidecar slots are sanitized at load.** A corrupted `*.randomizer`
  file can no longer drive out-of-bounds reads via an oversized slot `count` or
  item-pool index; bad slots are cleared with a warning. The sidecar version
  constant now documents that location-key semantics (TileEntity iteration order)
  require a version bump if they change.
- **`.logic` parse truncations warn instead of failing silently:** setting
  defines longer than the 48-byte field (which would break override lookups) and
  dropdowns with more than 10 options now emit parse-time warnings.
- F8 Randomizer tab only trusts the ROM region string when all 4 bytes were
  actually read.

### Build & CI

- **CI now runs the binaries it ships.** Linux jobs gate on a headless no-ROM
  smoke run (`SDL_VIDEODRIVER=dummy`, asserts SDL/PPU/imgui init reaches the
  prelaunch ROM wait) and Linux x86_64 builds and runs `rando_logic_test`.
  Previously nothing in CI ever executed a built binary.
- **CI caching enabled** for xmake and its package artifacts with per-OS/arch
  cache keys (keeps llvm-mingw, choco MinGW, and Linux/macOS package binaries
  from cross-contaminating); the Windows ARM64 llvm-mingw toolchain is pinned to
  release `20260602` instead of floating on `latest`.
- CRLF suppression (`core.autocrlf=false`) now applies on all CI runners, not
  just Windows, protecting the `git apply` ViruaPPU patch flow.
- ViruaPPU patches that are skipped because their marker is already present now
  log one line each, making patch drift diagnosable; removed duplicate
  `nlohmann_json` package requires in `xmake.lua`.
- `build.py` aborts with an actionable error when the sounds-embed generator
  fails and no previous `generated_sounds_embed.cpp` exists (previously it
  warned and deferred to an xmake hook that swallows its own failures).
- **64-bit-only policy made explicit.** The PC port now enforces
  `sizeof(void*) == 8` via a static assert in `port_types.h` (the N64 target is
  exempt), so accidental 32-bit configurations fail at compile time with a
  clear message. Removed dead 32-bit accommodation: the i686 crash-report arch
  label, the `nvdaControllerClient32.dll` probe (a 32-bit DLL can never load
  into a 64-bit process), and 32-bit arch tokens in `xmake.lua` gates.

### Docs & hygiene

- `docs/widescreen-phase2-design.md` opens with a single dated status block
  (Phase 2 fixed and shipping in 0.5.0 behind `widescreen_enabled`), replacing
  the contradictory WIP/REVERTED headers.
- `*.bundle` ignored; stray root git bundle removed; `gdb_softlock.gdb` moved to
  `scripts/`.

## 0.5.0 — 2026-06-08

0.5.0 cuts over the release build to the wide viewport (`build.py` now
configures `--widescreen_width=384`) and rolls up the post-0.4.0 crash,
renderer, CI, and portability fixes.

### Release / renderer

- **Release artifacts now compile as widescreen builds.** The `build.py`
  release path, including CI's `--slim` tag builds, passes
  `--widescreen_width=384` so `MODE1_GBA_WIDTH` is baked into `tmc_pc`.
  Direct `xmake` developer builds still default to native 240 unless
  explicitly configured.
- **ViruaPPU N64 / mode1 patch landed via the proper patch pipeline.**
  The N64/bare-metal TLS/endian/RDRAM shrink work plus the mode1 text-BG
  inner-loop cleanup now lives in `port/patches/viruappu-n64-mode1-perf.patch`
  and is registered in `xmake.lua`, not as dirty live submodule edits.
- **Render-thread cap for scanline OpenMP.** Runtime caps mode1 scanline
  workers to avoid oversubscription and includes the `TMC_PERFCAP=1`
  capture harness used for renderer profiling.

### Randomizer

- **Native in-process randomizer.** Added a clean-room, fixed-array randomizer
  with deterministic SplitMix64 seeds and a file-select SDL setup overlay for
  new saves (seed entry, glitchless/kinstones/dojos toggles, item difficulty).
  An active seed installs a **pool-preserving item bijection** that the
  engine's central give-item hook (`Rando_OverrideItem` in
  `GiveItemWithCutscene`) applies to every chest/NPC/drop, so real in-game
  rewards actually change. Difficulty scales the shuffle: `Normal` shuffles
  collectibles only (always beatable — progression untouched), `Hard` adds
  non-gating majors, `Chaos` also shuffles dungeon-gating progression (may be
  unbeatable without logic). For true per-location logic, a clean-room
  MinishMaker-style `.logic` engine (written from the public format spec, not
  the GPL C# source) parses the documented grammar and runs an **assumed-fill**
  placer with typed item/location pools + fallbacks, `~Items.X` placement
  guards, weighted count logic, accessibility modes, and graceful handling of
  symbols with no native id; it drives `Rando_OverrideLocationKey` at chests
  via address keys when a `.logic` file is supplied (`TMC_RANDO_LOGIC`).
  Seeds persist to a
  profile-local `*.randomizer` sidecar (vanilla EEPROM untouched) and reload on
  slot activation. The setup overlay is gated to the SDL_Renderer/software
  backend (where its SDL primitives are actually presented); on the default
  `auto`/GPU backend the new-file flow stays vanilla and the **F8 → Randomizer**
  menu (which renders on GPU) is the activation path — this avoids a freeze
  where the overlay opened and masked input behind an undrawn menu. Verified by
  `rando_logic_test` and the `TMC_REPRO_RANDO=1` headless end-to-end harness.
  The `.logic` engine maps item symbols to the authoritative engine `Item`
  enum (extracted to `include/item_ids.h` and shared by the C engine and the
  C++ randomizer, replacing drift-prone hardcoded id copies), and writes
  per-location rewards at small chests, big chests, and freestanding ground
  items; chests use MinishMaker's `area-room-chestIndex` identity (resolved via
  `Rando_RoomChestIndex`) so real `.logic` placements land on the correct
  in-game chest, while all other reward sources use the global item bijection.
  The engine is validated against the real MinishMaker `default.logic` (via
  `TMC_RANDO_LOGIC`): it parses the full file (882 locations / 176 items),
  generates a deterministic seed end-to-end with **all 365 real locations
  verified reachable** (executing `!ensurereachability`), and the headless
  harness confirms 144/161 keyed chests resolve to real engine chests in-game
  (e.g. Smith House chest: 20 rupees -> 50 rupees) and survive a sidecar
  save/reload. The file-select overlay enumerates the logic's declared
  `!flag`/`!dropdown`/`!numberbox` settings and applies changes as define
  overrides + reparse, so the menu drives real-logic generation. `!import` is
  approximated (logic-only item
  symbols assumed owned, standing in for `LogicImport.cs` — clean-room);
  `!prizeplacement`, precise-address NPC/floor reward hooks, and byte-for-byte
  placement parity remain.
- **Headless update-check skip.** `Port_CheckForUpdates` now returns
  immediately when `TMC_AUTOPLAY` or `TMC_NO_UPDATE_CHECK` is set, so
  automated/CI/repro runs don't block on the GitHub release network fetch.

### Fixed

- **#152 Romio house cat woman crash.** In area `0x22`, room `0x6`, the
  left woman is gated by `MIZUKAKI_START && !ITEM_FLIPPERS`. Her townsperson
  dialog entry is the only raw `DIALOG_CALL_FUNC` in `gUnk_0810B7C0` and
  pointed at GBA Thumb address `0x0806200D`; the PC unpacker stored that raw
  address as a native function pointer and jumped into GBA address space.
  The dialog unpacker now resolves CALL_FUNC slots via `Port_LookupScriptFunc`,
  and the repro harness covers both the townsperson path and the generic
  script-dialog unpacker.
- **Credits soft-reset path.** The end-of-credits script target is now
  registered, and staffroll completion returns through the PC soft-reset
  trampoline instead of leaving the game stuck/crashing.
- **Boss/enemy crash sweep.** Added PC-port guards/fixes for recent Vaati,
  Gyorg, Gleerok, Four Sword clone/button, PushableGrave, and takeover
  field-alias / NULL-deref failures.

### Build / CI

- **ARM64 build coverage.** Linux ARM64 and Windows ARM64 targets are now
  part of the default matrix; Windows ARM64 uses llvm-mingw and the corrected
  crash-handler PC register.


## 0.3.2-experimental — 2026-05-27

Re-tagged from the original 2026-05-26 cut to include the post-tag
work: the Project Picori UI rebrand (logo, theme, interactive
prelaunch with hash-validated ROM picker), four Windows-only fixes
that surfaced under Wine testing (#44 grey-blocks + cursor-bouncing,
plus universal collision-disable), and a cross-platform-parity audit
that closes several straggler `#ifdef __linux__`-only paths.

### Fixed (issue tracker)

- **#142 Talking to Tingle crashes the game (SIGSEGV / 0xc0000005).** `GetFuserId` is a multi-return function packing the fuser id (low 32 bits) and fuser text id (high 32 bits). Upstream zeldaret/tmc declares it `u32`, so the four simple callers `gSave.kinstones.fuserProgress[GetFuserId(this)]` (`tingleSiblings.c:155`, `din.c`, `farore.c`, `nayru.c`) index the 128-byte array with just the fuser id. A port commit (`7ccca6769`) widened the declaration to `u64`; on 64-bit PC the un-truncated return is then used as the *full* array index, so the non-zero text id in the high half pushes the subscript ~2.7 TB out of range → wild deref. On GBA the index is 32-bit regardless, so it never surfaced there. Pinned from a Linux backtrace (crash at `tingleSiblings.c:155`, fault addr `0x58bd…` far from the binary base) confirmed by disassembly. Fix restores the `u32` declaration to match upstream — the simple sites now index with the fuser id only, and `GetFuserIdAndFuserTextId` (which needs both halves) is unaffected because it reinterpret-casts the function pointer. Verified at the instruction level: the index is now `fuserId` (≤255), never the high half. (`include/asm.h`)
- **#87 Great Fairies have a light/sphere stuck on their face when talking.** As a Great Fairy appears she spawns a descending "blessing light" (FORM9) whose `child` pointer is never set by the spawn site (`GreatFairy_FinalUpdate`). The #33 Mt-Crenel-fountain crash fix (`06ad49dbf`) worked around the resulting NULL-deref in `sub_080871F8` by aliasing `target->child = super` — but that made the light **home onto the fairy** (32px above = her face) and park there permanently, since the action that releases it waits on `animFlags & 4`, a flag never set for this form. On GBA the unset `child` is open-bus garbage, so the light drifts off-screen and is cleaned up — no visible light. Fix: drop the `child = super` alias and instead **delete the light when it has no valid target**, matching the GBA result (no light on her face) while keeping the #33 crash fixed. (`src/object/greatFairy.c`)
- **Semi-transparent sprites (OBJ attr0 mode 1) now alpha-blend instead of rendering opaque.** VirtuaPPU's mode1 composite never read the OBJ "mode" field (attr0 bits 10-11), so semi-transparent OBJs — which on GBA hardware are unconditional alpha-blend 1st-targets regardless of BLDCNT's effect/target bits — rendered fully opaque. Surfaced by #87: the Great Fairy's see-through body/wings (and steam, the ghost brothers, Minish portal stones, Gleerok particles, etc.) were solid. Fix extracts the OBJ mode in `render_obj_line`, flags the winning pixel, and in `composite_line` forces the alpha blend with the layer below when the top sprite is mode-1 (the backdrop counts via the BD 2nd-target bit). Found via a GBA-accuracy audit of the PPU. Port-side ViruaPPU patch `port/patches/viruappu-obj-semitrans.patch`. (Companion OBJ-window fix below completes that audit item.)
- **OBJ-window sprites (OBJ attr0 mode 2) now carve the window mask instead of drawing as opaque blobs.** The other half of the OBJ-mode gap from the same audit. A mode-2 sprite is *invisible* on GBA — it draws no colour; its opaque pixels only define the OBJ window, inside which `WINOUT`'s high byte (the WINOBJ control) selects which layers show. VirtuaPPU drew mode-2 OBJs as ordinary sprites and never folded OBJ-window into the compositor's window logic, so TMC's dark-room light circles (`src/object/litArea.c`) rendered as solid circle-sprite blobs **and** the surrounding dark mask was lost (the room stayed fully lit). Affects Stockwell's rafters shop, the Temple of Droplets lantern rooms, Palace of Winds entrance, and the Royal Valley graves. Fix: `render_obj_line` marks mode-2 pixels into a per-scanline OBJ-window mask and skips drawing them; `composite_line` adds `DISPCNT_OBJWIN_ON` to `any_window` and resolves the window control with GBA-accurate precedence (WIN0 > WIN1 > OBJ-window > outside), using `WINOUT`'s high byte inside the mask. Strictly gated on `DISPCNT_OBJWIN_ON`, so every non-litArea room is byte-identical to before. Two F8 → Warp test entries added (Stockwell shop, ToD Madderpillars) that spawn Link on a light circle. Port-side ViruaPPU patch `port/patches/viruappu-objwin.patch`.
- **Vertical window range now wraps when `top > bottom` (WIN0V/WIN1V).** Another GBA-accuracy gap from the same PPU audit. VirtuaPPU's compositor handled horizontal window wraparound (`WINxH` left > right → active for `x >= left || x < right`) but the vertical path *disabled* the window entirely when `WINxV` top > bottom, instead of wrapping. On GBA an inverted vertical range is active for `line >= top || line < bottom` (same semantics as horizontal). TMC packs an inverted `WINxV` in the digging-cave entrance/exit iris spotlight (`src/scroll.c` `Scroll5Sub2`/`Scroll5Sub5`) on small-iris frames when the camera target nears a screen edge — so the spotlight blanked for those frames. Fix mirrors the horizontal wrap with `winX_v_wrap` ternaries. The compositor is shared by mode1 and mode2 (the title path calls it too), so one change covers both. Port-side ViruaPPU patch `port/patches/viruappu-winv-wrap.patch`.
- **BG mosaic (pixelate room transitions) is on by default again.** The `FADE_MOSAIC` fade type — used by the `TRANSITION_CUT` / `TRANSITION_CUT_FAST` room transitions (the iconic pixelate-dissolve when entering houses, caves, and crossing area edges) — sets `BGCNT_MOSAIC` on BG0–3 and ramps `REG_MOSAIC`. VirtuaPPU's mosaic implementation (`viruappu-mosaic.patch`) had been left gated behind an opt-in `TMC_ENABLE_MOSAIC` env var (a holdover from when it was being stabilized), so on PC those transitions cut without pixelating. The render is correct (each pixel snaps to its mosaic-block top-left, GBA-accurate), so the gate is removed — mosaic is now honored whenever `BGCNT` bit 6 is set, matching hardware. (OBJ mosaic is intentionally still unimplemented: TMC never sets the OBJ mosaic bit, so it has no observable effect, and adding it to the hot sprite path would only risk regressions.) Port-side ViruaPPU patch `port/patches/viruappu-mosaic.patch`.
- **#139 ToGrimblade (Grimblade dojo entrance): lit flame braziers vanish after a dark-room round-trip.** The flaming braziers flanking the door are BG1 (foreground) tiles drawn over the BG2 bowl base; both BGs sit at the *same* priority (2), so on GBA hardware the lower-numbered BG (BG1 = flame) wins the tie and draws on top. Going down into the dark Grimblade dojo and back up (an intra-area `RELOAD_ALL` transition) leaves `BG3CNT` at the dojo's priority 0 (`0x1e0c`). BG3 is disabled so it never renders — but VirtuaPPU's `mode1.c` composite built its per-pixel draw order with an **unstable selection sort** over all four BGs: the disabled BG3 at priority 0 swapped forward and displaced BG1 *past* BG2 in the order, flipping the equal-priority tie-break so the BG2 bowl drew over the BG1 flame and the flame "vanished" (only the bowl base showed). A full room init reset `BG3CNT` and masked the bug, which is why exiting the area and returning restored the flames. Fix replaces the sort with a **stable insertion sort** so equal-priority BGs always keep their BG-index order (GBA-accurate), regardless of any other BG's priority — a disabled/low-priority BG can no longer reorder two same-priority layers. Port-side ViruaPPU patch `port/patches/viruappu-bg-priority-sort.patch`.
- **#54 follow-up: Boomerang — dizzy-stars sprite never leaves enemies.** `b9ce2fb5d` restored the alive-dispatch branch so `GenericConfused` ticks on stunned enemies, but the FX detach inside `GenericConfused` still never fired — the rationale conflated `Entity::child` (PC offset 0x68) with `Enemy::child` (PC offset 0x90); they're two different fields after pointer widening. `EnemyCreateFX` writes the FX through the `Enemy*` cast, so the FX pointer lives in `Enemy::child`; the detach gate was reading `entity->child` (Entity::child), the kind/id/type identity check never matched, and `FX_STARS` — which self-deletes only when its parent goes NULL — stayed glued to the enemy after stun ended. (commit `d86cde0ba`)
- **#55 "Palace of Winds Tower" (actually Wind Tribe Tower 2F): softlock removing ghost from Gregal.** `script_WindTribespeople6` is declared as a 2-byte BSS stub in `port/port_linked_stubs.c` (one of many such stubs to satisfy the linker — the real script bytes live in ROM). The call site at `src/npc/windTribespeople.c:79` used `&script_WindTribespeople6` which resolved to zero-filled memory, so when Tribesperson5 script-swapped to it after the gust-jar capture, her `ExecuteScript` hit the defensive `cmd == 0x0000` short-circuit every frame and never advanced. Gregal had already set sync flag 1 and parked on `WaitForSyncFlagAndClear 2`; the partner's `SetSyncFlag 2` never executed → softlock. Diagnosed live via GDB `inspect_cutscene` + the `[sync]` diagnostic stream. Fix routes the call through `PORT_SCRIPT(script_WindTribespeople6)` so the real bytes resolve via `Port_ResolveRomData`; ROM address `0x08014A80` confirmed against the upstream zeldaret/tmc USA map. (commit `1b6f1d4b8`)
- **#102 Veil Falls: Biggoron + sibling BG-manager EWRAM-bridge bugs.** The GBA decomp uses pointer arithmetic that crosses *between* two distinct EWRAM symbols (`gMapDataTopSpecial + 0x4000` resolves to `gUnk_02006F00` on GBA's flat 256 KB EWRAM layout). On PC the two arrays are separate host allocations, so the bridged offset lands in unrelated memory: at best silently corrupts the wrong buffer, at worst SIGSEGVs inside `DmaCopy16`. Confirmed cases: `bigGoron.c::sub_0806D110` + `sub_0806D164`, `horizontalMinishPathBackgroundManager.c::sub_08058004`, `minishRaftersBackgroundManager.c::sub_080582D0` + `sub_080582A0`. PC fix routes bridge arithmetic through the actual target symbol on `PC_PORT` and clamps / early-returns when scroll-derived offsets would overflow the source buffer. (commit `7893f1cb4`)
- **#103 Cloud Tops: BG texture broken after kinstone fusion.** `vaatiAppearingManager` armed `SetVBlankDMA` on entry and never called `DisableVBlankDMA` on exit. On GBA this was benign because the next subtask re-armed `SetVBlankDMA` and overwrote the old src/dest; on PC the leftover DMA kept firing into BG2's charBase register every HBlank, breaking the next room's BG rendering. Fix pairs every `SetVBlankDMA` with a `DisableVBlankDMA` on the manager's exit path, plus a `Subtask_FadeOut` catch-all that disables any leftover DMA and calls `AnimatedBackgroundManager_RestoreBgGfx` on every active manager so BG3 re-arms cleanly across menu→game transitions. (commits `4e28ea2c4`, `543a8f47a`, `ebee7e9a6` for the BG-restore plumbing)
- **#136 Palace of Winds: Gyorg boss NULL deref on entry.** The boss-room dispatcher (`gyorgBossObject.c::sub_080A1DCC`) unconditionally calls tail helpers *after* the action handler every frame. On frame 0 the action handler is `SetupStart`, which populates `heap->female / male1 / male2` but NOT `heap->mouth / heap->tail` — those get filled by `GyorgFemale_Setup` on the female's first tick. The helper then dereferences `heap->mouth` and NULL-crashes. Fix NULL-guards the heap fields plus the `tail->child->child->child` walk. (commit `b91a4faa6`)
- **#44 Windows-only: pause-menu world-map grey blocks + cursor bouncing.** Two independent Windows-side root causes:
  1. `port_resolve_addr`'s `#ifdef _WIN32` branch used `VirtualQuery` to short-circuit GBA→host remapping for any address in the GBA range that happened to be a committed VM page on the host — which is the case for every real GBA EWRAM address since the low address space is densely mapped by Wine / Windows system DLLs. The map screen's tilemap pointer (`gMapDataBottomSpecial` at 0x02006B00) came back as the raw GBA value instead of `gEwram[0x6B00]`, and BG3 read garbage at the top. (commit `d9b2abef7`)
  2. MinGW defaults to `-mms-bitfields` (MSVC bitfield ABI) which makes any `__attribute__((packed))` struct with bitfields larger than the GCC-default packing — verified empirically: the pause-menu's `gUnk_08128DE8_struct_2` (5+5+6 bits) is 2 bytes on Linux GCC but 3 bytes on MinGW default. That shifted the outer struct's `unk6` / `unk7` (the screen-space cursor coords) from offsets 6/7 to 8/9, so the player-position marker read its x/y from the next entry's bitfield bytes. Adding `-mno-ms-bitfields` globally for MinGW / Windows targets restores parity. (commit `a8f480012`)
- **#44 (Windows-only, separate from #44 above): no enemy damages Link, Link can't damage enemies.** `src/collision.c::IsColliding` had a host-pointer range guard (`pa >= 0x100000000000`) added to catch half-pointer-write artifacts on Linux x86_64, but the lower bound (17.5 TB) excludes every valid Windows / Wine user-mode pointer — Wine puts entities at ~0x140_XXXXX (≈5 GB). The guard returned `FALSE` for every hitbox pair, silently disabling all combat collision. Split the check on `_WIN32`: Linux keeps the original strict range (still catches the documented half-pointer-write hazard); Windows uses a looser `>= 0x10000` lower bound (rules out NULL + the first 64 KB, which is the only platform-portable "obviously bogus" signal). (commit `b1685b5c3`)

### Quality of life

- **Project Picori UI rebrand.** Project codenamed "Project Picori" — README title updated, F8 / config menu re-themed with a deep-green Minish palette + 10 px card rounding, and a hand-drawn Ezlo-and-Link logo (`docs/picori-logo.png`) embedded in the binary via xmake's `utils.bin2c` rule. (commits `b9fa90b0e`, `d21d547ed`)
- **Interactive prelaunch screen replaces the timed splash.** `Port_PPU_Init` now runs before any ROM is loaded; the prelaunch ImGui card is the first interactive frame the user sees. Two states: "No ROM found" (big Select-ROM button, hash-explainer copy) or "ROM detected" (version + filename + Change-ROM + Play). Play kicks `Port_LoadRom` → asset extraction → audio init → `AgbMain`. Works on both SDL_Renderer and SDL_GPU backends (the latter via a new `Port_GPU_PresentPrelaunchFrame` that pairs PrepareDrawData + ImGui render in one pass). (commits `5a069c39d`, `03a2518ef`, plus `686a67cbe` / `c22f197e2` for the splash branding)
- **ROM picker validates by SHA-1 hash, not filename.** Drops the gamecode-only check (BZME / BZMP / BZMJ); any `.gba` whose SHA-1 matches one of the five known TMC dumps (USA / EU / JP retail + USA / JP demo) is accepted. Users can name their dump anything. Rejection dialog now prints the picked file's actual hash next to the expected list. (commit `f0497dc4b`)
- **xBRZ on the SDL_GPU backend.** The CPU-side xBRZ 4× upscaler used to only fire on the SDL_Renderer path; the SDL_GPU branch bypassed it entirely. GPU branch now runs `Port_Upscale_xBRZ_4x` and feeds the 960×640 buffer straight to `Port_GPU_PresentFrame` (mutually exclusive with internal-scale, same as the SDL_Renderer branch). F8 → "Filter" picker is no longer gated behind "GPU inactive". CRT filter stays SW-only — separate Stage 3 work. (commit `205616720`)
- **Internal render scale cap raised 4× → 10×.** The 4× cap was based on a stale comment about a fixed framebuffer size; the scaled buffer is actually `malloc`'d lazily and the affine-OAM overlay scales naturally with the `scale` parameter. 10× yields a 2400×1600 internal render (≈15 MB scratch + matching SDL_Texture). Verified live on both SDL_Renderer and SDL_GPU / Vulkan paths. (commit `7a12173c5`)

### Defensive / hardening (no specific issue)

- **Crash reporter no longer crashes itself on a corrupt frame chain.** The POSIX SIGSEGV handler walks the `rbp` frame-pointer chain via `SafeReadPointer`, which only NULL-checked its argument before `memcpy`-ing 8 bytes from it. When a crash left `rbp` corrupted to a tiny garbage value (e.g. `0x6`, so reading the return slot at `0xe`), that read faulted *inside the handler* → secondary `SIGSEGV@0xe` in `WriteBacktracePosix` → truncated bugreport with only "Crash IP / Fault addr" and no frames. Seen repeatedly while reproducing the #112 cutscene. Fix: `SafeReadPointer` now (a) pre-filters NULL / sub-64 KB / misaligned / non-canonical addresses, and (b) probes readability with a pre-made non-blocking pipe — `write()` returns `EFAULT` for unmapped memory instead of raising SIGSEGV (and is async-signal-safe), reading the bytes back only if accepted. Full frame chains are now captured even when the crash corrupted the stack. (`port/port_bugreport.cpp`)
- **`MinishSizedEntranceManager` NULL-deref when its room property is absent.** `MinishSizedEntranceManager_Main` reads `spawnData = GetCurrentRoomProperty(super->type)` and immediately loops on `spawnData->x`. If this manager ticks while an auxiliary cutscene overlay is active (the cutscene's room has no property for `super->type`), `GetCurrentRoomProperty` returns NULL — on GBA the read picks up adjacent ROM garbage and the loop harmlessly churns; on PC it's a NULL deref → SIGSEGV. Surfaced while reproducing the #112 cutscene from a minish-entrance room. `#ifdef PC_PORT` early-return when the property is NULL; GBA path byte-identical. (`src/manager/minishSizedEntranceManager.c`)
- **#136 family — defensive `parent == NULL` guards across 12 boss-helper sites.** Class audit after the #136 fix found the same shape (helper derefs `this->parent->next == NULL` at function entry with no preceding NULL check) across Vaati / Mazaal / Gyorg families plus `rupeeLike.c` and `flame.c`. `#ifdef PC_PORT` early-returns mirror the existing `sub_080A1DCC` fix; GBA path is byte-identical. Patched: `vaatiWrathEye`, three `vaatiEyesMacro` functions, `vaatiProjectile`, `v3ElectricProjectile`, `mazaalHead`, two `mazaalMacro` functions, `mazaalObject`, `gyorgFemaleEye`, `gyorgFemaleMouth`, `gyorgTail`, `rupeeLike`, `flame`. (commit `cd7805da2`)
- **`IsColliding` host-pointer range guard for stale hitbox** (Linux path retained, Windows path loosened — see #44 above).
- **Auto-crash bug-report capture restored.** A Matheo-merge regression removed the `Port_BugReport_InstallCrashHandlers` call from the top of `main()`. F9 manual capture kept working but auto-on-crash bundles silently stopped generating. Re-added the install call. (commit `51dc6c9c1`)
- **Delayed-entity bitmap reset on PC.** `gArea.filler6` aliases `gUnk_020342F8` (the delayed-entity-load bitmap) on GBA but they're separate symbols on PC; `sub_0806F364` now clears both. (commit `b1c58e587`)

### Cross-platform parity audit

- **`ExecutableDirectory()` was Linux-only in `port_mods.cpp` + `port_randomizer.cpp`** — both fell through to `current_path()` on Windows / macOS, returning the cwd instead of the exe's directory. Mod loader and randomizer CLI lookup probed the wrong root and silently failed. Added Windows `GetModuleFileNameW` + macOS `_NSGetExecutablePath` branches matching the existing pattern in `port_asset_bootstrap.cpp::GetExecutableDirectory`. (commit `79a11cf68`)
- **#135 — TownMinish bookshelf NPC didn't offer Speak prompt.** Decomp typo in the `gUnk_additional_a_TownMinishHoles_LibraryBookshelf` EntityData table: `15` (=0x0F) instead of the default `0x4F` npc_raw pool. Replaced with the correct value so `sub_0804AF0C`'s case 0x40 (which calls `StartCutscene` and sets `ENT_SCRIPTED`) fires. (commit `ebee7e9a6`)

### Build / CI / docs

- **CI matrix now triggers on `sync-matheo-release`** (the working branch), in addition to `master` / `CI-Test`. Linux + Windows + macOS-arm64 all build on every push so any new Linux-only code gets flagged at PR time. (commit `4c823eae5`)
- **Windows F9 path-resolve fix** — replaced POSIX `realpath` with `_fullpath` under `#ifdef _WIN32`. MinGW lacks `realpath` and a whole-file `extern "C"` declaration of it failed at link time. (commit `5cff9ad6a`)
- **`build.py` always passes `--gpu_renderer=y`** so the shipping binary has the SDL_GPU backend + F8 → Shader Preset picker enabled. (commit `d3ff4c374`)
- **Windows CI: force LF on checkout + `git apply --ignore-whitespace`** so `port/patches/viruappu-*.patch` apply cleanly even when CRLF crept into the submodule. (commit `d82630f44`)
- **`docs/widescreen-phase2-design.md` kept after the Phase 2 revert** as an archive of what was tried, what worked, what broke, and why — so a future attempt doesn't re-walk the same dead-ends. (commit `7cd6afeab`)

## 0.3.1-experimental — 2026-05-24

Bug-fix release covering five issue-tracker reports + a packet of
quality-of-life features (renderer backend picker, auto-save,
quit-save confirm, F8 audio tab, profile management).

### Fixed (issue tracker)

- **#131 Deepwood Shrine: missing barrel textures after pause-menu close.** Closing the pause menu while standing inside a barrel left `RollingBarrelManager` un-reinitialised, so the barrel sprites' GFX slots were never re-claimed. `Subtask_FadeOut` → `RestoreGameTask` now re-runs `RollingBarrelManager_OnEnterRoom()` when the barrel-update path is the resumed task. Confirmed both Linux and Wine/Windows builds. (commit `2b07478d`)
- **#128 Hyrule Town: minish doors disappear after first walk-through; house signs disappear after scrolling off-screen.** Both bugs in the same family — a manager subclass declared an `unk_20` field meant to alias `EntityManager::field_0x20` (the per-instance "active door slots" bitmask).  On GBA the 4-byte Entity::zVelocity at offset 0x20 was reused as the manager's field; on PC the pointer-widened Entity shifts that slot, and the door's `parent->zVelocity & mask` read goes to actual zVelocity (always 0) so doors always delete themselves. Replaced the alias with a direct cast to the manager subclass, reading the real `field_0x20` / `bitfield` slot. Same pattern applied to `houseSign.c`. (commits `41c77124`, `f47e8470`)
- **#129 Hyrule Castle Garden: post-takeover knights stand still.** The non-scripted patrol guards' movement script lives in `gUnk_0810F6BC[type]`, a 6-entry packed-pointer table.  `Port_ReadPackedRomPtr` was rejecting it because the table sits in PC `.rodata` (`data_const_stubs.c` defines it as `const u8[920]` copied verbatim from ROM 0x10F6BC) rather than inside the `gRomData` mmap. The base-bounds gate said "outside gRomData, reject" and `sub_0806EE04` got `child=NULL`, so the per-tick `RunScript` found no script to execute and the guards never moved. Loosened the gate: still bounds-validate when base is in gRomData, but accept PC stubs and trust the caller. Movement script loads, guards patrol. (commit `2f2fdbc2`)
- **#101 Temple of Droplets: Scissors Beetle crash when defeated with detached mandibles (follow-up).** `8d5e0066f` already guarded `sub_08038C2C` but the underlying mandibles projectile has three more parent/child deref sites that crash in the same scenario.  Added NULL guards to `MandiblesProjectile` dispatch (line 48 `entity->confusedTime` after both child + parent fall through NULL), `sub_080AA270` (parent-anim-state read), and `OnCollision` default case (parent iframes/knockback writes).  Guarded with `#ifdef PC_PORT` so the GBA path is byte-identical. (commit `438e4bf1`)
- **#110 \[FATAL\] palette group N not found — startup abort on Arch Linux.** Title screen tries to load palette group 2 (Japanese title intro) when `gSaveHeader->language != 0`. When the user's setup didn't extract the non-English palette files — or has a tmc.sav from a non-English session — the abort kills the engine before file-select. Engine-level fallback: a missing palette group N now tries the canonical English-equivalent (2 → 1, 4 → 3) before aborting, with a one-shot warning.  Title colours may be slightly off in the rare case the user genuinely wanted a non-English palette, but the game boots. The `7a7fd0c1` asset-environment dump is still emitted if neither group resolves. (commit `b05039e3`)
- **#78 Wind Ruins wizards (follow-up).** After `8d5e0066f` fixed the divide-by-zero crash, JesterWizard / linkdedo reported the wizards "appear out of bounds" — the teleport do-while picked random positions in `[homeX, homeX + rangeX*8)` and accepted any walkable tile, including ones outside the room (OOB tiles return non-collision from asset-load fallback).  Now: reject candidates outside `gRoomControls.{origin_x..origin_x+width, origin_y..origin_y+height}` and cap iterations at 64 so the loop can't infinite-spin on unlucky rooms. (commit `924967d3`)

### Quality of life

- **F8 → renderer backend picker** (Auto / Software / GPU).  Software stays the default; users with broken SDL_GPU drivers (Wayland surface conflicts in particular) can pin Software at startup without env-var fiddling. (commit `9eca4567`)
- **F8 → Audio tab with per-category SFX mutes** — mute Link's footsteps, sword swings, item pickups, etc. independently while keeping BGM. (commit `510e4721`)
- **F8 → Profiles tab: rename + delete soft slots.** (commit `467faf91`)
- **Auto-save on area / room change** — eliminates "I forgot to save" loss on crash. Quick-save-style soft slot is rotated every transition. (commit `7247a18e`)
- **Quit-save confirm modal on window close.** Asks before discarding unsaved progress when you Alt-F4 / close the window. (commit `6c7e9972`)
- **GPU backend parity with Software backend** — aspect ratio, bg fill, soft-slot overlay, internal render scale all now work on the SDL_GPU path. (commits `91283475`, `7d97af48`)

### Defensive / hardening (no specific issue)

- **#131-class crash hardening: defensive `UnlinkEntity` guards against half-pointer prev/next.** Rupee LikeLike crash showed `prev = 0x555511113333` (decompressed 32-bit-half written into 64-bit field).  `UnlinkEntity` now NULL-tests `ent->prev` / `ent->next` before dereferencing them and logs once if the pointer fails the user-space-address range check. Quiet on the GBA path, defensive guard on PC. (commit `265c2a5f`)
- **gSpritePtrs runtime-extend beyond 329-entry compile-time cap.** Sprite-pointer indices > 329 used to fall off the end of `port_asset_index.c`'s lookup table; runtime now walks the ROM table directly past that index until it hits the 0-sentinel.  Lays groundwork for #127-class "sprite present but invisible". (commit `fcb5b57c`)

### Notes on the Vulkan RT experiment

The `port/vk_rt_experiment/` standalone Vulkan ray tracing demo gained
slices 4–13 this release (sprite plane, multi-bounce GI, point lights,
a-trous denoiser, water reflections, material variety, animated water,
volumetric fog, bloom + dither, perf pass).  It's still a separate
binary, not linked into `tmc_pc` — it consumes `/dev/shm/tmc_framebuffer`
when `tmc_pc` is started with `TMC_PUBLISH_FRAMEBUFFER=1`. No change to
normal end-user behaviour.

## 0.3.0-experimental — 2026-05-23

Boss-room crash sweep across Temple of Droplets, Discord Rich Presence
default-on with native Windows named-pipe support, and the post-v0.2.2.0
bundle of audio / VSync / portal / asset-loader follow-ups.

### Fixed (issue tracker)

- **#64 Temple of Droplets: big-key-door / Big Octorok boss room crashes.** Two confirmed SIGSEGVs along the path from the Entrance room (3) through the boss door into the BigOcto room (14). Same `#91`/`#97` family as today's frozen-octorok fix: a child entity dereferences a sibling pointer that hasn't been assigned yet. (a) `OctorokBoss_Init` (`src/enemy/octorokBoss.c:472`) ends with a direct `OctorokBoss_Action1(this)` call. The head (WHOLE) creates LEG_*/MOUTH/TAIL_END/TAIL children, but their own Inits run later in the entity-update loop in list order — so a leg or tail child's Action1 reads `heap->mouthObject->base.health` before MOUTH's Init has assigned `mouthObject` (line 455). GBA NULL-deref returned BIOS bytes; PC SIGSEGVs. Guarded four sites in Action1 (lines ~598, 623 for LEG_*, 636 for TAIL, 657 for TAIL_END). (b) `OctorokBossObject_Action1` case 4 line 251 derefs `helper->tailObjects[super->timer]` — the type-4 (smoke-attack) variant is spawned by `OctorokBoss_ExecuteAttackSmoke` without ever assigning `helper`, so it stays NULL. The line is a `x = x` self-assign (no observable effect even on GBA), so the PC-side guard simply skips it. Behaviour-preserving: NULL window is one Init frame before the sprite has even drawn; all reads converge to real values on the next tick. (commit `a7eeda1a`)
- **#100 Temple of Droplets: Blue Chuchus don't spawn after lever.** Resolved indirectly. The chuchu spawner is `TempleOfDropletsManager` Type 1 in room 16 (BigBlueChuchu), whose `localFlag` (`0x46`) is one of the fields the `#75` `src/room.c:122` rewrite already restores for **all** `id == 0x15` managers, not just the sunbeam Type 2. The bug was also gated by the boss-room frozen-octorok crash blocking the route through room 8 (Element); once that crash is gone, the room loads and the chuchus spawn as designed. No code change beyond `69c84f84`. (Verified live by F8-warping to area 0x60 room 0x10 and watching `[ToD-mgr] type=1 action=3` advance to 4 on lever-push.)

### Fixed (other)

- **Temple of Droplets Element room: `FrozenOctorok_Action1` SIGSEGV on entry.** First crash uncovered while reproducing #100. Same Init→Action1 pattern as the OctorokBoss fix above: leg children (types 1-4) of the head (type 0 / WHOLE-equivalent) run their Init→Action1 (line 150) before the mouth (type 5) has assigned `heap->mouthObject`. GBA NULL-deref → BIOS garbage ≠ 1 → else branch; PC → SIGSEGV at the offset_of_health load. Single PC-port-only NULL guard at `src/object/frozenOctorok.c:193`. Also applied the same guard to three defence-in-depth sites in `src/enemy/octorokBoss.c` (`Hit` at line 122 — `tailObjects[0]` camera-target; `Hit_SubAction6` at lines 303/306 — `legObjects[0]` death-FX + `mouthObject` death-kill); these sites only reach the deref under a valid boss state, but the cost of guarding is one NULL check and avoids re-hitting the same pattern from a different attack path. (commit `69c84f84`)

### Tooling / new features

- **Discord Rich Presence: default-on + native Windows support.** Rich Presence now ships enabled by default on Linux, macOS, and Windows. Adds a `TryConnectWindows()` path in `port/port_discord_rpc.c` that opens `\\?\pipe\discord-ipc-N` via `CreateFileA` (`N = 0..9`), with the existing JSON-RPC frame protocol reused unchanged — `WriteFile` replaces `send(MSG_NOSIGNAL)` on the Windows branch. Connection handle storage moved from `int sock` to `intptr_t handle` so the same field holds either a Unix fd or a `HANDLE`. F8 toggle still controls per-session enable/disable; `TMC_DISCORD_APP_ID` env var or the `discord_app_id.txt`-at-build-time path still gate whether the connection is even attempted, so users without Discord (or without a registered app ID) see no behaviour change.

### Notes

These NULL-guard fixes (`#91`/`#97` family) are intentionally not bit-identical to GBA: on hardware, the BIOS bytes at the NULL+offset address are deterministic but not always equal to the value our guard substitutes, so the chosen branch can differ for **one Init frame** before the sibling's Init runs. The affected fields (`unk_74`/`unk_76`/`radius`/`DeleteThisEntity` gating) all reset to correct values on the next tick, and the child sprite isn't drawn yet during that one frame, so the divergence is unobservable in-game. This matches the established "guard + clamp/early-return" pattern used throughout the port for the "NULL deref differs from GBA" class of fixes.

### Carries from post-v0.2.2.0 master

Issues already fixed in master between v0.2.2.0 and this tag — closed by re-release rather than further code changes:
**#42, #45, #74, #75, #91, #94 (auto safe-spawn), #99, #101, #106, #107, #117, #118, #119**.

## 0.2.0-experimental — 2026-05-06

Two-day bug-fix and tooling pass on top of 0.1.6.x. Six tracker issues closed (cucco round 9, figurine minigame, Deepwood barrels, max-hearts, Cave of Flames boss round 3, Link's house warp); F8 internal-render-scale page + sub-pixel OAM affine added; bug-report capture upgraded to PNG with auto-on-crash trigger and raw-IP/maps emit before unsafe calls; F8 "All areas (raw, by index)" warp submenu so any room is one keystroke away.

### Fixed (issue tracker)

- **#46 Hyrule Town: cucco minigame round-9 SEGV.** Levels 8 and 9 only define 2-3 cuccos; the remaining heap slots are NULL. `sub_080A1270` evaluated `pEnt->next != NULL && pEnt != NULL` — wrong order, dereferences `pEnt->next` before the NULL guard fires. Harmless on GBA (NULL reads return BIOS data) but SEGVs on the PC port at the start of round 9. Swap the operand order so short-circuit evaluation actually gates the deref. (commit `868159bd`)
- **#51 Cave of Flames: boss round-transition hang + death-path SEGV.** Two-part Gleerok fix. (a) The round-3 transition stuck because `Gleerok_KnockBack` ran the round-bump path even after death; gate on `super->action != ACTION_DEATH` to keep the death sequence linear. (b) `sub_0802E7E4` deref'd `heap->entity1` unconditionally on death; the heap entry was NULL on PC where GBA hardware silently returned BIOS data. Added the same NULL guard pattern as the rest of the gleerok work. (commit `2c9f0a60`)
- **#52 Debug menu: max-hearts sets 20, not 10.** `Port_DebugAction_MaxHearts` set `maxHealth = 80` (10 hearts in eighths) where the player's actual cap is `0xA0` (20 hearts). The previous value matched the fileselect/UI's silent 10-cap special-case. Fixed to `0xA0`. (commit `1045174f`)
- **#57 Figurine viewer: SEGV the moment you owned a figurine + glitched sprite.** Three layered bugs. (a) `gFigurines` was a 512-byte zeroed stub in `port_linked_stubs.c`; the menu deref'd `fig->pal` / `fig->gfx` and crashed. New `port/port_figurines.c` defines a real `Figurine[137]`, populated from a compile-time table of 136 GBA ROM addresses + sizes resolved via `Port_ResolveRomData` after `gRomData` is mapped (same pattern as `gPalette_549` / `gSpritePtrs`). (commit `0114e8bf`) (b) The first table version stored ROM-relative offsets (`0x005B5EC0`) but `Port_ResolveRomData` requires `>= 0x08000000`; every entry resolved to NULL, `port_DmaTransfer` early-returned on `!src`, and the viewer rendered with stale palette/VRAM ("glitched"). OR-in `0x08000000` when calling. (c) `sub_080A4CBC` deref'd `(u16*)0x02021f72 + 0x80` — a hardcoded GBA EWRAM scan address — bypassing the `ShowTextBox` write path's `Port_ResolveEwramPtr` routing. Resolve through `Port_ResolveEwramPtr` inside the PC_PORT guard before the scan. (commit `1599a004`)
- **#61 Deepwood barrels invisible on Windows.** `Port_LoadGfxGroupFromAssets` and `Port_LoadPaletteGroupFromAssets` ran heap-allocated `std::vector::data()` source pointers through `port_resolve_addr`. On Windows MinGW, malloc occasionally hands out addresses inside `[0x02000000, 0x0A000000)` which the resolver remaps to `gEwram[]` — the gfx/palette never reaches its destination. Linux glibc never allocates in that window so the bug never fires there. Resolve the destination ourselves in both loaders and use plain `std::memcpy`; source pointer never touches the resolver. (commit `8f15a9e3`)
- **#65 Debug menu: Link's house warp lands a mile off.** Entry warped to area 3 room 0 (Western_Woods_South) instead of room 1 (SOUTH_HYRULE_FIELD). Updated the coords to match `gExitList_HouseInteriors2_LinksHouseEntrance` (`0x290, 0x19c, layer 1`). (commit `89a575a1`)

### Fixed (other)

- **Ocarina-of-Wind / windcrest fast-travel SIGSEGV.** `Subtask_FastTravel_Functions` was an unpopulated function-pointer stub. Filled in the table from the decompiled fast-travel state machine. (commit `bb962804`)
- **#50 asset extractor missing sounds.json.** `sounds.json` wasn't being embedded into the extractor binary (the v0.1.6 packaging bug). Added it via xmake's `bin2c` rule. (commit `8dfb6a72`)

### Tooling / new features

- **F8 → Display settings page + internal-render-scale.** New display submenu lets you switch render scale (1x..4x) at runtime. Includes a sub-pixel OAM affine overlay path so rotated sprites (Vaati's tornado, screen-shrink cinematic, every spinning enemy) sample at the upscaled grid instead of nearest-neighbouring the 240-pixel staircase. (commit `dcde8415`)
- **F9 bug-report capture: PNG output + auto-capture on crash.** Replaces the BMP screenshot with PNG. SIGSEGV/SIGABRT/SIGBUS handlers (POSIX) and `SetUnhandledExceptionFilter` (Windows) snapshot the same bug-report directory automatically with `reason=crash:<sig>@<addr> ip=<rip>` so post-mortem reports include the fault site even if the user couldn't press F9. The handler now emits raw IPs and `/proc/self/maps` *before* any unsafe call so the report survives even when the crash came from the handler's own stack. (commits `cb4cc0ca`, `8fa93717`)
- **F8 → "All areas (raw, by index)" warp submenu.** Iterates every area slot with at least one mapped room, opens a per-area room list. Pages scroll if the list overflows. Spawn coord is geometric centre — re-warp if you land in a wall. (commit `628b8c49`)
- **F8 → ITEMS → "999 mysterious shells".** Companion to the existing 999-rupees / max-hearts / all-kinstones actions; needed for figurine-minigame repros. (commit `1599a004`)
- **F8 → WARP → repro shortcuts.** "Carlov figurine shop (#57 repro)", "MinishRafters Bakery (#58 repro)" — direct warps to the bug-report coordinates so repros don't need a save run. (commits `1599a004`, `89a575a1`)

### Build / CI

- **CI: install Linux audio dev libs.** Was failing to find ALSA/PulseAudio headers on the new runner. (commit `69c448d5`)
- **CI: upgrade meson via pip so Wayland 1.25.0 builds.** Distro meson was too old. (commit `d6252a06`)
- **Updater + Discord links repoint at this fork.** (commits `fdfb5aac`, `2be226ff`)

### Known issues (still open)

- **#9 Steam Deck packages**, **#11 boss asset rendering**, **#15 libfmt.so.12 on Fedora 43**, **#17 GLIBC_2.43 on Nobara/Fedora 43**, **#21 Link's house glitched doors**, **#26 fast-forward not working on Windows**, **#28 random door above staircase**, **#32 shop top textures**, **#37 Lon Lon Ranch shrink/door**, **#40 Hyrule Town door texture**, **#41 EU text extract**, **#44 map grey blocks**, **#47 Mt Crenel background texture**, **#54 boomerang dizzy stars sprite**, **#55 Palace of Winds idle softlock**, **#56 Lake Hylia entry crash**, **#58 bakery attic missing texture** (Windows-only, likely covered by #61 fix — needs reporter retest), **#63 West Hyrule Cave Moldorm sprite chunks**, **#64 Temple of Droplets big-key door crash** — open at the time of this build.

## 0.1.6-experimental — 2026-05-04

Bug-fix + tooling pass on top of 0.1.5. Five open tracker issues closed (cucco crash, chuchu freeze, two Melari's Mines bugs, two Cave-of-Flames bugs); F8 in-game debug menu + F5/F6 quicksave/quickload added for bug-repro work. Picked up matheo/master twice (fileselect / spear-moblin / cucco refactors + AVX2 build option).

### Fixed (issue tracker)

- **#46 Hyrule Town: cucco minigame crash on reward.** `CuccoMinigame_WinRupees` mimicked the GBA `add r0,r3,r0` quirk by casting `CuccoMinigameRupees` to `int`, adding the cucco-type index, and re-casting back to `u8*`. On 64-bit hosts that drops the upper bits of the rupee-table pointer and the dereference SIGSEGVs as soon as the reward sequence runs. Replaced with a normal bounds-checked indexed read. (commit `d629b93c`)
- **#45 Chuchu missing animation (frozen blob).** Animation .bin files extracted at exact ROM size drop the trailing `loop_back` byte (on GBA it was the first byte of the next animation in ROM). The runtime's `FrameZero` hits `AnimRangeHasBytes()=false` on the loop frame, returns silently, and the chuchu freezes on the last frame. `Port_LoadSpritePtrsFromAssets` now allocates a padded copy of every animation that ends on a loop frame and appends a synthetic loop_back byte (next animation's first byte when in-range, otherwise loop-to-start). (commit `68f6e79f`)
- **#36 Cave of Flames: moving lava platforms missing.** Two layered bugs. (a) The asset extractor emits `room_properties/offset_<hex>.bin` files that contain only the leading 4-byte rail pointer of multi-chunk `gUnk_additional_*` tables; the runtime iterates 16-byte LavaPlatform entries from a 4-byte buffer, runs into uninitialised heap, and gives up before any moving platform spawns. `RomBackedPointerForAssetFile()` now recognises that filename pattern and returns `gRomData + <hex>` so the consumer sees the GBA-original contiguous layout including all interleaved rail pointers and chunks. (commit `8a8c92d3`) (b) Cave-of-Flames boss room separately SIGSEGV'd in `sub_0802D650`: `gUnk_080CD7E4/810/828/848` are packed 4-byte GBA function-pointer tables in `data_const_stubs.c`; the C declarations of `void (*const [])(...)` make x86-64 stride 8 bytes per index, calling garbage. Added native shadow tables in `gleerok.c` with the actual decompiled C functions, dispatched via a `GLEEROK_FN(table, idx)` macro. Also guards a NULL `heap->entity1` deref in `sub_0802E7E4` that GBA hardware silently wrote to BIOS at addr 0. (commits `fa77240c`, `9d5f55a5`)
- **#42 Melari's Mines: double heart containers + green warp tile.** Symptom of the same bug class as #43 below — `LoadRoomEntityList(&gUnk_additional_8_MelarisMine_Main)` reads 64 zero bytes from the stub and parses them as four `kind=0` (GROUND_ITEM) entities at world (0,0); the renderer draws those as the spurious heart containers + warp tile. (commit `86f62351`)
- **#43 Melari's Mines: Melari and his crew missing.** `gUnk_additional_8/9_MelarisMine_Main` are zero-initialised stubs in `port_linked_stubs.c`; the asset extractor doesn't index them and `Port_InitDataStubs` had no entry to copy them in from `gRomData`. The state-change path that loads Melari + the two mountain-minish apprentices read zeros and spawned nothing. Enlarged the additional_8 stub to 128 bytes (the GBA table is 96 B, the previous 64-byte size truncated past Melari) and added both stubs to `Port_InitDataStubs` so they're populated from `gRomData[0xDD214..]` and `gRomData[0xDD274..]` on every boot. (commit `86f62351`)

Bonus: a NO_CAP-mode dispatch bug in `sub_08077D38` (player-item animation lookup) had no `default` case, so any item without a no-cap-specific animation (boomerang, gust jar, pacci cane, etc.) ended up with an uninitialised `anim` value of `0x7FFF` — Link rendered as `SPRITE_JARPORTAL` (the pot) while using those items. Added `anim = ptr->frameIndex` as the default. Discovered while building the F8 debug menu's "Unlock all items" feature.

### Tooling

- **F8 debug menu.** In-game overlay with arrow-key navigation. Pages: Items / progress (unlock equipment, max hearts, 999 rupees, all kinstones), Warp (14 destinations including individual dungeon entries with the right spawn coordinates from `gWallMasterScreenTransitions`), Heal to full. Refuses to warp when not in `TASK_GAME` (toast says so). Renders via `SDL_RenderDebugText` after the game frame, intercepts input while open. (commit `ed136db3`)
- **F5 quicksave / F6 quickload.** Snapshots a curated set of game-state regions (gEwram/gIwram/gVram/gIoMem + gSave/gPlayerEntity/gPlayerState/gMain/gRoomControls/gRoomTransition + the full `gEntities[72]`) into a heap buffer on F5, memcpys back on F6 — handy for iterating on bug repros without replaying through a sequence of inputs. Single in-process slot, lost on exit. (commit `d6ddf908`)

### Build / merges

- **Sync with matheo/master ×2.** Picked up matheo's `Port_UnpackRomDataPtr` helper (cleaner than our `Port_PackedRomEntry` wrappers — same correctness, no `#ifdef PC_PORT` blocks at call sites), spear-moblin / hurdy-gurdy / percy / npcUtils / script `gUnk_08001A7C` cleanups using it, the `DmaCopy32` portable wrapper for the kinstone menu, the `houseDoorExterior` `sizeof(...)` bounds check, the `cuccoMinigame` rupees rewrite (we'd just landed our own — kept matheo's), and the AVX2 build option. (commits `afa4b3e2`, `023adf1b`)
- **Reverted `feature/skip_opening_cutscene` (#48).** It broke audio sync — the m4a engine state isn't flushed when the cutscene is skipped, so the next BGM plays ahead of the visuals. Reverted in `a2b806b7`; comment posted on the PR explaining the desync and what would need to change to land it cleanly.

### Known issues (still open)

- **#9 Steam Deck packages**, **#11 boss asset rendering**, **#15 libfmt.so.12 on Fedora 43** (header-only fmt is in xmake.lua but the released tarball needs a rebuild on the CI runner), **#17 GLIBC_2.43 on Nobara/Fedora 43** (CI runner pin to ubuntu-22.04 not yet effective), **#21 Link's house glitched doors**, **#26 fast-forward not working on Windows**, **#27 tree stump scaling** (fixed in `c2d69075`, awaiting tarball), **#28 random door above staircase** (fixed in `829de678`, awaiting test), **#32 shop top textures**, **#37 Lon Lon Ranch shrink/door**, **#40 Hyrule Town door texture**, **#41 EU text extract**, **#44 map grey blocks**, **#47 Mt Crenel background texture** — still open at the time of this build.

## 0.1.5-experimental — 2026-05-04

Issue-tracker bug-fix pass on top of 0.1.4. Eight tracker entries closed — including the cloud-shadow "lines after castle" carry-forward (#25) and the tall-grass shoes overlay (#24) that had been deferred since 0.1.2 — plus a port-side dispatcher fix that unblocks any enemy whose death cascade lives in OnCollision/OnKnockback. Adds an F9 bug-report capture for playtesters and surfaces the port version in the window title.

### Fixed (issue tracker)

- **#5 Deepwood Shrine: Link backflips off solid walls.** `FindValueForKey` iterates a `KeyValuePair` list until `key == 0`. The GBA assembled each list as N entries plus a trailing `gUnk_..End` u16 sentinel placed immediately after; the C port has those as separate top-level definitions which the linker is free to reorder. When the End ends up elsewhere, scanning falls through into whatever array follows. Verified at runtime: walking up into a Deepwood wall returned uVar3=5 from the next array's `{42, 5}` entry. Inline a `{0, 0}` sentinel at the end of each KeyValuePair array so each list self-terminates. Same fix applied to `sTiles0..3` in player.c which had the same single-entry-plus-trailing-u16 shape. (commit `38d8290a`)
- **#22 BGM doesn't duck during item-get jingle.** GBA m4a's per-channel priority allocation let `SFX_ITEM_GET` (high priority on `MUSIC_PLAYER_1E`) starve `MUSIC_PLAYER_BGM` of channels, naturally muting BGM. The PC backend renders all players independently. Reuse the existing `volumeBgm` fade plumbing: when `SoundReq` starts a recognized ducking SFX, set `volumeBgmTarget = 0`; each frame `AudioMain` polls a new `Port_M4A_Backend_IsPlayerActive` helper to detect when the SFX player has reached `ply_fine` and restores `volumeBgmTarget = 0x100`. (commit `0ecf37b4`)
- **#24 Tall-grass / shallow-water shoes overlay missing.** GBA renders a small overlay sprite over Link's feet when on tall grass (act-tile 0x2F) or shallow water (act-tile 0x0F). Frame data lives at ROM 0x080B2B58 in 16 IWRAM-relative pointers (4 shadow rows × 4 anim frames). The shadow-table fix (#10) skipped the IWRAM overlay copy and never resolved this companion table — the `ProcessEntityForDraw` `z >= 0` branch had a TODO. Translate the IWRAM pointers (delta 0x050AC28C, USA) and render via `RenderSpritePieces` when an entity at `z >= 0` flags `spritePriority & 8` and stands on the right act-tile. (commit `0178cf37`)
- **#25 Cloud overlay broken into lines after exiting Hyrule Castle.** Hyrule Castle's `holeManager` parallax overwrites `SCREENBASE 30` (cloud tilemap) with castle data; on PC the asset cache short-circuits the area gfx-group reload that would restore it on GBA. Snapshot the live BG3 VRAM (CHARBASE 1 chardata + SCREENBASE 30 tilemap) the first time `CloudOverlayManager_OnEnterRoom` fires for `AREA_HYRULE_FIELD` with a working overlay, then restore on every subsequent Hyrule Field entry — side-steps the question of which gfx group actually owns the cloud data. (commit `14b184b6`)
- **#34 Mt Crenel mountaintop renders pink/cyan terrain.** `weatherChangeManager.c` cross-fades the mountaintop palette via `sub_08059894(gPalette_549, gPalette_549 + 0xD0, factor)`. On GBA the linker placed `gPalette_549..gPalette_574` sequentially in `gfxAndPalettes`, so the +0xD0 read walks into `gPalette_562`. The PC port stubbed `gPalette_549` as a single 32-byte buffer, so the offset read fell off the end into garbage. Allocate the full 416-color block and populate it from `gGlobalGfxAndPalettes` at offset 0x44A0 (same on USA and EU). (commit `1a5d2931`)
- **#35 AcroBandit stack drifts off-screen + survivors don't fall.** Three layered bugs: (a) `AcroBanditEntity.unk_70/72` aliased `Enemy.homeX/homeY` on GBA (both at offset 0x70/72) but the alias broke on PC because Entity grew 0x68 → 0x90 bytes; chain bandits ended up with `homeX = 0` and the stack drifted toward (0,0). Restored the alias by padding the struct on PC. (b) Hazards (pit/water/lava) delete a chain bandit without going through OnCollision's chain unwind, leaving children with stale `parent` pointing at zeroed memory; added a self-heal in Action4 that promotes a bandit to chain head when `parent->kind != 3`. (c) The original chain unwind only walks DOWN from the dying bandit, leaving ancestors stuck in Action4 when the player kills the bottom; walk UP too so the whole stack falls together. (commit `b5ee0144`)
- **#39 Cave of Flames map crash on B3.** `GetAreaRoomPropertyList` had only NULL-or-ROM-range branches; for never-visited dungeon-boss areas the slot held a stale 4-byte→8-byte widen value (non-NULL but not a real address) and the fallback `areaTable[room]` dereference SIGSEGV'd. Added a third bucket: if the pointer is non-NULL, not in ROM, and not in the canonical x86-64 user-space range (< 2^47), force a refresh and reclassify. (commit `6dd75555`)
- **GetNextFunction skipped OnCollision/OnKnockback when health=0.** The PC fix added for #20 (Peahat corpse stuck in OnGrabbed loop) short-circuited to OnDeath as soon as `health == 0`, which also blocked the AcroBandit chain unwind (#35) and the death-fall animation. Preserve OnCollision/OnKnockback dispatch when their conditions are met, fall through to OnDeath only when neither is active. (commit `ef1b8344`)

### Tooling

- **F9 bug-report capture for playtesters.** Pressing F9 in-game writes a timestamped `bugreport_YYYYMMDD_HHMMSS/` directory next to the binary containing `screenshot.bmp` (240x160 GBA framebuffer), `save.bin` (current EEPROM dump), and `state.txt` (area / room / coords / hp / ROM size). Attach the folder to a GitHub issue. (commit `52cbd7e2`)
- **Window title now shows port version.** "The Minish Cap 0.1.5-experimental - 60.0 FPS" instead of just "The Minish Cap - 60.0 FPS", so testers can include their version when filing issues without digging through the changelog. (commit `52cbd7e2`)

### Known issues (still open)

- **#9 Steam Deck packages**, **#11 boss asset rendering**, **#21 Link's house glitched doors**, **#27 tree stump scaling** (already fixed in `c2d69075`, awaiting tarball), **#28 random door above staircase** (already fixed in `829de678`), **#32 shop top textures**, **#36 CoF moving platform** (already fixed), **#37 Lon Lon Ranch closed door**, **#38 LikeLike crash** (already fixed), **#40 Hyrule Town door texture**, **#41 EU text extract** — still open at the time of this build.

## 0.1.4-experimental — 2026-05-03

Hyrule Town + South Hyrule field playthrough fixes, driven by a live GDB-under-the-game session against the issue tracker. Three crashes and one stuck-state bug closed; CI build stability improvements; matheo upstream merged.

### Fixed (issue tracker)

- **#16 Hyrule Town kinstone-bag crash chain.** Five layered bugs all on the kinstone-fusion path that compound after picking up the bag: (1) `gUnk_08001A7C` and `gUnk_08001DCC` are packed 4-byte GBA-pointer tables but were declared as `T*[]` so `arr[idx]` read 8 bytes on x86-64 → garbage pointer → SIGSEGV the moment a kinstone fuser scripted up; (2) `(u8*)(fuserProgress + (u32)fuserData)` truncated a 64-bit pointer in `common.c::GetFusionToOffer`; (3) `TextDispEnquiry`'s post-A_BUTTON modulo divided by zero on x86 (ARM silently returns garbage) after MemClear zeroed `gMessageChoices.choiceCount`; (4) `kinstoneMenu.c::sub_080A4418` wrote to `DMA3->sourceAddress` (raw GBA hardware register address 0x040000D4, unmapped on PC); (5) `KinstoneMenu_080A4468` dereferenced `gPossibleInteraction.currentObject` without checking it pointed inside `candidates[]`. Fixed each + added `Port_PackedRomEntry()` helper for the ~5 packed-pointer-table call sites. (commit `b825b5fd`)
- **#19 South Hyrule field loading-zone crash.** Two bugs on the spear moblin: (a) `gUnk_080CC944` was the same packed-pointer-table pattern as #16, handing out garbage Hitbox pointers on x86-64; (b) `definition->ptr.hitbox` lives in read-only mmap'd ROM (gRomData), but the spear moblin's action routines write `hitbox->offset_x/y/width/height` every frame. Allocate a mutable `Hitbox3D` copy at init (`AllocMutableHitbox()` would `zFree` the const ROM pointer, so do it manually). (commit `c749c609`)
- **#20 Peahat (and other gust-jar-killable enemies) corpse never despawns.** `GetNextFunction()` returned 5 (OnGrabbed) for any entity with `gustJarState & 4` set, before the `health == 0` check. When a peahat is killed by gust jar, `Peahat_OnGrabbed_Subaction5` sets `health=0` but `gj=0x04` stays set until ENT_COLLIDE toggles, which doesn't happen in that subaction's else branch. Result: dead-while-grabbed enemies loop in OnGrabbed forever — corpse stays, no death animation. PC port now prefers the `health==0` dispatch so dead-while-grabbed enemies flow into GenericDeath → DEATH_FX → DeleteThisEntity. (commit `c708678a`)

### Build / CI

- **Linux runner pinned to `ubuntu-22.04`** (glibc 2.35) so the prebuilt tarball runs on Nobara 43, Fedora 43, Kubuntu 25.10, and any other distro that lags behind the Arch host's glibc 2.43. Addresses #2's class plus #17. (commits `91b5c9fd`, `a5f546da`)
- **Pre-generated USA `map_offsets.h` + `gfx_offsets.h` committed** under a `.gitignore` exception so CI can build without needing a private ROM repository. Drops `python build.py` from the workflow in favour of direct `xmake build tmc_pc` + `xmake build asset_extractor`, which don't need a ROM at compile time. (commit `fb36e928`)
- **Merged matheo/master twice today**: ubuntu fmt12 build fix (#15 root cause); update-check infrastructure (`port/port_update_check.{c,h}`); Minish Woods Ezlo fight + lilypad rail data fallbacks; bootstrap-based asset management (`port/port_asset_bootstrap.cpp`); affineSet cleanup. (commits `b5cf8067`, `d37edeb3`)

### Implicit fixes confirmed

- **#23 Broken map (grey tiles)** — works locally on the post-merge branch; one of the struct-alignment / BG-flush fixes resolved it. Awaiting Proton/Bazzite retest on this tarball.

### Known issues (still open)

- **#4 Pulled mushroom**, **#5 backflip at walls**, **#8 blue/red teleport icons**, **#9 Steam Deck packages**, **#11 boss asset rendering** (deferred to next sprite-extraction pass).
- **#21 Link's house glitched doors** — likely same class as the door-priority issue carried from 0.1.2.
- **#22 BGM doesn't mute on item-get** — audio priority/ducking, same area as the festival-house BGM-reset fix in 0.1.2 but with a different interaction.
- **#24 Tall-grass shoes overlay** — known carry-forward.
- **Inside-the-rolling-barrel scene**, **festival house facades**, **Minish Woods fog**, **mosaic effect** — renderer-iteration deferred from 0.1.2.

## 0.1.3-experimental — 2026-05-03

Bug-fix pass on top of 0.1.2 driven by the GitHub issue tracker — Deepwood Shrine playthrough now reaches the boss-clear warp, and a class of x86-64 struct-alignment bugs that was silently breaking ~30 entity subclasses is fixed at the source.

### Fixed (issue tracker)

- **#2 Linux white-screen on launch.** Asset loader fell back to `/proc/self/exe` to locate `assets/`, but Kubuntu users running through a custom `ld-linux` interpreter saw the loader path instead of the binary's directory. Now also probes the current working directory, and the missing-asset message points at `./asset_extractor` instead of "ROM fallback disabled". (`port/port_asset_loader.cpp`, `src/common.c`, commit `dc31679e`)
- **#3 Minish Village entrance vegetation missing.** Gfx group 30 has destinations in EWRAM (`gMapDataTopSpecial` at `0x02002F00`); the asset loader was writing those through the wrong resolver and the foliage tilemap stayed zeroed. Now routed through `Port_ResolveEwramPtr` like other heap-allocated game variables. (`port/port_asset_loader.cpp`, commit `0f728182`)
- **#6 Deepwood Shrine barrel doors render as flat colour bands** + **gust-jar barrel soft-lock.** Two unrelated bugs in the same room. (a) HBlank-DMA wasn't honouring `DEST_FIXED` vs `DEST_INC` vs `DEST_RELOAD` modes, so the cylindrical barrel-stave affine warp showed as solid bands. (b) The middle-hatch fall-through was gated on a barrel-rotation angle window the port never reached (max 0xF0 < required 0x118 because the port stops simulating once you're aligned); PC build now bypasses the angle gate. (`port/port_hdma.c`, `src/manager/rollingBarrelManager.c`, commits `107e7451`, `cd99dd4d`)
- **#7 "Mysterious Shells" textbox showed 0 obtained.** Number-variable substitution in textboxes wasn't being initialised on the port — `gUnk_08107BE0[1]` (the variable-slot pointer table) needed to point inside `gTextRender` so the code that copies the rupee count into the message buffer would land in the right place. Also fixed a `DecToHex` BCD-encoder regression that relied on GBA Div SWI's r1 remainder side effect; replaced with plain divmod. (`src/message.c`, `src/common.c`, commit `580ff28c`)
- **#10 Drop shadows missing.** `sShadowFrameTable` was never populated because the GBA original loads it via the IWRAM overlay copy (`sub_080B197C..RAMFUNCS_END`) that the PC port deliberately skips. Now loaded directly from ROM at first use, with IWRAM↔ROM offset translation for each region. Also fixed a draw-order bug where shadows rendered above their owning sprite. (`port/port_draw.c`, commits `4a191f01`, `cd99dd4d`)
- **#12 Boss reward chain (heart container + green warp) didn't spawn after defeating the Deepwood Shrine boss.** Three layered fixes:
  - `gUnk_additional_a_DeepwoodShrineBoss_Main` (the post-defeat entity list) was a zeroed 64-byte stub in `port_linked_stubs.c` because the asset extractor doesn't index it. Now populated from ROM 0x0DF94C in `Port_InitDataStubs`. (commit `cd99dd4d`)
  - **Root cause** (the data fix alone wasn't enough): GenericEntity has a `void*`-aligned `scriptContext` union that pushes `cutsceneBeh`/`field_0x86` to PC offset 0xB0/0xB2, but most entity subclass structs (`WarpPointEntity`, `HeartContainerEntity`, `GentariCurtainEntity`, `bossDoorEntity`, `lockedDoorEntity`, ~25 more) lay out a `flag` field at GBA offset 0x84/0x86 *without* that void* trick, landing at PC 0xAE. `RegisterRoomEntity` was writing `spritePtr` halves via `GE_FIELD` which always lands at 0xB0/0xB2, so subclass reads got junk — warps never armed and heart containers stayed in their hidden first-action state forever. Fix mirrors the writes to PC 0xAC/0xAE for all non-Enemy kinds, catching every affected subclass at once. (`src/room.c`, commit `bfd80ec6`)
- **#14 Minish Village elder (Gentari) wouldn't open the curtain after the first dungeon.** Same root cause as #12 — `GentariCurtainEntity::flags` at GBA 0x86 lands at PC 0xAE, was reading junk so the curtain animated as already-open or not at all. Universal struct-alignment fix from #12 covers it. (`src/room.c`, commit `bfd80ec6`)

### Other fixes carried since 0.1.2

- **Doorway crash on `HouseDoorExterior_Type3`** — guard against NULL script context when the spawner failed to resolve. (`src/object/houseDoorExterior.c`, commit `6bc17ac2`)
- **Map-hint cutscene black BG** — `sub_0807C4F8` now iterates native 24-byte `MapDataDefinition` structs from the asset loader; `RestoreGameTask` force-flushes the BG buffer→VRAM copy. (`src/playerUtils.c`, `src/gameUtils.c`, commits `01948f13`, `86f1463a`)

### Known issues (still open)

- **#4 Pulled mushroom (B1) renders as a flat tan rectangle while held.** Sprite-frame extraction edge case — `gSpriteAnimations_PullableMushroom` is one of ~900 animations the extractor flags as `Animation loop byte missing` / `Animation data has trailing bytes`. Held state falls back to zeroed frame data. The unmounted mushroom renders correctly. Tracked under the broader extractor fix.
- **#5 Backflip / vault triggers off solid walls in Deepwood Shrine.** Likely a `gMapTileTypeToActTile` mismatch — a wall tile is being mapped to a vaultable act-tile (43, 44, 65, 66, 76-79). Needs world coordinates / tile type to pin down.
- **#8 Blue/red teleport-icon parallax sprite missing.** PARALLAX_ROOM_VIEW spawns but its sprite frame may be in the same extraction-loss bucket as #4. Worth retesting with this release's struct-alignment fix.
- **#11 Boss texture renders incorrectly during the fight.** Likely also in the animation-extraction loss bucket; struct fix may help indirectly. Retest requested in 0.1.3.
- **#13 Re-entering the boss room after defeat breaks textures.** Per-room state (tilemap / palette / gfx group) may not be fully refreshed on re-entry. Needs a fresh repro on 0.1.3.
- **#15 Fedora 43: `libfmt.so.12: cannot open shared object`** when running the prebuilt `asset_extractor`. The xmake config sets `header_only = true` for fmt but the linker still pulls the host's `libfmt.so.12` SONAME. Workaround: install fmt 12 from upstream, or build from source with `python3 build.py --usa`. Build-side fix coming in 0.1.4.
- **Inside-the-rolling-barrel scene** still renders as flat brown bands — the `BG2PA` per-scanline DMA driving the cylindrical roll isn't honoured by VirtuaPPU even with the dest-mode fix above. Visible only inside the barrel; the room is otherwise playable.
- **Festival house facades, doorway sprite glitches, map-screen grey patches, cloud-shadow lines, Minish Woods fog, tall-grass shoes overlay** — renderer-iteration deferred from 0.1.2.
- **~900 animation entries** still skipped during extraction (`Animation loop byte missing` / `Animation data has trailing bytes`). Affects boss frames, Vaati cutscene, MazaalHand, mushroom carry pose, possibly the teleport icons.
- **Mosaic effect** on title fade and certain spell charges still kill-switched.

## 0.1.2-experimental — 2026-05-02

Bug-fix pass over a USA playthrough from prologue through Hyrule Field, plus a hard-found extraction bug that was silently dropping whole asset subtrees from release tarballs.

### Fixed

- **Doors in Hyrule town & overworld no longer make the game crash on entry.** `HouseDoorExterior_Type3` (the fully-scripted door variant) called `ExecuteScript(super, this->context)` even when the spawner failed to set `context` (port script resolution can return NULL where the GBA original always resolved). Reproduced entering `HyruleField/LonLonRanch`. Skip the script invocation under PC_PORT instead of segfaulting. (`src/object/houseDoorExterior.c`)
- **BG no longer goes black after a map-hint cutscene.** After the Mountain Minish elder shows the world map and the dialog continues, the room behind it was rendering pure black instead of returning to the room art. Two issues in `Subtask_FadeOut → RestoreGameTask → sub_0801AE44`: (a) `sub_0807C4F8` couldn't iterate native 24-byte `MapDataDefinition` structs from the asset loader (only handled ROM-packed 12-byte entries), so `gMapDataBottomSpecial` stayed zeroed; (b) even when the BG buffer got refilled correctly, no one raised `gScreen.bg.updated` so the buffer→VRAM copy never fired and VRAM kept the stale map tilemap. (`src/playerUtils.c`, `src/gameUtils.c`)
- **Lily pads in Minish Forest move again.** `data_080D5360/` was missing from extracted assets, so the lily-pad rail data couldn't be loaded and the pads sat still. See "Asset extraction" below for the underlying fix.
- **Festival house BGM stops resetting on every room transition.** `Port_M4A_Backend_StartSongById` was unconditionally restarting the same song each time `SoundReq` fired with the room's queued BGM. Now skipped when the same BGM (songId 1..99) is already playing on the same player. SFX still re-trigger correctly. (`port/port_m4a_backend.cpp`)
- **Hyrule Field / Castle Garden plays the correct prologue BGM.** USA region was using `BGM_FESTIVAL_APPROACH` for the first prologue scenes; matched the EU mapping (`BGM_BEANSTALK`) on the port so the music matches. (`src/roomInit.c`)
- **Stray location-name textbox no longer appears during Zelda's intro call.** `EnterRoomTextboxManager` was outliving an unrelated message and reasserting itself. Restored the GBA-original kill condition `(gMessage.state & MESSAGE_ACTIVE)` so the textbox dies when another message starts. (`src/manager/enterRoomTextboxManager.c`)
- **Magic-stump exit animation grows from small → big** (PC-port deviation from the canonical giant→normal animation, requested by user). Player entity's `unk_80` / `unk_84` now start at `0x80` and increment to `0x100`, matching the OOT-style growing-Link feel. (`src/player.c`)

### Asset extraction

- **`data_*/` subtrees no longer drop from release tarballs.** Two pipeline gaps caused fresh extractions to silently miss whole directories (most user-visibly `data_080D5360/`, where lily-pad rails and door data live):
  - `extract_area_tables` skipped property indices 4..7 unconditionally — they're usually room callbacks but some rooms put data pointers there. Now follows the offset when it matches an indexed asset entry, and a final sweep extracts every `EmbeddedAssetIndex` entry the specialized passes missed (~7300 additional files). (`tools/src/assets_extractor/assets_extractor.hpp`)
  - `CopyRuntimePassthroughAssets` had a fixed directory whitelist that excluded all `data_<addr>/` subtrees. Now lists them explicitly so they survive the `assets_src/ → assets/` runtime build. (`port/port_asset_pipeline.cpp`)
- `asset_processor` now creates `assets/` before scanning JSON configs, so a fresh checkout doesn't crash in extract mode. (`tools/src/asset_processor/main.cpp`)

### Known issues (still open)

- **Inside-the-rolling-barrel scene (Deepwood Shrine) renders as flat brown bands** instead of the textured barrel-stave + window view. Per-scanline HBlank-DMA on `REG_ADDR_BG2PA` (the affine matrix that creates the cylindrical roll) isn't being applied by VirtuaPPU. The same root cause likely affects light-ray/parallax effects (`vaatiAppearingManager`, `steamOverlayManager`, `lightRayManager`, `pauseMenuScreen6`) and the iris/circle window effects (`common.c` ×4). Visible: cosmetic; the room is otherwise playable.
- **Festival house facades, doorway sprite glitches, map-screen grey patches, cloud-shadow line artifacts, Minish Woods fog, tall-grass shoes overlay** — all renderer-iteration heavy and deferred until they can be debugged with eyes-on-screen.
- **`asset_extractor` warnings about "Animation loop byte missing" / "Animation data has trailing bytes"** still affect ~900 animations (Gyorg, Vaati cutscene, MazaalHand, etc.). Not used in the title or early gameplay; surfaces during specific cutscenes. Same root cause as 0.1.1.
- **Mosaic effect on title fade and certain spell charges is disabled** (kill-switched). Patch needs re-porting against current ViruaPPU `mode1.c`. Same as 0.1.1.

## 0.1.1-experimental — 2026-05-02

First end-to-end release-tarball flow: download `tmc-usa-{linux,windows}-0.1.1-experimental.tar.gz`, drop a `baserom.gba` next to it, run `./asset_extractor` once, then `./tmc_pc`. Works on Linux and Windows (real and via wine). Tarball ships only `tmc_pc[.exe]`, `asset_extractor[.exe]`, and `sounds.json` (104 KB metadata).

### Fixed

- **Title-screen sword + hilltop BG render correctly on Windows.** Mingw's static-CRT heap allocates inside the simulated GBA address window (0x02000000-0x0A000000), so `port_resolve_addr` mistranslated heap pointers to `&gEwram[N]` and the title-scene palette load read zeros from EWRAM. Fixed by reserving the GBA address window with `VirtualAlloc(MEM_RESERVE)` before any heap is opened on Windows. Linux glibc keeps malloc above 0x55... so it was never affected. (`port_main.c`)
- **Title sword routing restored.** The `matheo/master` merge regressed `ad9b4d94`'s GBA-mode-1 → VirtuaPPU-mode-2 routing via a "case handling" cleanup. BG2 affine was reading text-BG indexing and the title sword came out as garbage tiles. (`port_ppu.cpp`)
- **`asset_extractor` works from a release tarball.** Old logic required walking up to find an `xmake.lua` and bailed otherwise; new logic always writes `assets/` and `assets_src/` next to the executable, which is the same path `tmc_pc` looks under, in both dev and release layouts. (`tools/src/assets_extractor/assets_extractor_main.cpp`)
- **`tmc_pc` finds assets and ROM next to the binary on Linux/macOS too.** Replaced the cwd-only lookup with `readlink(/proc/self/exe)` / `_NSGetExecutablePath`, mirroring the Windows `GetModuleFileNameW` path. Double-clicking `tmc_pc` from a file manager now works. (`port/port_asset_loader.cpp`, `port/port_rom.c`)
- **`sounds.json` discovered next to the binary.** Audio backend now probes `<exe_dir>/sounds.json` and `<exe_dir>/assets/sounds.json` before the cwd-relative dev paths. Release tarballs ship `sounds.json` at the top level. (`port/port_m4a_backend.cpp`)
- **ViruaPPU patches re-apply automatically every build.** `xmake.lua`'s `before_build` callback was disabled in a stash; re-enabled with `git apply -3` so partial-upstream drift no longer aborts the patch step. The mosaic patch (drifted, has known issues) is removed from the auto-apply list — `TMC_NO_MOSAIC` (`ea33eb71`) covers the gameplay paths.

### Added

- Merged `matheo/master`: `build.py` end-to-end build helper and `.github/workflows/{_build,ci,release}.yaml` release workflow. Tagging `v*` on a fork with `ASSETS_REPO`/`ASSETS_TOKEN` configured will produce `tmc-{usa,eur}-{linux,windows}-<tag>.tar.gz` artifacts.

### Known issues

- `~900` animations (Gyorg, Vaati cutscene, MazaalHand, etc.) are skipped during extraction with `Animation loop byte missing` / `Animation data has trailing bytes`. Bug in `infer_asset_size` / `WriteEditableAnimation`'s end-of-stream detection — affects both platforms identically. Animations not used by the title or early gameplay; will surface during specific cutscenes.
- Mosaic effect on title fade and certain spell charges is disabled (kill-switched). Patch needs re-porting against current ViruaPPU `mode1.c`.

## 0.1.0-experimental — earlier

Initial PC port snapshot.
