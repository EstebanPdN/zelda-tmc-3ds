#ifndef PORT_SOFTSLOTS_H
#define PORT_SOFTSLOTS_H

/*
 * Soft item slots — extra equip buttons (X / Y / L2 / R2) for the PC port.
 *
 * The GBA only had A and B, so item dispatch in src/playerUtils.c reads
 * gSave.stats.equipped[SLOT_A/B] each frame. With a PC controller (or
 * keyboard) we want quick-access to ~four items without pause-menu juggling.
 *
 * Soft-slots never mutate the save; instead the dispatch and held-item
 * sites call Port_SoftSlots_GetEffectiveBItem() and Port_SoftSlots_IsBHeld()
 * to consult the soft-slot state. See port_softslots.c for the mechanism.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define PORT_SOFTSLOT_COUNT 4

void Port_SoftSlots_Init(void);

/* Polled once per frame from Port_UpdateInput. Reads the four soft-slot
 * inputs and decides which (if any) is the current active slot. */
void Port_SoftSlots_Update(void);

/* True iff a soft-slot button is held with an item assigned. The port
 * input layer ORs B_BUTTON into KEYINPUT in this case so the engine sees
 * a regular B-press and routes it through CreateItemIfInputMatches. */
bool Port_SoftSlots_IsBHeld(void);

/* Returns the soft-slot's item if a slot is active, else `saved`. Used by
 * src/playerUtils.c at the B-dispatch site to override the effective
 * equipped[SLOT_B] without touching gSave. */
uint8_t Port_SoftSlots_GetEffectiveBItem(uint8_t saved);

uint8_t Port_SoftSlots_GetAssignment(int slot);
void    Port_SoftSlots_SetAssignment(int slot, uint8_t itemId);
const char* Port_SoftSlots_SlotName(int slot);

/* Display name for the item currently in `slot` (or "—" when unassigned).
 * Stable pointer; caller does not free. */
const char* Port_SoftSlots_GetSlotLabel(int slot);

/* Cycle the assignment in `slot` to the next/previous item the player
 * actually owns (`direction` is +1 or -1). Wraps through "unassigned"
 * (item id 0). Persists immediately. */
void Port_SoftSlots_CycleAssignment(int slot, int direction);

void Port_SoftSlots_Save(void);
void Port_SoftSlots_Load(void);

/* ------------------------------------------------------------------
 * Pause-menu integration
 *
 * The engine's Subtask_PauseMenu calls NotifyPauseActive() each frame
 * the menu is open; the port input layer decays the counter via
 * TickPause() so we know when the player closes it. IsPauseActive() is
 * used by the SDL overlay to know when to render the soft-slot info
 * panel underneath the engine's pause/start menu.
 * ------------------------------------------------------------------ */
void Port_SoftSlots_NotifyPauseActive(void);
void Port_SoftSlots_TickPause(void);
bool Port_SoftSlots_IsPauseActive(void);

/* Inline configuration overlay (opened from the start menu via the
 * bound config key). When open, all keys are routed to it first via
 * Port_SoftSlots_ConfigHandleKey — anything it consumes shouldn't reach
 * the engine. */
bool Port_SoftSlots_ConfigIsOpen(void);
void Port_SoftSlots_ConfigOpen(void);
void Port_SoftSlots_ConfigClose(void);
bool Port_SoftSlots_ConfigHandleKey(int sdlKey);

/* Render hook: called once per frame from port_ppu.cpp. Renders the
 * configuration overlay (modal) when open. No-op otherwise. */
void Port_SoftSlots_RenderOverlay(void* sdl_renderer, int winW, int winH);

/* (Earlier iterations had Port_SoftSlots_DrawInfoIntoFramebuffer and
 *  Port_SoftSlots_PushBadge here for native pause-menu visuals. They
 *  hit issues we couldn't resolve without iterative visual feedback —
 *  framebuffer bar occluded the item-name display, OAM tile injection
 *  leaked into pause-menu sprite tiles. Removed; the visible UI is the
 *  SDL config overlay reached via `\` during pause and the F8 page.) */

#ifdef __cplusplus
}
#endif

#endif
