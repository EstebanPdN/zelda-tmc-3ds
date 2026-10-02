/*
 * port_a11y_cues.c — Accessibility audio cues (Phase 1: surroundings scan).
 *
 * See port_a11y_cues.h for the design. Implementation notes:
 *
 *  - Entities are enumerated by walking the engine's per-kind linked
 *    lists (gEntityLists), using the same defensive walk the engine
 *    itself uses in entity.c: bounded step count + Port_IsValidEntityAddr
 *    so a corrupt `next` pointer can never spin or crash the scan.
 *      list index per kind (gEntityListLUT): ENEMY -> 4, OBJECT -> 6,
 *      NPC -> 7. OBJECT and MANAGER share list 6; managers classify to
 *      NULL and are skipped.
 *  - Positions are GBA Q16.16; the integer pixel is the high half-word
 *    (`.HALF.HI`). World space; the player and every entity share it.
 *  - Room exits live in gArea.pCurrentRoomInfo->exits (a Transition[]
 *    terminated by warp_type == WARP_TYPE_END_OF_LIST). AREA warps carry
 *    a room-relative anchor (startX/startY) we convert to world space;
 *    BORDER warps are whole room edges encoded in `shape` bits.
 *  - Classification uses engine enum identifiers by NAME (not numeric
 *    literals) so the values stay correct against the headers.
 *  - Only an occasional keypress drives this, so the integer sqrt and
 *    string building here are not on any hot path.
 */
#include "entity.h"
#include "player.h"
#include "object.h"
#include "npc.h"
#include "enemy.h"
#include "item_ids.h"
#include "sound.h"
#include "area.h"
#include "room.h"
#include "transitions.h"
#include "main.h"   /* gMain, TASK_GAME */
#include "asm.h"    /* GetCollisionDataAtWorldCoords */

#include "port_a11y_cues.h"
#include "port_tts.h"
#include "port_a11y_audio.h"
#include "port_runtime_config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* How far out the scan looks, and how many items it reads at most. A
 * full GBA screen is ~15x10 tiles; 12 tiles covers a bit beyond the
 * visible area without reaching across a large room. */
#define A11Y_SCAN_RADIUS_TILES 12
#define A11Y_MAX_ANNOUNCE      8

/* Border-edge bitmask for the exit summary. */
enum { EDGE_N = 1, EDGE_E = 2, EDGE_S = 4, EDGE_W = 8 };

static int A11y_DebugEnabled(void) {
    static int dbg = -1;
    if (dbg < 0) {
        const char* e = getenv("TMC_A11Y_DEBUG");
        dbg = (e && *e && *e != '0') ? 1 : 0;
    }
    return dbg;
}

extern int Port_IsValidEntityAddr(const void* p); /* entity.c */

/* ------------------------------------------------------------------ */
/* Geometry helpers                                                   */
/* ------------------------------------------------------------------ */

/* 8-way compass label for a delta in screen space (x east-positive,
 * y south-positive — the GBA convention). */
static const char* A11y_DirName(int dx, int dy) {
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx == 0 && ady == 0) return "here";
    if (adx > ady * 2) return dx > 0 ? "east" : "west";
    if (ady > adx * 2) return dy > 0 ? "south" : "north";
    if (dx > 0) return dy > 0 ? "south east" : "north east";
    return dy > 0 ? "south west" : "north west";
}

/* ------------------------------------------------------------------ */
/* Classification                                                     */
/* ------------------------------------------------------------------ */

static const char* A11y_GroundItemLabel(u8 itemType) {
    switch (itemType) {
        case ITEM_RUPEE1:
        case ITEM_RUPEE5:
        case ITEM_RUPEE20:
        case ITEM_RUPEE50:
        case ITEM_RUPEE100:
        case ITEM_RUPEE200:
            return "rupee";
        case ITEM_HEART:
            return "heart";
        case ITEM_HEART_PIECE:
            return "heart piece";
        case ITEM_HEART_CONTAINER:
            return "heart container";
        case ITEM_FAIRY:
            return "fairy";
        case ITEM_KINSTONE:
        case ITEM_KINSTONE_RED:
        case ITEM_KINSTONE_BLUE:
        case ITEM_KINSTONE_GREEN:
            return "kinstone";
        case ITEM_BIG_KEY:
            return "big key";
        case ITEM_SMALL_KEY:
            return "small key";
        case ITEM_BOMBS:
        case ITEM_REMOTE_BOMBS:
        case ITEM_BOMBS5:
        case ITEM_BOMBS10:
        case ITEM_BOMBS30:
            return "bombs";
        case ITEM_ARROWS5:
        case ITEM_ARROWS10:
        case ITEM_ARROWS30:
            return "arrows";
        case ITEM_SHELLS:
        case ITEM_SHELLS30:
            return "shells";
        default:
            return "item";
    }
}

/* Returns a spoken label for a point-of-interest entity, or NULL for
 * entities the scan should ignore (particles, managers, decoration). */
static const char* A11y_EntityLabel(const Entity* e) {
    switch (e->kind) {
        case ENEMY:
            if (((const Enemy*)e)->enemyFlags & EM_FLAG_BOSS) return "boss";
            return "enemy";
        case NPC:
            switch (e->id) {
                case DOG:         return "dog";
                case CAT:         return "cat";
                case COW:         return "cow";
                case CUCCO:       return "cucco";
                case CUCCO_CHICK: return "chick";
                case EPONA:       return "horse";
                default:          return "person";
            }
        case OBJECT:
            switch (e->id) {
                case CHEST_SPAWNER:
                case SPECIAL_CHEST:   return "chest";
                case GROUND_ITEM:     return A11y_GroundItemLabel(e->type);
                case RUPEE_OBJECT:    return "rupee";
                case FAIRY:           return "fairy";
                case GREAT_FAIRY:     return "great fairy";
                case HEART_CONTAINER: return "heart container";
                case WARP_POINT:      return "warp";
                case WHIRLWIND:       return "whirlwind";
                case LAVA_PLATFORM:   return "flipping platform";
                case PUSHABLE_ROCK:   return "rock";
                case POT:             return "pot";
                case BUSH:            return "bush";
                default:              return NULL;
            }
        default:
            return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Nearest-N collection                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    int         dist2;
    int         dx;
    int         dy;
    const char* label;
} A11yPoi;

static A11yPoi sNearest[A11Y_MAX_ANNOUNCE];
static int     sNearestCount;

/* Insertion-sort a candidate into the ascending-by-distance top-N. */
static void A11y_Consider(int dist2, int dx, int dy, const char* label) {
    int i;
    if (label == NULL) return;
    if (sNearestCount == A11Y_MAX_ANNOUNCE && dist2 >= sNearest[A11Y_MAX_ANNOUNCE - 1].dist2) {
        return; /* farther than everything we already kept */
    }
    if (sNearestCount < A11Y_MAX_ANNOUNCE) {
        i = sNearestCount++;
    } else {
        i = A11Y_MAX_ANNOUNCE - 1; /* evict the current farthest */
    }
    for (; i > 0 && sNearest[i - 1].dist2 > dist2; i--) {
        sNearest[i] = sNearest[i - 1];
    }
    sNearest[i].dist2 = dist2;
    sNearest[i].dx = dx;
    sNearest[i].dy = dy;
    sNearest[i].label = label;
}

static void A11y_ScanEntityList(int listIndex, int px, int py, int radiusPx) {
    LinkedList* list = &gEntityLists[listIndex];
    Entity* e;
    int steps = 0;
    for (e = list->first;
         e != NULL && (intptr_t)e != (intptr_t)list && steps < 256 && Port_IsValidEntityAddr(e);
         e = e->next, ++steps) {
        const char* label;
        int dx, dy, dist2;
        if (e->flags & ENT_DELETED) continue;
        label = A11y_EntityLabel(e);
        if (label == NULL) continue;
        dx = (int)e->x.HALF.HI - px;
        dy = (int)e->y.HALF.HI - py;
        if (dx > radiusPx || dx < -radiusPx || dy > radiusPx || dy < -radiusPx) continue;
        dist2 = dx * dx + dy * dy;
        if (dist2 > radiusPx * radiusPx) continue;
        A11y_Consider(dist2, dx, dy, label);
    }
}

/* Returns a border-edge bitmask; AREA warps are fed to A11y_Consider as
 * regular distance-ranked points of interest. */
static int A11y_ScanExits(int px, int py, int radiusPx) {
    int edgeMask = 0;
    const Transition* t;
    if (gArea.pCurrentRoomInfo == NULL) return 0;
    t = gArea.pCurrentRoomInfo->exits;
    if (t == NULL) return 0;
    for (; t->warp_type != WARP_TYPE_END_OF_LIST; t++) {
        if (t->warp_type == WARP_TYPE_AREA || t->warp_type == WARP_TYPE_AREA2) {
            /* startX/startY are room-relative; convert to world space. */
            int ex = (int)gRoomControls.origin_x + (int)t->startX;
            int ey = (int)gRoomControls.origin_y + (int)t->startY;
            int dx = ex - px;
            int dy = ey - py;
            const char* label = (t->transition_type == TRANSITION_TYPE_STAIR_EXIT) ? "stairs" : "exit";
            if (dx > radiusPx || dx < -radiusPx || dy > radiusPx || dy < -radiusPx) continue;
            A11y_Consider(dx * dx + dy * dy, dx, dy, label);
        } else {
            /* BORDER / BORDER2: whole room edges encoded in `shape`. */
            u8 shape = t->shape;
            if (shape & 0x03) edgeMask |= EDGE_N;
            if (shape & 0x0c) edgeMask |= EDGE_E;
            if (shape & 0x30) edgeMask |= EDGE_S;
            if (shape & 0xc0) edgeMask |= EDGE_W;
        }
    }
    return edgeMask;
}

/* Fill sNearest[] with the nearest entity POIs and return the room exit
 * edge bitmask. Shared by the scan and the cycle action. */
static int A11y_CollectNearest(int px, int py, int radiusPx) {
    sNearestCount = 0;
    A11y_ScanEntityList(4, px, py, radiusPx); /* enemies */
    A11y_ScanEntityList(6, px, py, radiusPx); /* objects (chests, items, features) */
    A11y_ScanEntityList(7, px, py, radiusPx); /* NPCs / animals */
    return A11y_ScanExits(px, py, radiusPx);
}

/* Format one nearby POI as "label, dir, very close." / "label, dir, N tiles."
 * into buf (size n); returns snprintf's count (may exceed n on truncation,
 * matching the originals). Shared by the scan and the cycle action. */
static int A11y_FormatPoi(char* buf, size_t n, const A11yPoi* poi) {
    int tiles = ((int)sqrtf((float)poi->dist2) + 8) / 16;
    const char* dir = A11y_DirName(poi->dx, poi->dy);
    if (tiles <= 0)
        return snprintf(buf, n, "%s, %s, very close.", poi->label, dir);
    return snprintf(buf, n, "%s, %s, %d tile%s.", poi->label, dir, tiles, tiles == 1 ? "" : "s");
}

/* Append "Exits: north, east, ..." for the set edge bits. Mirrors the original
 * inline decode: only the "Exits:" lead is pos-guarded, the per-direction
 * writes rely on snprintf's own bound (bufsz - *pos). */
static void A11y_AppendExits(char* buf, size_t bufsz, int* pos, int edgeMask) {
    if (edgeMask == 0 || *pos >= (int)bufsz) return;
    *pos += snprintf(buf + *pos, bufsz - *pos, "Exits: ");
    if (edgeMask & EDGE_N) *pos += snprintf(buf + *pos, bufsz - *pos, "north, ");
    if (edgeMask & EDGE_E) *pos += snprintf(buf + *pos, bufsz - *pos, "east, ");
    if (edgeMask & EDGE_S) *pos += snprintf(buf + *pos, bufsz - *pos, "south, ");
    if (edgeMask & EDGE_W) *pos += snprintf(buf + *pos, bufsz - *pos, "west, ");
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                 */
/* ------------------------------------------------------------------ */

void Port_A11y_ScanSurroundings(void) {
    Entity* player = &gPlayerEntity.base;
    int px, py, radiusPx, i, edgeMask;
    char buf[1024];
    int pos = 0;
    PortTtsOptions opts;

    if (!Port_TTS_GetEnabled()) return;

    /* The persistent player entity never carries ENT_DID_INIT, so gate
     * on the engine task instead: TASK_GAME covers overworld, menus and
     * cutscenes — anywhere the room/entity pool is meaningful. */
    if (gMain.task != TASK_GAME) {
        if (A11y_DebugEnabled())
            fprintf(stderr, "[a11y] not in game (task=%u)\n", (unsigned)gMain.task);
        Port_TTS_AnnounceMessage("Not in the game.");
        return;
    }

    px = (int)player->x.HALF.HI;
    py = (int)player->y.HALF.HI;
    radiusPx = A11Y_SCAN_RADIUS_TILES * 16;

    edgeMask = A11y_CollectNearest(px, py, radiusPx);

    if (sNearestCount == 0 && edgeMask == 0) {
        if (A11y_DebugEnabled()) fprintf(stderr, "[a11y] nothing of interest nearby\n");
        Port_TTS_AnnounceMessage("Nothing of interest nearby.");
        return;
    }

    for (i = 0; i < sNearestCount && pos < (int)sizeof(buf); i++) {
        pos += A11y_FormatPoi(buf + pos, sizeof(buf) - pos, &sNearest[i]);
        if (pos < (int)sizeof(buf))
            pos += snprintf(buf + pos, sizeof(buf) - pos, " ");
    }

    A11y_AppendExits(buf, sizeof(buf), &pos, edgeMask);

    /* URGENT + no dedupe so a fresh scan replaces an in-flight one and
     * repeated presses always speak. */
    opts.priority = PORT_TTS_PRIO_URGENT;
    opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f; /* NaN -> use settings */
    opts.voice = NULL;
    opts.language = NULL;
    opts.dedupe = false;
    if (A11y_DebugEnabled()) { fprintf(stderr, "[a11y] scan: %s\n", buf); fflush(stderr); }
    Port_TTS_Speak(buf, &opts);
}

/* ================================================================== */
/* Passive cue layer (Phase 2): tonal enemy radar + footsteps +       */
/* hazard/surface warnings + wall bumps, driven once per gameplay      */
/* frame from Port_UpdateInput (port_bios.c).                          */
/* ================================================================== */

/* Enemy radar reach (tiles) — shorter than the scan so it only flags
 * nearby threats. */
#define A11Y_RADAR_TILES 8

/* Per-category enables — config (Port_Config_*) drives these; default on. */
static bool sCuePassive   = true;
static bool sCueFootsteps = true;
static bool sCueHazards   = true;
static bool sCueRadar     = true;
static bool sCueWalls     = true;

/* Frame-to-frame state (game thread only). */
static int  sLastPx, sLastPy;
static int  sStepAccum;
static int  sLastSurface = -1;
static int  sRadarTimer;
static int  sWallTimer;
static bool sPassiveInit;

void Port_A11y_SetPassiveEnabled(bool on)   { sCuePassive = on;   Port_Config_SetA11yCues(on); }
bool Port_A11y_GetPassiveEnabled(void)      { return sCuePassive; }
void Port_A11y_SetFootstepsEnabled(bool on) { sCueFootsteps = on; Port_Config_SetA11yFootsteps(on); }
bool Port_A11y_GetFootstepsEnabled(void)    { return sCueFootsteps; }
void Port_A11y_SetHazardsEnabled(bool on)   { sCueHazards = on;   Port_Config_SetA11yHazards(on); }
bool Port_A11y_GetHazardsEnabled(void)      { return sCueHazards; }
void Port_A11y_SetRadarEnabled(bool on)     { sCueRadar = on;     Port_Config_SetA11yRadar(on); }
bool Port_A11y_GetRadarEnabled(void)        { return sCueRadar; }
void Port_A11y_SetWallsEnabled(bool on)     { sCueWalls = on;     Port_Config_SetA11yWalls(on); }
bool Port_A11y_GetWallsEnabled(void)        { return sCueWalls; }

/* Load persisted toggles into the module. Call once after config load
 * (port_main.c). Idempotent. */
void Port_A11y_Init(void) {
    sCuePassive   = Port_Config_GetA11yCues();
    sCueFootsteps = Port_Config_GetA11yFootsteps();
    sCueHazards   = Port_Config_GetA11yHazards();
    sCueRadar     = Port_Config_GetA11yRadar();
    sCueWalls     = Port_Config_GetA11yWalls();
}

/* (dx,dy) -> stereo pan (east = right) and pitch (nearer = higher). A
 * top-down world can't encode north/south on a stereo field, so the
 * tonal radar conveys left/right + distance; the spoken F10 scan gives
 * the full cardinal direction. */
static void A11y_PanPitchFor(int dx, int dy, float* pan, float* freq) {
    int dist = (int)sqrtf((float)(dx * dx + dy * dy));
    int tiles;
    float p;
    if (dist < 1) dist = 1;
    p = (float)dx / (float)dist;
    if (p < -1.0f) p = -1.0f; else if (p > 1.0f) p = 1.0f;
    *pan = p;
    tiles = dist / 16;
    if (tiles > A11Y_RADAR_TILES) tiles = A11Y_RADAR_TILES;
    *freq = 1300.0f - (float)tiles * (900.0f / (float)A11Y_RADAR_TILES);
}

static const char* A11y_SurfaceName(int s) {
    switch (s) {
        case SURFACE_PIT:           return "pit";
        case SURFACE_HOLE:          return "hole";
        case SURFACE_WATER:         return "deep water";
        case SURFACE_SHALLOW_WATER: return "shallow water";
        case SURFACE_SWAMP:         return "swamp";
        case SURFACE_ICE:           return "ice";
        case SURFACE_LADDER:
        case SURFACE_AUTO_LADDER:   return "ladder";
        case SURFACE_CLIMB_WALL:    return "climbable wall";
        default:                    return NULL;
    }
}

static bool A11y_SurfaceIsFallHazard(int s) {
    return s == SURFACE_PIT || s == SURFACE_HOLE || s == SURFACE_WATER;
}

static void A11y_FootstepTone(int surface) {
    float freq = 170.0f;
    A11yWave wave = A11Y_WAVE_SINE;
    switch (surface) {
        case SURFACE_SHALLOW_WATER:
        case SURFACE_WATER:  freq = 260.0f; wave = A11Y_WAVE_TRIANGLE; break;
        case SURFACE_ICE:    freq = 330.0f; break;
        case SURFACE_SWAMP:  freq = 130.0f; wave = A11Y_WAVE_TRIANGLE; break;
        default:             freq = 170.0f; break;
    }
    Port_A11yAudio_Beep(0.0f, freq, 28, 0.20f, wave);
}

static bool A11y_NearestEnemy(int px, int py, int radiusPx, int* outDx, int* outDy) {
    LinkedList* list = &gEntityLists[4]; /* enemy list */
    Entity* e;
    int steps = 0;
    int bestD2 = radiusPx * radiusPx + 1;
    bool found = false;
    for (e = list->first;
         e != NULL && (intptr_t)e != (intptr_t)list && steps < 256 && Port_IsValidEntityAddr(e);
         e = e->next, ++steps) {
        int dx, dy, d2;
        if (e->flags & ENT_DELETED) continue;
        if (e->kind != ENEMY) continue;
        dx = (int)e->x.HALF.HI - px;
        dy = (int)e->y.HALF.HI - py;
        if (dx > radiusPx || dx < -radiusPx || dy > radiusPx || dy < -radiusPx) continue;
        d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; *outDx = dx; *outDy = dy; found = true; }
    }
    return found;
}

void Port_A11y_Update(void) {
    Entity* player = &gPlayerEntity.base;
    int px, py, dx, dy, moved;

    if (!sCuePassive) { sPassiveInit = false; return; }
    if (gMain.task != TASK_GAME || player->kind != PLAYER) { sPassiveInit = false; return; }

    px = (int)player->x.HALF.HI;
    py = (int)player->y.HALF.HI;

    if (!sPassiveInit) {
        sLastPx = px; sLastPy = py;
        sStepAccum = 0;
        sLastSurface = gPlayerState.floor_type;
        sRadarTimer = 0; sWallTimer = 0;
        sPassiveInit = true;
        return;
    }

    dx = px - sLastPx;
    dy = py - sLastPy;
    moved = dx * dx + dy * dy;

    /* Footsteps: one tick per ~14px travelled, tinted by surface. */
    if (sCueFootsteps && moved > 0) {
        sStepAccum += (int)sqrtf((float)moved);
        if (sStepAccum >= 14) {
            sStepAccum = 0;
            A11y_FootstepTone(gPlayerState.floor_type);
        }
    }

    /* Surface change: warning buzz on fall hazards, spoken name for
     * notable surfaces. Only fires on a transition, so standing still
     * stays quiet. */
    if (sCueHazards) {
        int surf = gPlayerState.floor_type;
        if (surf != sLastSurface) {
            const char* name = A11y_SurfaceName(surf);
            if (A11y_SurfaceIsFallHazard(surf)) {
                Port_A11yAudio_Beep(0.0f, 150.0f, 180, 0.5f, A11Y_WAVE_SQUARE);
                if (name) Port_TTS_AnnounceMessage(name);
            } else if (name) {
                Port_TTS_AnnounceMessage(name);
            }
            sLastSurface = surf;
        }
    }

    /* Wall bump: trying to move (speed set) but position didn't change.
     * Rate-limited so a held direction into a wall ticks, not buzzes. */
    if (sCueWalls) {
        if (sWallTimer > 0) sWallTimer--;
        if (player->speed > 0 && moved == 0 && sWallTimer == 0) {
            float pan = 0.0f;
            int d = player->direction & 0x18;
            if (d == DirectionEast) pan = 0.7f;
            else if (d == DirectionWest) pan = -0.7f;
            Port_A11yAudio_Beep(pan, 110.0f, 40, 0.22f, A11Y_WAVE_SQUARE);
            sWallTimer = 18;
        }
    }

    /* Enemy radar: periodic panned + pitched ping toward the nearest
     * enemy in range. */
    if (sCueRadar) {
        if (sRadarTimer > 0) sRadarTimer--;
        if (sRadarTimer == 0) {
            int edx, edy;
            sRadarTimer = 48; /* ~0.8 s between pings */
            if (A11y_NearestEnemy(px, py, A11Y_RADAR_TILES * 16, &edx, &edy)) {
                float pan, freq;
                A11y_PanPitchFor(edx, edy, &pan, &freq);
                Port_A11yAudio_Beep(pan, freq, 60, 0.28f, A11Y_WAVE_SINE);
            }
        }
    }

    sLastPx = px;
    sLastPy = py;
}

/* ================================================================== */
/* On-demand extensions: cycle nearby objects + look around.          */
/* ================================================================== */

static int sCycleIndex;

/* Step to the next nearby point of interest: a directional beep plus a
 * spoken "label, direction, distance". Rebuilds the nearest list each
 * press so it tracks the current state. */
void Port_A11y_CycleNext(void) {
    Entity* player = &gPlayerEntity.base;
    A11yPoi* p;
    int px, py;
    float pan, freq;
    char buf[128];
    PortTtsOptions opts;

    if (!Port_TTS_GetEnabled()) return;
    if (gMain.task != TASK_GAME) { Port_TTS_AnnounceMessage("Not in the game."); return; }

    px = (int)player->x.HALF.HI;
    py = (int)player->y.HALF.HI;
    A11y_CollectNearest(px, py, A11Y_SCAN_RADIUS_TILES * 16);
    if (sNearestCount == 0) {
        Port_TTS_AnnounceMessage("Nothing nearby.");
        sCycleIndex = 0;
        return;
    }
    if (sCycleIndex >= sNearestCount) sCycleIndex = 0;
    p = &sNearest[sCycleIndex];
    sCycleIndex++;

    A11y_FormatPoi(buf, sizeof(buf), p);

    A11y_PanPitchFor(p->dx, p->dy, &pan, &freq);
    Port_A11yAudio_Beep(pan, freq, 70, 0.30f, A11Y_WAVE_SINE);

    opts.priority = PORT_TTS_PRIO_URGENT;
    opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
    opts.voice = NULL;
    opts.language = NULL;
    opts.dedupe = false;
    if (A11y_DebugEnabled()) { fprintf(stderr, "[a11y] cycle: %s\n", buf); fflush(stderr); }
    Port_TTS_Speak(buf, &opts);
}

/* Orientation readout: the surface under the player, each cardinal tile
 * (open / wall / blocked), and the room exits. Collision classification
 * is conservative — only 0 is reported "open" and 0xff "wall"; anything
 * else is "blocked" so a wall is never mistaken for open ground. */
void Port_A11y_LookAround(void) {
    static const struct { const char* name; int dx; int dy; } kDirs[4] = {
        { "north", 0, -16 }, { "east", 16, 0 }, { "south", 0, 16 }, { "west", -16, 0 },
    };
    Entity* player = &gPlayerEntity.base;
    int px, py, layer, i, edgeMask, pos = 0;
    const char* surf;
    char buf[256];
    PortTtsOptions opts;

    if (!Port_TTS_GetEnabled()) return;
    if (gMain.task != TASK_GAME) { Port_TTS_AnnounceMessage("Not in the game."); return; }

    px = (int)player->x.HALF.HI;
    py = (int)player->y.HALF.HI;
    layer = player->collisionLayer;

    surf = A11y_SurfaceName(gPlayerState.floor_type);
    pos += snprintf(buf + pos, sizeof(buf) - pos, "On %s. ", surf ? surf : "ground");

    for (i = 0; i < 4 && pos < (int)sizeof(buf); i++) {
        unsigned c = (unsigned)GetCollisionDataAtWorldCoords((u32)(px + kDirs[i].dx),
                                                             (u32)(py + kDirs[i].dy), (u32)layer);
        const char* st = (c == 0) ? "open" : (c == 0xff ? "wall" : "blocked");
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s %s. ", kDirs[i].name, st);
    }

    /* Reuse the exit edge decode (resets sNearest as a side effect). */
    sNearestCount = 0;
    edgeMask = A11y_ScanExits(px, py, A11Y_SCAN_RADIUS_TILES * 16);
    A11y_AppendExits(buf, sizeof(buf), &pos, edgeMask);

    if (A11y_DebugEnabled()) { fprintf(stderr, "[a11y] look: %s\n", buf); fflush(stderr); }

    opts.priority = PORT_TTS_PRIO_URGENT;
    opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
    opts.voice = NULL;
    opts.language = NULL;
    opts.dedupe = false;
    Port_TTS_Speak(buf, &opts);
}
