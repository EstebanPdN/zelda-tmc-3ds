/* Debug entity viewer (PC port).
 *
 * Read-only inspector that snapshots every live Entity across the nine
 * gEntityLists into a flat array the F8 menu can render. Walking the engine's
 * intrusive circular lists is hazardous: a corrupted or mid-deletion list can
 * contain a NULL `next`, a node outside valid entity memory, or a cycle (see
 * the Lake Hylia warp repro in src/color.c and the takeover-cutscene exit in
 * src/entity.c). We mirror the engine's own defensive walk — bound each list,
 * stop on NULL, and validate every node with Port_IsValidEntityAddr — so the
 * viewer can never spin or fault on bad list state.
 *
 * Snapshot model: the UI calls Port_DebugQuery_RefreshEntities() once per frame
 * (or on demand), then reads rows by index. This gives a consistent view even
 * though the lists mutate as entities spawn/despawn between frames. */

#include "global.h"
#include "entity.h"
#include "port_debug_actions.h"

extern int Port_IsValidEntityAddr(const void* p); /* entity.c */

#define PORT_ENTITY_SNAP_MAX 512
#define PORT_ENTITY_LIST_CAP 256 /* per-list walk bound; far above any real length */

static PortEntityInfo sSnap[PORT_ENTITY_SNAP_MAX];
static int sSnapCount = 0;

int Port_DebugQuery_RefreshEntities(void) {
    int n = 0;
    int li;
    for (li = 0; li < 9 && n < PORT_ENTITY_SNAP_MAX; li++) {
        LinkedList* ll = &gEntityLists[li];
        Entity* e = ll->first;
        int steps = 0;
        while (e != (Entity*)ll && e != NULL && steps < PORT_ENTITY_LIST_CAP &&
               n < PORT_ENTITY_SNAP_MAX && Port_IsValidEntityAddr(e)) {
            PortEntityInfo* info = &sSnap[n];
            info->listIndex = li;
            info->kind = e->kind;
            info->id = e->id;
            info->type = e->type;
            info->x = (int)(s16)e->x.HALF.HI;
            info->y = (int)(s16)e->y.HALF.HI;
            info->health = e->health;
            n++;
            e = e->next;
            steps++;
        }
    }
    sSnapCount = n;
    return n;
}

int Port_DebugQuery_EntitySnapshotCount(void) {
    return sSnapCount;
}

const PortEntityInfo* Port_DebugQuery_Entity(int i) {
    if (i < 0 || i >= sSnapCount) {
        return (const PortEntityInfo*)0;
    }
    return &sSnap[i];
}

const char* Port_DebugQuery_EntityKindName(int kind) {
    switch (kind) {
        case PLAYER:      return "Player";
        case ENEMY:       return "Enemy";
        case PROJECTILE:  return "Projectile";
        case OBJECT:      return "Object";
        case NPC:         return "NPC";
        case PLAYER_ITEM: return "PlayerItem";
        case MANAGER:     return "Manager";
        default:          return "?";
    }
}
