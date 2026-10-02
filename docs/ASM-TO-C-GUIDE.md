# GBA Assembly to PC-Native C/C++ Conversion Guide

This document explains the architecture, translation strategy, and alignment requirements for substituting GBA hardware assembly with PC-native C/C++ implementations in the `tmc_pc` target.

---

## 1. Build Architecture & Linkage

The codebase maintains a dual-target architecture configured in `xmake.lua`:

1.  **GBA ROM Target (`tmc`):** Compiles decompiled C source files (`src/`) and falls back to original GBA assembly files (`asm/**/*.s` and `data/**/*.s`) for non-decompiled or non-matching functions.
2.  **PC Port Target (`tmc_pc`):** Compiles *only* the C/C++ source code (`src/` and `port/`). It completely excludes the GBA assembler. All functions originally implemented in GBA assembly are substituted by C/C++ equivalents compiled directly into the binary.

### Translation Directory Mapping

| GBA ASM Path | PC Port C/C++ Equivalent | Purpose |
|---|---|---|
| `asm/src/crt0.s` | `port/port_main.c` | Startup, SDL3 initialization, and task entry into `AgbMain()`. |
| `asm/src/intr.s` | `port/port_bios.c` / `port_ppu.cpp` | Interrupt handling, VBlank synchronization, and rendering presentation. |
| `asm/src/player.s` | `src/player.c` / `port/port_linked_stubs.c` | Player movement, actions, and state transitions. |
| `asm/src/code_*.s` | `port/port_gameplay_stubs.c` / `port_stubs.c` | System utilities, scripting wrappers, and memory management. |
| `asm/lib/m4a_asm.s` | `port/port_m4a_backend.cpp` / `libs/agbplay_core` | Audio driver execution and sequencer parsing (re-implemented in C++). |
| `asm/src/stack_check.s` | *Excluded* | Not required for the native PC runtime. |

---

## 2. Core Translation Mechanisms

For functions originally in assembly, the PC port resolves symbols using three main approaches:

### A. Hand-written Port Replacements (`port/port_linked_stubs.c`, `port_gameplay_stubs.c`, `port_stubs.c`)
High-level C implementations of GBA-specific assembly routines:
*   **Script Command Parsing (`port/port_stubs.c`):** Implements `GetNextScriptCommandHalfword()` and `GetNextScriptCommandWord()` to read scripted events from ROM-data structures using PC-compatible pointer offsets.
*   **Collision Engine (`port/stubs_autogen.c`):** `ram_CollideAll()` and `PortCalcCollision()` replace the GBA's `arm_CalcCollision` assembly routine, executing the full entity bounding box checks, priority filters, and knockback matrix resolutions natively.

### B. Auto-Generated Stubs (`port/stubs_autogen.c`)
Managed by developer scripts (`tools/gen_stubs.py`), this file provides native implementations for GBA-specific memory structures and resolved function entrypoints that are not compiled in the PC build.

### C. GBA BIOS Emulation (`port/port_bios.c`)
Replaces GBA system calls (swi) with PC-native C routines:
*   `Div()` / `DivArm()`: Replaced by native division operators.
*   `Sqrt()`: Replaced by `<math.h>`'s `sqrt()`.
*   `LZ77UnCompVram()` / `LZ77UnCompWram()`: Replaced by a C-based LZ77 decompressor.

---

## 3. Memory Alignment & Pointer Width Padding

On GBA, `Entity` and other structures are 32-bit (pointers are 4 bytes). On 64-bit PCs, pointers are 8 bytes.
Because `Entity` has pointer fields (such as `next`, `prev`, `child`, `animPtr`), the base size of `Entity` grows:
*   **GBA Entity Size:** `0x68` bytes.
*   **PC Entity Size (64-bit):** `0x90` bytes.

This `0x28` byte shift pushes out all custom fields defined in subclass structures (like `Enemy` or specific enemy/object entities).

### The Offset Mismatch Hazard
If an enemy subclass structure is defined with a byte-counted filler array (e.g. `u8 unused1[12]`) followed by custom fields:
*   **GBA Offset:** `0x68 (base) + 12 (unused1) = 0x74`
*   **PC Offset (without padding):** `0x90 (base) + 12 (unused1) = 0x9C`
*   **PC Offset (actual class field offset):** `Enemy::field_0x74` sits at `0xA0` on PC.

This creates a 4-byte misalignment, causing the PC code to read/write wrong offsets when interacting with parameters populated during room loading.

### The Alignment Fix Pattern
To align the fields, conditional compiler padding (`#ifdef PC_PORT`) must be added to the structures, shifting the custom fields to match the PC-widened layout.

Example alignment from `src/enemy/keaton.c`:
```c
typedef struct {
    /*0x00*/ Entity base;
#ifdef PC_PORT
    /*0x68*/ u8 unused1[12 + 4]; // Shift by 4 bytes to align with 0xA0 on PC
#else
    /*0x68*/ u8 unused1[12];
#endif
    /*0x74*/ u16 unk_74;
    /*0x76*/ u16 unk_76;
    /*0x78*/ u16 unk_78;
    /*0x7a*/ u8 unused2[1];
    /*0x7b*/ u8 unk_7b;
} KeatonEntity;
```

### Compile-Time Validation
Every padded structure must be guarded with a compile-time static assert to guarantee that alignment modifications remain correct:
```c
PORT_STATIC_ASSERT_OFFSET(KeatonEntity, unk_74, 0x74, 0xA0,
                          "KeatonEntity unk_74 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(KeatonEntity, unk_7b, 0x7b, 0xA7,
                          "KeatonEntity unk_7b offset incorrect");
```

---

## 4. Audio Emulation & Quantization

The original GBA audio driver (`m4a_asm.s` / `m4a.c`) depends on cycle-approximate DMA refills, timers, and hardware audio registers. 
Instead of emulating these low-level hardware structures, the PC port uses a behavioral C++ implementation of the MP2K engine:

1.  **Sequencer & Synthesizer:** `libs/agbplay_core` parses the original ROM sound data streams, handles ADSR envelopes, and synthesizes PSG (square/wave/noise) and DirectSound (PCM) channels.
2.  **GBA-Accurate Limits & Stealing:** The interpreter parses the `m4aSoundMode` channel nibble and enforces the GBA's software channel cap (`maxChannels`, hard-clamped to a limit of 12) inside `SequenceReader.cpp`, culling lower-priority or releasing voices first.
3.  **9-bit PWM DAC Simulation:** Under GBA-Accurate mode, the mixed 16-bit output is quantized to match the GBA's 9-bit PWM DAC resolution (SOUNDBIAS=9):
    ```c
    buffer[i] = (int16_t)((buffer[i] / 128) * 128); // Zeroes lower 7 bits of 16-bit sample
    ```

---

## 5. Key Decomp Bug Remediations

During translation, several latent GBA-only compiler assumptions and bugs were corrected to ensure stability on PC:

### Save File Checksum Corruption Fix (`src/save.c`)
*   **The Bug:** The GBA checksum status check added `checksum1` (normal) and `checksum2` (two's complement negation). If `checksum1` happened to be exactly `0`, both checksums were `0`, and the sum was `0` instead of `0x10000` — rendering the save file unreadable.
*   **The PC Fix:** 
    ```c
    #ifdef PC_PORT
                if (fileStatus->checksum2 == (u16)(-fileStatus->checksum1)) {
    #else
                if (fileStatus->checksum1 + fileStatus->checksum2 == 0x10000) {
    #endif
    ```

### Widescreen Cutscene Margin Fallback (`port/port_linked_stubs.c`)
*   To prevent HUD and menu tilemaps from bleeding columns past 240 during dialogue and cutscenes, widescreen is deactivated (rendering black bars and clipping backgrounds at 240) whenever the player's controls are disabled or dialogue is active:
    ```c
    int Port_Widescreen_IsActive(void) {
        if (gMain.controlMode == CONTROL_DISABLED || (gMessage.state & MESSAGE_ACTIVE) != 0) {
            return 0;
        }
        ...
    }
    ```
