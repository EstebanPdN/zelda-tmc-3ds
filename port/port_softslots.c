/*
 * port_softslots.c — Extra item-equip buttons (X / Y / L2 / R2).
 *
 * Engine dispatch path (src/playerUtils.c:UpdateActiveItems) reads
 *   gSave.stats.equipped[SLOT_B] and INPUT_USE_ITEM2 each frame.
 * To add four extra equip buttons without touching the save format we:
 *   1. Each frame, scan the four soft-slot inputs. The most recently
 *      pressed slot with an assigned item wins (sticky while held).
 *   2. While a slot is active, port_bios.c forces B_BUTTON pressed in the
 *      GBA KEYINPUT register so the engine produces an INPUT_USE_ITEM2
 *      bit on its abstract input.
 *   3. Two functions in src/playerUtils.c (the B-dispatch site and the
 *      held-item active check) consult Port_SoftSlots_GetEffectiveBItem /
 *      Port_SoftSlots_IsBHeld to override the effective equipped[SLOT_B]
 *      ONLY at those points. The save data is never mutated.
 *
 * Charged items (Gust Jar, Bow): the active state persists for as long
 * as the soft-slot button is held, so the engine's "hold to charge,
 * release to fire" works automatically. When the button releases, B is
 * no longer forced and the engine sees the release.
 *
 * Multi-slot: last-pressed wins. Pressing R2 while still holding L2
 * switches the active slot mid-action; releasing the newer one with the
 * older still held picks any remaining held slot (highest index) so an
 * item is always firing rather than a flicker between two states.
 *
 * Persistence: tmc.softslots, sidecar to tmc.sav. 6-byte magic + 4 bytes.
 * Survives across runs but is unrelated to the EEPROM save.
 */

#include "port_softslots.h"
#include "port_runtime_config.h"

#ifndef TMC_3DS
#include <SDL3/SDL.h>
#endif
#include <stdio.h>
#include <string.h>

/* Engine-side queries used by the assignment UI. Declared as plain externs
 * so this TU doesn't drag in the full game headers (and the type collisions
 * that come with them). The numeric ITEM_* values match include/item.h —
 * see comments next to the table below. */
extern unsigned int GetInventoryValue(unsigned int item);

/* Mirror of the relevant prefix of ItemMetaData. We only need menuSlot and
 * the existence of the entry; the real struct (include/itemMetaData.h) has
 * eight u8 fields starting with menuSlot. */
struct PortSoftSlotItemMeta { unsigned char menuSlot; unsigned char rest[7]; };
extern const struct PortSoftSlotItemMeta gItemMetaData[];

#define SOFTSLOT_FILENAME "tmc.softslots"
static const char SOFTSLOT_MAGIC[6] = { 'T', 'M', 'C', 'S', 'S', '1' };

static uint8_t sAssignments[PORT_SOFTSLOT_COUNT];
static int sActiveSlot = -1;
static bool sLoaded = false;


const char* Port_SoftSlots_SlotName(int slot) {
    switch (slot) {
        case 0: return "X";
        case 1: return "Y";
        case 2: return "L2";
        case 3: return "R2";
        default: return "?";
    }
}

void Port_SoftSlots_Init(void) {
    if (sLoaded) return;
    Port_SoftSlots_Load();
    sLoaded = true;
}

void Port_SoftSlots_Update(void) {
    Port_SoftSlots_Init();

    static bool sPrevHeld[PORT_SOFTSLOT_COUNT] = { false, false, false, false };
    bool nowHeld[PORT_SOFTSLOT_COUNT];
    int newlyPressed = -1;

    for (int i = 0; i < PORT_SOFTSLOT_COUNT; i++) {
        nowHeld[i] = Port_Config_SoftSlotPressed(i) && sAssignments[i] != 0;
        if (nowHeld[i] && !sPrevHeld[i]) {
            /* Later iterations overwrite, giving last-iterated == highest-
             * index newly-pressed slot priority. */
            newlyPressed = i;
        }
    }

    int chosen = -1;
    if (newlyPressed >= 0) {
        chosen = newlyPressed;
    } else if (sActiveSlot >= 0 && nowHeld[sActiveSlot]) {
        chosen = sActiveSlot;
    } else {
        for (int i = PORT_SOFTSLOT_COUNT - 1; i >= 0; i--) {
            if (nowHeld[i]) {
                chosen = i;
                break;
            }
        }
    }

    sActiveSlot = chosen;
    memcpy(sPrevHeld, nowHeld, sizeof(sPrevHeld));
}

bool Port_SoftSlots_IsBHeld(void) {
    return sActiveSlot >= 0;
}

uint8_t Port_SoftSlots_GetEffectiveBItem(uint8_t saved) {
    if (sActiveSlot < 0) return saved;
    uint8_t a = sAssignments[sActiveSlot];
    return a ? a : saved;
}

uint8_t Port_SoftSlots_GetAssignment(int slot) {
    if (slot < 0 || slot >= PORT_SOFTSLOT_COUNT) return 0;
    return sAssignments[slot];
}

void Port_SoftSlots_SetAssignment(int slot, uint8_t itemId) {
    if (slot < 0 || slot >= PORT_SOFTSLOT_COUNT) return;
    sAssignments[slot] = itemId;
    Port_SoftSlots_Save();
}

void Port_SoftSlots_Save(void) {
    FILE* f = fopen(SOFTSLOT_FILENAME, "wb");
    if (!f) return;
    fwrite(SOFTSLOT_MAGIC, 1, sizeof(SOFTSLOT_MAGIC), f);
    fwrite(sAssignments, 1, sizeof(sAssignments), f);
    fclose(f);
}

void Port_SoftSlots_Load(void) {
    memset(sAssignments, 0, sizeof(sAssignments));
    FILE* f = fopen(SOFTSLOT_FILENAME, "rb");
    if (!f) return;
    char magic[sizeof(SOFTSLOT_MAGIC)];
    if (fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
        memcmp(magic, SOFTSLOT_MAGIC, sizeof(magic)) == 0) {
        if (fread(sAssignments, 1, sizeof(sAssignments), f) != sizeof(sAssignments)) {
            memset(sAssignments, 0, sizeof(sAssignments));
        }
    }
    fclose(f);
}

/* ---- Assignment UI helpers (cycling through owned items) -------------- */

/* Equippable items in cycle order. Mirrors the ITEM_* enum values from
 * include/item.h that have a menu slot — i.e. the things the pause menu
 * lets the player put on A or B. ITEM_LANTERN_OFF is preferred over
 * ITEM_LANTERN_ON because that's the cold-start id; the engine swaps to
 * _ON at runtime. ITEM_BOMBS / _BOW / _BOOMERANG / _SHIELD / _LANTERN
 * pair up with their upgraded variants — owning the upgraded variant
 * implies ownership of the base one for our purposes. */
static const struct { uint8_t id; const char* name; } kEquippable[] = {
    /* values mirror include/item.h enum positions 1..0x21 */
    { 1,  "Sword (Smith)"   }, /* ITEM_SMITH_SWORD     */
    { 2,  "Sword (Green)"   }, /* ITEM_GREEN_SWORD     */
    { 3,  "Sword (Red)"     }, /* ITEM_RED_SWORD       */
    { 4,  "Sword (Blue)"    }, /* ITEM_BLUE_SWORD      */
    { 7,  "Bombs"           }, /* ITEM_BOMBS           */
    { 8,  "Remote Bombs"    }, /* ITEM_REMOTE_BOMBS    */
    { 9,  "Bow"             }, /* ITEM_BOW             */
    { 10, "Light Arrow"     }, /* ITEM_LIGHT_ARROW     */
    { 11, "Boomerang"       }, /* ITEM_BOOMERANG       */
    { 12, "Magic Boomerang" }, /* ITEM_MAGIC_BOOMERANG */
    { 13, "Shield"          }, /* ITEM_SHIELD          */
    { 14, "Mirror Shield"   }, /* ITEM_MIRROR_SHIELD   */
    { 15, "Lantern"         }, /* ITEM_LANTERN_OFF     */
    { 17, "Gust Jar"        }, /* ITEM_GUST_JAR        */
    { 18, "Pacci Cane"      }, /* ITEM_PACCI_CANE      */
    { 19, "Mole Mitts"      }, /* ITEM_MOLE_MITTS      */
    { 20, "Roc's Cape"      }, /* ITEM_ROCS_CAPE       */
    { 21, "Pegasus Boots"   }, /* ITEM_PEGASUS_BOOTS   */
    { 23, "Ocarina"         }, /* ITEM_OCARINA         */
    { 28, "Bottle 1"        }, /* ITEM_BOTTLE1         */
    { 29, "Bottle 2"        }, /* ITEM_BOTTLE2         */
    { 30, "Bottle 3"        }, /* ITEM_BOTTLE3         */
    { 31, "Bottle 4"        }, /* ITEM_BOTTLE4         */
};

static const char* ItemDisplayName(uint8_t id) {
    if (id == 0) return "(unassigned)";
    for (size_t i = 0; i < sizeof(kEquippable) / sizeof(kEquippable[0]); i++) {
        if (kEquippable[i].id == id) return kEquippable[i].name;
    }
    return "?";
}

const char* Port_SoftSlots_GetSlotLabel(int slot) {
    static char buf[64];
    if (slot < 0 || slot >= PORT_SOFTSLOT_COUNT) return "?";
    uint8_t id = sAssignments[slot];
    snprintf(buf, sizeof(buf), "%-3s : %s",
             Port_SoftSlots_SlotName(slot), ItemDisplayName(id));
    return buf;
}

/* Returns the table index of `id`, or -1 if not in the table. */
static int CycleIndexOf(uint8_t id) {
    if (id == 0) return -1; /* "unassigned" lives outside the table */
    for (size_t i = 0; i < sizeof(kEquippable) / sizeof(kEquippable[0]); i++) {
        if (kEquippable[i].id == id) return (int)i;
    }
    return -1;
}

void Port_SoftSlots_CycleAssignment(int slot, int direction) {
    if (slot < 0 || slot >= PORT_SOFTSLOT_COUNT) return;
    if (direction == 0) return;
    int step = direction > 0 ? 1 : -1;
    int n = (int)(sizeof(kEquippable) / sizeof(kEquippable[0]));

    /* Walk the circular sequence (unassigned, kEquippable[0..n-1]) until
     * we hit a position the player owns (or wrap back to start). */
    int idx = CycleIndexOf(sAssignments[slot]); /* -1 = unassigned */
    for (int tries = 0; tries < n + 1; tries++) {
        idx += step;
        if (idx >= n) {
            idx = -1; /* wrap to "unassigned" */
        } else if (idx < -1) {
            idx = n - 1;
        }
        uint8_t cand = idx < 0 ? 0 : kEquippable[idx].id;
        if (cand == 0 || GetInventoryValue(cand) == 1) {
            Port_SoftSlots_SetAssignment(slot, cand);
            return;
        }
    }
    /* Player has no owned items at all; leave unchanged. */
}

/* ---- Pause-menu integration --------------------------------------- */

/* Frames-of-grace counter. Subtask_PauseMenu pumps this each engine frame
 * the menu is open; Port_UpdateInput decays it. We stay "active" for a
 * couple of frames after the engine stops calling NotifyPauseActive so
 * the overlay doesn't flicker during the engine's frame jitter. */
static int sPauseFramesGrace = 0;
static bool sConfigOpen = false;
static int sConfigCursor = 0;

void Port_SoftSlots_NotifyPauseActive(void) {
    sPauseFramesGrace = 4;
}

void Port_SoftSlots_TickPause(void) {
    if (sPauseFramesGrace > 0) {
        sPauseFramesGrace--;
    } else if (sConfigOpen) {
        /* Player closed the start menu while config overlay was open
         * (e.g. Esc'd out of pause via Start). Drop the overlay too. */
        sConfigOpen = false;
    }
}

bool Port_SoftSlots_IsPauseActive(void) {
    return sPauseFramesGrace > 0;
}

bool Port_SoftSlots_ConfigIsOpen(void) {
    return sConfigOpen;
}

void Port_SoftSlots_ConfigOpen(void) {
    Port_SoftSlots_Init();
    sConfigOpen = true;
    sConfigCursor = 0;
}

void Port_SoftSlots_ConfigClose(void) {
    sConfigOpen = false;
}

bool Port_SoftSlots_ConfigHandleKey(int sdlKey) {
#ifdef TMC_3DS
    (void)sdlKey;
    return false;
#else
    if (!sConfigOpen) return false;
    switch (sdlKey) {
        case SDLK_UP:
            sConfigCursor = (sConfigCursor + PORT_SOFTSLOT_COUNT - 1) % PORT_SOFTSLOT_COUNT;
            return true;
        case SDLK_DOWN:
            sConfigCursor = (sConfigCursor + 1) % PORT_SOFTSLOT_COUNT;
            return true;
        case SDLK_LEFT:
            Port_SoftSlots_CycleAssignment(sConfigCursor, -1);
            return true;
        case SDLK_RIGHT:
            Port_SoftSlots_CycleAssignment(sConfigCursor, +1);
            return true;
        case SDLK_RETURN:
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            sConfigOpen = false;
            return true;
        default:
            return false;
    }
#endif
}

/* ---- Rendering ---------------------------------------------------- */

#ifndef TMC_3DS
static const char* SlotValueLabel(int slot) {
    /* sAssignments stored across runs; render "—" for empty so the panel
     * looks intentional rather than crashed. */
    uint8_t id = sAssignments[slot];
    if (id == 0) return "---";
    /* Reuse ItemDisplayName — the same name table the cycle code drives. */
    static char buf[32];
    SDL_strlcpy(buf, ItemDisplayName(id), sizeof(buf));
    return buf;
}

/* The visible UI is the SDL `\` configuration overlay (RenderConfigOverlay,
 * below) and the F8 → "Extra equip slots" page in the debug menu. */

static void RenderConfigOverlay(SDL_Renderer* r, int winW, int winH) {
    /* Centered modal: 4 stacked rows, one per slot, with arrow hints. */
    const int charW = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    const float rowH = 28.0f;
    const float rowGap = 4.0f;
    const float boxW = 360.0f;
    const float boxH = 60.0f + rowH * 4 + rowGap * 3;
    SDL_FRect box = { (winW - boxW) * 0.5f, (winH - boxH) * 0.5f, boxW, boxH };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 230);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 200, 220, 255, 255);
    SDL_RenderRect(r, &box);

    /* Title */
    const char* title = "EXTRA EQUIP SLOTS";
    int titleLen = (int)SDL_strlen(title);
    SDL_SetRenderDrawColor(r, 200, 220, 255, 255);
    SDL_RenderDebugText(r, box.x + (box.w - titleLen * charW) * 0.5f,
                        box.y + 8.0f, title);

    float y = box.y + 30.0f;
    for (int i = 0; i < PORT_SOFTSLOT_COUNT; i++) {
        bool sel = (i == sConfigCursor);
        SDL_FRect row = { box.x + 12.0f, y, box.w - 24.0f, rowH };

        SDL_SetRenderDrawColor(r, sel ? 60 : 30, sel ? 60 : 30, sel ? 80 : 40, 220);
        SDL_RenderFillRect(r, &row);
        SDL_SetRenderDrawColor(r, sel ? 255 : 140, sel ? 240 : 140, sel ? 64 : 160, 255);
        SDL_RenderRect(r, &row);

        char left[8];
        SDL_snprintf(left, sizeof(left), "[%s]", Port_SoftSlots_SlotName(i));
        SDL_RenderDebugText(r, row.x + 8.0f, row.y + 9.0f, left);

        const char* lArrow = sel ? "<" : " ";
        const char* rArrow = sel ? ">" : " ";
        SDL_RenderDebugText(r, row.x + 60.0f, row.y + 9.0f, lArrow);
        const char* val = SlotValueLabel(i);
        SDL_RenderDebugText(r, row.x + 80.0f, row.y + 9.0f, val);
        SDL_RenderDebugText(r, row.x + row.w - 16.0f, row.y + 9.0f, rArrow);

        y += rowH + rowGap;
    }

    /* Footer hint */
    SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
    const char* foot = "Up/Down pick  Left/Right cycle  Enter/Esc done";
    int footLen = (int)SDL_strlen(foot);
    SDL_RenderDebugText(r, box.x + (box.w - footLen * charW) * 0.5f,
                        box.y + box.h - 18.0f, foot);
}
#endif

void Port_SoftSlots_RenderOverlay(void* sdl_renderer, int winW, int winH) {
#ifdef TMC_3DS
    (void)sdl_renderer;
    (void)winW;
    (void)winH;
#else
    SDL_Renderer* r = (SDL_Renderer*)sdl_renderer;
    if (!r) return;
    if (sConfigOpen) {
        RenderConfigOverlay(r, winW, winH);
    }
#endif
}
