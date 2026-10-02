/*
 * port_debug_menu.cpp — F8 in-game debug menu.
 *
 * Renders an SDL overlay using SDL_RenderDebugText. While open, all game
 * input is masked (Port_UpdateInput consults Port_DebugMenu_IsOpen) and
 * SDL key events are routed to the menu instead of the game.
 *
 * Pages are an array of items; each item is either a submenu pointer or
 * a callable action. Up/Down navigates, Enter activates, B/Esc backs out
 * (and closes the menu when at the top level).
 *
 * Game-state mutations live in port_debug_actions.c so this TU doesn't
 * need to include the game headers (which don't parse as C++ — they use
 * `this` as a parameter name).
 */

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "port_debug_menu.h"
#include "port_debug_query.h"
#include "port_debug_actions.h"
#include "port_widescreen.h"
#include "item_ids.h" /* BOTTLE_CHARM_* / ITEM_BOTTLE_PICOLYTE_* enum ids (C++-safe) */

/* Console-Parity getter (port_runtime_config.cpp) — used to annotate the
 * noclip row; avoids pulling the whole runtime-config header in here. */
extern "C" bool Port_Config_GetConsoleParity(void);

extern "C" {

/* Display / runtime-config knobs — same set the file-select "L Settings"
 * panel exposes, so the F8 menu can drive them mid-game. */
void Port_PPU_ToggleFullscreen(void);
bool Port_PPU_IsFullscreen(void);
void Port_PPU_CycleWindowScale(int direction);
unsigned char Port_PPU_WindowScale(void);
void Port_PPU_ApplyWindowScale(void);
void Port_PPU_CyclePresentationMode(int direction);
const char* Port_PPU_PresentationModeName(void);
void Port_PPU_CycleFilter(int direction);
const char* Port_PPU_FilterName(void);
unsigned int Port_Config_TargetFps(void);
void Port_Config_CycleTargetFps(int direction);
unsigned char Port_Config_InternalScale(void);
void Port_Config_CycleInternalScale(int direction);
bool Port_Config_WidescreenEnabled(void);
void Port_Config_SetWidescreenEnabled(bool enabled);

/* Soft-slot equip-button assignments (port_softslots.c). */
const char* Port_SoftSlots_GetSlotLabel(int slot);
void Port_SoftSlots_CycleAssignment(int slot, int direction);

/* Save-state slots (port_quicksave.c). */
int Port_QuickSave_SaveSlot(int slot);
int Port_QuickSave_LoadSlot(int slot);
int Port_QuickSave_HasSlot(int slot);
unsigned long long Port_QuickSave_SlotTimestamp(int slot);
int Port_QuickSave_SlotCount(void);
int Port_QuickSave_AutoSlotBase(void);
int Port_QuickSave_AutoSlotCount(void);
int Port_QuickSave_AutoEnabled(void);
void Port_QuickSave_SetAutoEnabled(int enabled);
unsigned int Port_QuickSave_AutoIntervalMs(void);
void Port_QuickSave_SetAutoIntervalMs(unsigned int ms);
/* Persistence wrappers — keep config.json in sync with runtime toggles. */
bool Port_Config_AutosaveEnabled(void);
void Port_Config_SetAutosaveEnabled(bool enabled);
unsigned int Port_Config_AutosaveIntervalMs(void);
void Port_Config_SetAutosaveIntervalMs(unsigned int ms);

/* Save profiles (port_save.c + port_runtime_config.cpp). */
const char* Port_Save_GetActivePath(void);
void Port_Save_SetActivePath(const char* path);
int Port_Save_SaveAsProfile(const char* path);
int Port_Save_FilenameMax(void);
int Port_Save_ListProfiles(char (*out)[64], int max);
const char* Port_Config_ActiveSaveProfile(void);
void Port_Config_SetActiveSaveProfile(const char* path);
}

namespace {

/* Mirror a few enum values from include/area.h here so the menu doesn't
 * pull in the game headers. Update if these area indices ever change. */
constexpr unsigned char AREA_MINISH_WOODS = 0x00;
constexpr unsigned char AREA_MINISH_VILLAGE = 0x01;
constexpr unsigned char AREA_HYRULE_TOWN = 0x02;
constexpr unsigned char AREA_HYRULE_FIELD = 0x03;
constexpr unsigned char AREA_MT_CRENEL = 0x06;
constexpr unsigned char AREA_MELARIS_MINE = 0x10;
constexpr unsigned char AREA_DEEPWOOD_SHRINE = 0x48;
constexpr unsigned char AREA_DEEPWOOD_SHRINE_BOSS = 0x49;
constexpr unsigned char AREA_DEEPWOOD_SHRINE_ENTRY = 0x4A;
constexpr unsigned char AREA_CAVE_OF_FLAMES = 0x50;
constexpr unsigned char AREA_CAVE_OF_FLAMES_BOSS = 0x51;
constexpr unsigned char AREA_FORTRESS_OF_WINDS = 0x58;
constexpr unsigned char AREA_TEMPLE_OF_DROPLETS = 0x60;
constexpr unsigned char AREA_ROYAL_CRYPT = 0x68;
constexpr unsigned char AREA_PALACE_OF_WINDS = 0x70;

bool sOpen = false;

/* Classic-menu selection state for the per-dungeon-items and buff sub-pages.
 * File-scope so the cycle lambdas share one selection that persists across
 * page re-pushes (the ribbon keeps the equivalent state in its own statics). */
int sClassicDungeonSel = 0; /* selected dungeon id, 0..15 */
int sClassicCharmSel = 1;   /* charm combo index; 0 = Off */
int sClassicPicoSel = 1;    /* picolyte combo index; 0 = Off */
int sClassicFlagBank = 0;   /* selected flag bank, 0..12 */
int sClassicFlagIndex = 0;  /* selected flag index within the bank */
/* Memory-watch candidate (the address being composed before "Add watch"). */
unsigned int sClassicMemAddr = 0x03000000u; /* IWRAM base — a sane starting point */
int sClassicMemWidth = 0;                   /* 0=u8 1=u16 2=u32 */
int sClassicMemStepIdx = 0;                 /* address step = 1 << (4*idx): 1,0x10,...,0x10000000 */

struct MenuItem {
    std::string label;
    std::function<void()> action;
    /* Optional cycle handlers for value-toggle items (Display settings page).
     * When set, Left/Right invoke them and the renderer prefers labelFn over
     * the static label so the visible row updates with the current value. */
    std::function<void()> cycleLeft;
    std::function<void()> cycleRight;
    std::function<std::string()> labelFn;
};

struct MenuPage {
    std::string title;
    std::vector<MenuItem> items;
    int cursor = 0;
    /* Viewport: index of the topmost visible item. Renderer + key handler
     * together keep `cursor` inside [viewportTop, viewportTop + visible). */
    int viewportTop = 0;
    /* Optional self-rebuilder for pages whose item SET changes in response to
     * an action (e.g. the memory-watch list growing/shrinking). An action calls
     * RequestRebuild(); ApplyPendingMutations() then replaces this page with
     * rebuild() at the safe deferred point. Needed because Pop()+Push() can't
     * replace the current page — the deferred pop runs from the top and would
     * discard the just-pushed page. */
    std::function<MenuPage()> rebuild;
};

/* Maximum number of items shown at once on a page. Larger pages scroll —
 * cursor still walks every item, but only a window of this many is drawn. */
constexpr int kVisibleItemsMax = 18;

std::vector<MenuPage> sPageStack;
std::string sToast; /* Temporary message shown at bottom of screen. */
unsigned int sToastUntilTicks = 0;

/* Items in sPageStack store std::function lambdas. Clearing the stack
 * inside one of those lambdas would destroy the std::function whose body
 * is currently executing — even though the executing copy is a local,
 * the implementation is fragile enough that doing it has been blamed for
 * a crash on "Close menu". Defer the actual stack clear/pop to the
 * top-level HandleKey caller via these flags. */
int sPendingPops = 0;
bool sPendingClose = false;
/* Set by RequestRebuild() when an action mutates the current page's item set.
 * Applied (top page replaced via its rebuild closure) in ApplyPendingMutations,
 * after the action lambda has returned — same deferral discipline as the pops. */
bool sPendingRebuild = false;

void Toast(const std::string& msg) {
    sToast = msg;
    sToastUntilTicks = SDL_GetTicks() + 1500;
}

/* ------- Page builders (forward-declared so actions can push pages) ------- */
MenuPage BuildItemsPage(void);
MenuPage BuildWarpPage(void);
MenuPage BuildAllAreasPage(void);
MenuPage BuildAreaRoomsPage(unsigned char area);
MenuPage BuildDisplaySettingsPage(void);
MenuPage BuildSoftSlotsPage(void);
MenuPage BuildSaveStatesPage(void);
MenuPage BuildSaveProfilesPage(void);
MenuPage BuildMainPage(void);
MenuPage BuildItemTogglePage(void);
MenuPage BuildDungeonItemsPage(void);
MenuPage BuildBuffsPage(void);
MenuPage BuildStatsPage(void);
MenuPage BuildBottlesPage(void);
MenuPage BuildFlagsPage(void);
MenuPage BuildEntitiesPage(void);
MenuPage BuildMemWatchPage(void);

void Push(MenuPage page) {
    sPageStack.push_back(std::move(page));
}

void Pop(void) {
    /* Deferred — see sPendingPops/sPendingClose. The actual stack mutation
     * happens after the calling lambda has returned. */
    if (static_cast<int>(sPageStack.size()) - sPendingPops <= 1) {
        sPendingClose = true;
    } else {
        ++sPendingPops;
    }
}

void RequestRebuild(void) {
    sPendingRebuild = true;
}

void ApplyPendingMutations(void) {
    if (sPendingClose) {
        sPendingClose = false;
        sPendingPops = 0;
        sPendingRebuild = false;
        sOpen = false;
        sPageStack.clear();
        return;
    }
    while (sPendingPops > 0 && !sPageStack.empty()) {
        sPageStack.pop_back();
        --sPendingPops;
    }
    sPendingPops = 0;
    /* Replace the (post-pop) top page with a freshly built copy, preserving the
     * cursor where the new item count allows. Copy the closure out first so the
     * assignment doesn't free the std::function whose body just ran. */
    if (sPendingRebuild) {
        sPendingRebuild = false;
        if (!sPageStack.empty() && sPageStack.back().rebuild) {
            const int savedCursor = sPageStack.back().cursor;
            const int savedTop = sPageStack.back().viewportTop;
            std::function<MenuPage()> fn = sPageStack.back().rebuild;
            sPageStack.back() = fn();
            MenuPage& pg = sPageStack.back();
            const int count = static_cast<int>(pg.items.size());
            pg.cursor = (savedCursor < count) ? savedCursor : (count > 0 ? count - 1 : 0);
            pg.viewportTop = (savedTop < count) ? savedTop : 0; /* renderer re-clamps */
        }
    }
}

void DoWarp(unsigned char area, unsigned char room, unsigned short x = 0x80, unsigned short y = 0x80,
            unsigned char layer = 0) {
    if (!Port_DebugAction_Warp(area, room, x, y, layer)) {
        Toast("Warp ignored: not in gameplay");
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Warp -> area 0x%02X room 0x%02X", area, room);
    Toast(buf);
    sOpen = false;
    sPageStack.clear();
}

MenuPage BuildItemsPage(void) {
    MenuPage p;
    p.title = "ITEMS";
    p.items.push_back({ "Unlock all items", []() {
                           Port_DebugAction_GiveAllItems();
                           Toast("All items granted");
                       } });
    p.items.push_back({ "Max heart containers", []() {
                           Port_DebugAction_MaxHearts();
                           Toast("Hearts maxed");
                       } });
    p.items.push_back({ "Heal to full", []() {
                           Port_DebugAction_HealFull();
                           Toast("Healed");
                       } });
    p.items.push_back({ "999 rupees", []() {
                           Port_DebugAction_MaxRupees();
                           Toast("999 rupees");
                       } });
    p.items.push_back({ "999 mysterious shells", []() {
                           Port_DebugAction_MaxShells();
                           Toast("999 shells");
                       } });
    p.items.push_back({ "All kinstones fused", []() {
                           Port_DebugAction_AllKinstones();
                           Toast("All kinstones");
                       } });
    p.items.push_back({ "All figurines (130)", []() {
                           Port_DebugAction_AllFigurines130();
                           Toast("All 130 figurines");
                       } });
    p.items.push_back({ "Figurines 100% (beaten)", []() {
                           Port_DebugAction_AllFigurines100();
                           Toast("136 figurines + game cleared");
                       } });
    p.items.push_back({ "Per-item toggle ->", []() { Push(BuildItemTogglePage()); } });
    p.items.push_back({ "Counts / capacities ->", []() { Push(BuildStatsPage()); } });
    p.items.push_back({ "Bottle contents ->", []() { Push(BuildBottlesPage()); } });
    p.items.push_back({ "Dungeon items ->", []() { Push(BuildDungeonItemsPage()); } });
    p.items.push_back({ "Charm / Picolyte ->", []() { Push(BuildBuffsPage()); } });
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Per-item ownership toggle list. Items come from the C layer pre-grouped;
 * the classic menu is a flat list so each row shows "[x]/[ ] Group: Name".
 * Enter flips ownership; SetToggleItem handles slot exclusivity + gfx. */
MenuPage BuildItemTogglePage(void) {
    MenuPage p;
    p.title = "PER-ITEM TOGGLE";
    const int count = Port_DebugQuery_ToggleItemCount();
    for (int i = 0; i < count; ++i) {
        MenuItem it;
        it.action = [i]() { Port_DebugAction_SetToggleItem(i, Port_DebugQuery_ToggleItemOwned(i) ? 0 : 1); };
        it.labelFn = [i]() -> std::string {
            const char* name = Port_DebugQuery_ToggleItemName(i);
            const char* grp = Port_DebugQuery_ToggleItemGroup(i);
            std::string s = Port_DebugQuery_ToggleItemOwned(i) ? "[x] " : "[ ] ";
            if (grp) {
                s += grp;
                s += ": ";
            }
            s += name ? name : "?";
            return s;
        };
        const char* nm = Port_DebugQuery_ToggleItemName(i);
        it.label = nm ? nm : "?";
        p.items.push_back(std::move(it));
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Any-dungeon Map/Compass/Big Key/Small Key editor. Left/Right on the first
 * row pick the dungeon id (0..15); the toggle rows and key counter act on
 * that selection, which the engine indexes by dungeon id directly. */
MenuPage BuildDungeonItemsPage(void) {
    MenuPage p;
    p.title = "DUNGEON ITEMS";

    MenuItem sel;
    sel.cycleLeft = []() { sClassicDungeonSel = (sClassicDungeonSel + 15) % 16; };
    sel.cycleRight = []() { sClassicDungeonSel = (sClassicDungeonSel + 1) % 16; };
    sel.labelFn = []() -> std::string {
        char buf[48];
        const int cur = Port_DebugQuery_CurrentDungeon();
        std::snprintf(buf, sizeof(buf), "Dungeon: %d%s   (<- ->)", sClassicDungeonSel,
                      (sClassicDungeonSel == cur) ? " (current)" : "");
        return buf;
    };
    sel.label = "Dungeon";
    p.items.push_back(std::move(sel));

    struct DBit {
        const char* name;
        int which;
        int bit;
    };
    static const DBit kBits[] = { { "Map", 0, 0x1 }, { "Compass", 1, 0x2 }, { "Big Key", 2, 0x4 } };
    for (const DBit& b : kBits) {
        const char* name = b.name;
        const int which = b.which, bit = b.bit;
        MenuItem it;
        it.action = [which, bit]() {
            const int bits = Port_DebugQuery_DungeonItems(sClassicDungeonSel);
            Port_DebugAction_SetDungeonItem(sClassicDungeonSel, which, (bits & bit) ? 0 : 1);
        };
        it.labelFn = [name, bit]() -> std::string {
            const bool on = (Port_DebugQuery_DungeonItems(sClassicDungeonSel) & bit) != 0;
            return std::string(on ? "[x] " : "[ ] ") + name;
        };
        it.label = name;
        p.items.push_back(std::move(it));
    }

    MenuItem keys;
    keys.cycleLeft = []() {
        const int k = Port_DebugQuery_DungeonKeys(sClassicDungeonSel);
        Port_DebugAction_SetDungeonKeys(sClassicDungeonSel, k > 0 ? k - 1 : 0);
    };
    keys.cycleRight = []() {
        Port_DebugAction_SetDungeonKeys(sClassicDungeonSel, Port_DebugQuery_DungeonKeys(sClassicDungeonSel) + 1);
    };
    keys.labelFn = []() -> std::string {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "Small keys: %d   (<- ->)", Port_DebugQuery_DungeonKeys(sClassicDungeonSel));
        return buf;
    };
    keys.label = "Small keys";
    p.items.push_back(std::move(keys));

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Charm + Picolyte activator. Left/Right pick the type; Enter on the Apply
 * row writes {type, normal-duration}. Full timer control lives in the ribbon;
 * the classic fallback applies the standard durations (charm 60s / pico 15s). */
MenuPage BuildBuffsPage(void) {
    static const char* const kCharm[] = { "Off", "Nayru", "Farore", "Din" };
    static const int kCharmId[] = { 0, BOTTLE_CHARM_NAYRU, BOTTLE_CHARM_FARORE, BOTTLE_CHARM_DIN };
    static const char* const kPico[] = { "Off", "Red", "Orange", "Yellow", "Green", "Blue", "White" };
    static const int kPicoId[] = { 0,
                                   ITEM_BOTTLE_PICOLYTE_RED,
                                   ITEM_BOTTLE_PICOLYTE_ORANGE,
                                   ITEM_BOTTLE_PICOLYTE_YELLOW,
                                   ITEM_BOTTLE_PICOLYTE_GREEN,
                                   ITEM_BOTTLE_PICOLYTE_BLUE,
                                   ITEM_BOTTLE_PICOLYTE_WHITE };

    MenuPage p;
    p.title = "CHARM / PICOLYTE";

    MenuItem ct;
    ct.cycleLeft = []() { sClassicCharmSel = (sClassicCharmSel + 3) % 4; };
    ct.cycleRight = []() { sClassicCharmSel = (sClassicCharmSel + 1) % 4; };
    ct.labelFn = []() -> std::string { return std::string("Charm type: ") + kCharm[sClassicCharmSel]; };
    ct.label = "Charm type";
    p.items.push_back(std::move(ct));

    p.items.push_back({ "Apply charm (60s)", []() {
                           Port_DebugAction_SetCharm(kCharmId[sClassicCharmSel], sClassicCharmSel == 0 ? 0 : 3600);
                           Toast(sClassicCharmSel == 0 ? "Charm cleared" : "Charm applied");
                       } });

    MenuItem pt;
    pt.cycleLeft = []() { sClassicPicoSel = (sClassicPicoSel + 6) % 7; };
    pt.cycleRight = []() { sClassicPicoSel = (sClassicPicoSel + 1) % 7; };
    pt.labelFn = []() -> std::string { return std::string("Picolyte type: ") + kPico[sClassicPicoSel]; };
    pt.label = "Picolyte type";
    p.items.push_back(std::move(pt));

    p.items.push_back({ "Apply picolyte (15s)", []() {
                           Port_DebugAction_SetPicolyte(kPicoId[sClassicPicoSel], sClassicPicoSel == 0 ? 0 : 900);
                           Toast(sClassicPicoSel == 0 ? "Picolyte cleared" : "Picolyte applied");
                       } });

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Numeric count / capacity editor. Left/Right step each stat; the step scales
 * with the range so wide stats (rupees) move usefully. Bounds + clamping live
 * in the C layer's Set/Query, so this page just drives them. */
MenuPage BuildStatsPage(void) {
    MenuPage p;
    p.title = "STATS / COUNTS";
    const int count = Port_DebugQuery_StatCount();
    for (int i = 0; i < count; ++i) {
        auto stepOf = [](int idx) {
            const int span = Port_DebugQuery_StatMax(idx) - Port_DebugQuery_StatMin(idx);
            return span > 64 ? span / 64 : 1;
        };
        MenuItem it;
        it.cycleLeft = [i, stepOf]() { Port_DebugAction_SetStat(i, Port_DebugQuery_StatValue(i) - stepOf(i)); };
        it.cycleRight = [i, stepOf]() { Port_DebugAction_SetStat(i, Port_DebugQuery_StatValue(i) + stepOf(i)); };
        it.labelFn = [i]() -> std::string {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s: %d / %d   (<- ->)", Port_DebugQuery_StatName(i),
                          Port_DebugQuery_StatValue(i), Port_DebugQuery_StatMax(i));
            return buf;
        };
        it.label = Port_DebugQuery_StatName(i);
        p.items.push_back(std::move(it));
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Per-bottle content picker. Left/Right cycle the content list; selecting a
 * content also grants the bottle ("[grant]" marks a bottle not yet owned). */
MenuPage BuildBottlesPage(void) {
    MenuPage p;
    p.title = "BOTTLE CONTENTS";
    const int n = Port_DebugQuery_BottleContentCount();
    for (int b = 0; b < 4; ++b) {
        MenuItem it;
        it.cycleLeft = [b, n]() {
            const int idx = Port_DebugQuery_BottleContentIndex(Port_DebugQuery_BottleContent(b));
            Port_DebugAction_SetBottleContent(b, Port_DebugQuery_BottleContentId((idx + n - 1) % n));
        };
        it.cycleRight = [b, n]() {
            const int idx = Port_DebugQuery_BottleContentIndex(Port_DebugQuery_BottleContent(b));
            Port_DebugAction_SetBottleContent(b, Port_DebugQuery_BottleContentId((idx + 1) % n));
        };
        it.labelFn = [b]() -> std::string {
            char buf[56];
            const int idx = Port_DebugQuery_BottleContentIndex(Port_DebugQuery_BottleContent(b));
            const bool owned = Port_DebugQuery_BottleOwned(b) != 0;
            std::snprintf(buf, sizeof(buf), "Bottle %d: %s%s   (<- ->)", b + 1, Port_DebugQuery_BottleContentName(idx),
                          owned ? "" : " [grant]");
            return buf;
        };
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "Bottle %d", b + 1);
        it.label = lbl;
        p.items.push_back(std::move(it));
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Raw flag browser (wishlist #5). Left/Right pick the bank and the index;
 * Enter on the toggle row flips the selected flag. The ribbon "Flags" tab has
 * the full scrollable grid; this is the compact bank+index fallback. */
MenuPage BuildFlagsPage(void) {
    MenuPage p;
    p.title = "FLAG BROWSER";
    const int nBanks = Port_DebugQuery_FlagBankCount();

    MenuItem bk;
    bk.cycleLeft = [nBanks]() {
        sClassicFlagBank = (sClassicFlagBank + nBanks - 1) % nBanks;
        if (sClassicFlagIndex >= Port_DebugQuery_FlagBankSize(sClassicFlagBank))
            sClassicFlagIndex = 0;
    };
    bk.cycleRight = [nBanks]() {
        sClassicFlagBank = (sClassicFlagBank + 1) % nBanks;
        if (sClassicFlagIndex >= Port_DebugQuery_FlagBankSize(sClassicFlagBank))
            sClassicFlagIndex = 0;
    };
    bk.labelFn = []() -> std::string {
        const int cur = Port_DebugQuery_CurrentFlagBank();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%s   (<- ->)", Port_DebugQuery_FlagBankName(sClassicFlagBank),
                      (sClassicFlagBank == cur) ? " *current area*" : "");
        return buf;
    };
    bk.label = "Bank";
    p.items.push_back(std::move(bk));

    MenuItem ix;
    ix.cycleLeft = []() {
        const int sz = Port_DebugQuery_FlagBankSize(sClassicFlagBank);
        if (sz > 0)
            sClassicFlagIndex = (sClassicFlagIndex + sz - 1) % sz;
    };
    ix.cycleRight = []() {
        const int sz = Port_DebugQuery_FlagBankSize(sClassicFlagBank);
        if (sz > 0)
            sClassicFlagIndex = (sClassicFlagIndex + 1) % sz;
    };
    ix.labelFn = []() -> std::string {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Index: %d / %d   (<- ->)", sClassicFlagIndex,
                      Port_DebugQuery_FlagBankSize(sClassicFlagBank) - 1);
        return buf;
    };
    ix.label = "Index";
    p.items.push_back(std::move(ix));

    MenuItem tg;
    tg.action = []() {
        const int on = Port_DebugQuery_Flag(sClassicFlagBank, sClassicFlagIndex);
        Port_DebugAction_SetFlag(sClassicFlagBank, sClassicFlagIndex, on ? 0 : 1);
    };
    tg.labelFn = []() -> std::string {
        const bool on = Port_DebugQuery_Flag(sClassicFlagBank, sClassicFlagIndex) != 0;
        return std::string("Flag = ") + (on ? "ON" : "off") + "   [Enter toggles]";
    };
    tg.label = "Toggle flag";
    p.items.push_back(std::move(tg));

    p.items.push_back({ "Jump to current area bank", []() {
                           const int cur = Port_DebugQuery_CurrentFlagBank();
                           if (cur >= 0) {
                               sClassicFlagBank = cur;
                               if (sClassicFlagIndex >= Port_DebugQuery_FlagBankSize(cur))
                                   sClassicFlagIndex = 0;
                           }
                       } });

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

MenuPage BuildWarpPage(void) {
    MenuPage p;
    p.title = "WARP";
    /* Dungeon entries are lifted verbatim from src/data/screenTransitions.c
     * (the Wallmaster screen-transitions table, gWallMasterScreenTransitions)
     * — area, room, endX, endY, layer — so the warp goes through DoExitTransition
     * exactly the way a wallmaster pickup does. Layer=1 across all dungeons. */
    p.items.push_back({ "Hyrule Town", []() { DoWarp(AREA_HYRULE_TOWN, 0x00, 0x80, 0xC0, 1); } });
    /* #65 fix: Link's house lives in SOUTH_HYRULE_FIELD (room 0x01),
     * not Western_Woods_South (room 0x00). Local coords come from the
     * exit list in src/data/transitions.c (gExitList_HouseInteriors2_-
     * LinksHouseEntrance: WARP_TYPE_BORDER -> 0x290, 0x19c). */
    p.items.push_back({ "Hyrule Field - Link's house", []() { DoWarp(AREA_HYRULE_FIELD, 0x01, 0x290, 0x19C, 1); } });
    p.items.push_back({ "Minish Woods", []() { DoWarp(AREA_MINISH_WOODS, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Minish Village", []() { DoWarp(AREA_MINISH_VILLAGE, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Mt Crenel", []() { DoWarp(AREA_MT_CRENEL, 0x00, 0x80, 0xC0, 1); } });
    /* Spawn at Mountain Minish 4's coordinates from gUnk_additional_9 — a
     * known-walkable spot near the room's left side (#42/#43 repro). */
    p.items.push_back({ "Melari's Mines", []() { DoWarp(AREA_MELARIS_MINE, 0x00, 0x80, 0x130, 1); } });
    p.items.push_back({ "Deepwood Shrine", []() { DoWarp(AREA_DEEPWOOD_SHRINE, 0x0B, 0xa8, 0xb8, 1); } });
    /* Boss-room coords match the canonical entry transitions in
     * src/data/transitions.c / src/manager/holeManager.c rather than the
     * placeholder (0x80, 0x80) that left Link off-camera or invisible.
     * Layer matches what the room map expects (CoF boss is a hole drop
     * onto layer 2). */
    p.items.push_back({ "Deepwood Shrine - boss", []() { DoWarp(AREA_DEEPWOOD_SHRINE_BOSS, 0x00, 0x88, 0xD8, 1); } });
    p.items.push_back({ "Cave of Flames", []() { DoWarp(AREA_CAVE_OF_FLAMES, 0x04, 0x98, 0xa8, 1); } });
    /* Room 0x08 = Rollobite lava room (#36 — moving lava platforms).
     * Local coords come from the captured world position (610, 3578) minus the
     * room origin (336, 3200) recorded in area_room_headers.json. */
    p.items.push_back({ "Cave of Flames - Rollobite", []() { DoWarp(AREA_CAVE_OF_FLAMES, 0x08, 0x112, 0x17A, 1); } });
    p.items.push_back({ "Cave of Flames - boss", []() { DoWarp(AREA_CAVE_OF_FLAMES_BOSS, 0x00, 0xC0, 0xF8, 2); } });
    p.items.push_back({ "Fortress of Winds", []() { DoWarp(AREA_FORTRESS_OF_WINDS, 0x21, 0x78, 0xa8, 1); } });
    p.items.push_back({ "Temple of Droplets", []() { DoWarp(AREA_TEMPLE_OF_DROPLETS, 0x03, 0x108, 0xf8, 1); } });
    p.items.push_back({ "Royal Crypt", []() { DoWarp(AREA_ROYAL_CRYPT, 0x08, 0x88, 0x78, 1); } });
    p.items.push_back({ "Palace of Winds", []() { DoWarp(AREA_PALACE_OF_WINDS, 0x31, 0x238, 0x58, 1); } });
    /* #58 repro: bakery rafters at the reporter's exact spot. World pos
     * (1864, 117); room 3 origin map_x=0x60 << 4 = 0x600 → local (0x148, 0x75).
     * Area + room constants hardcoded — not yet mirrored above. */
    p.items.push_back({ "MinishRafters Bakery (#58 repro)", []() { DoWarp(0x2E, 0x03, 0x148, 0x75, 1); } });
    /* #57 repro: Carlov's figurine shop. Area 0x23 = HouseInteriors3,
     * room 7 = Carlov, room header (0x00, 0x0E, 0xF0, 0xA0) → local centre
     * (0x78, 0x50). Walk into the device + insert shells to draw. */
    p.items.push_back({ "Carlov figurine shop (#57 repro)", []() { DoWarp(0x23, 0x07, 0x78, 0x50, 1); } });
    /* #101 repro: Scissors Beetle crash room in Temple of Droplets.
     * World pos (1650, 3335) - room origin (1568, 3280) = local (82, 55). */
    p.items.push_back(
        { "ToD Scissors Beetle (#101 repro)", []() { DoWarp(AREA_TEMPLE_OF_DROPLETS, 0x33, 0x52, 0x37, 1); } });
    /* #78 repro: Wind Ruins / Fortress-of-Winds approach left wizard room.
     * World pos (520, 5379) - room origin (352, 5312) = local (168, 67). */
    p.items.push_back(
        { "WindRuins Wizards (#78 repro)", []() { DoWarp(AREA_FORTRESS_OF_WINDS, 0x23, 0xA8, 0x43, 1); } });
    /* OBJ-window (mode-2) render test — dark rooms whose light circles are
     * litArea OBJ-window sprites (src/object/litArea.c). Spawn point sits on a
     * litArea coordinate (always-on, no flag gate) so the light reveal is in
     * view immediately. Correct render: dark room + soft circular light, no
     * floating opaque blob. Stockwell = MinishRafters room 0x01 (two candle
     * circles); Madderpillars = ToD room 0x2C (two circles). */
    p.items.push_back({ "OBJWIN test: Stockwell shop (litArea)", []() { DoWarp(0x2E, 0x01, 0x64, 0x78, 1); } });
    p.items.push_back(
        { "OBJWIN test: ToD Madderpillars (litArea)", []() { DoWarp(AREA_TEMPLE_OF_DROPLETS, 0x2C, 0xBC, 0x58, 1); } });
    p.items.push_back({ "All areas (raw, by index) ->", []() { Push(BuildAllAreasPage()); } });
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Iterate every area slot and add an entry per area that has at least one
 * mapped room. The room headers come from the asset pipeline, so areas
 * with no extracted data (NULL_xx slots in include/area.h) won't appear.
 * Selecting an area pushes a per-area submenu listing its rooms. */
MenuPage BuildAllAreasPage(void) {
    MenuPage p;
    p.title = "WARP - all areas";
    for (unsigned int area = 0; area < 0x90; ++area) {
        unsigned char a = static_cast<unsigned char>(area);
        int count = Port_DebugQuery_AreaRoomCount(a);
        if (count <= 0) {
            continue;
        }
        const char* name = Port_DebugQuery_AreaName(a);
        /* Match the ribbon UI: skip areas that aren't warpable
         * (unnamed slots + known-broken named areas). */
        if (!Port_DebugAction_AreaIsWarpable(a))
            continue;
        char buf[80];
        std::snprintf(buf, sizeof(buf), "0x%02X %s (%d)", area, name, count);
        p.items.push_back({ buf, [a]() { Push(BuildAreaRoomsPage(a)); } });
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Per-area room list. Each entry warps to the room with x = pixel_width/2,
 * y = pixel_height/2 — geometric centre. Not guaranteed walkable (could
 * spawn inside an obstacle) but good enough for debug; if you land on a
 * wall, just F8 → warp again to a different room. Layer defaults to 1. */
MenuPage BuildAreaRoomsPage(unsigned char area) {
    MenuPage p;
    char title[48];
    std::snprintf(title, sizeof(title), "WARP - area 0x%02X rooms", area);
    p.title = title;
    int count = Port_DebugQuery_AreaRoomCount(area);
    for (int r = 0; r < count; ++r) {
        unsigned short w = 0, h = 0;
        if (!Port_DebugQuery_RoomDimensions(area, static_cast<unsigned char>(r), &w, &h)) {
            continue;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Room 0x%02X (%ux%u px)", r, w, h);
        unsigned char rr = static_cast<unsigned char>(r);
        unsigned short cx = static_cast<unsigned short>(w / 2);
        unsigned short cy = static_cast<unsigned short>(h / 2);
        p.items.push_back({ buf, [area, rr, cx, cy]() { DoWarp(area, rr, cx, cy, 1); } });
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

MenuPage BuildDisplaySettingsPage(void) {
    /* Mirrors the file-select "L Settings" panel (src/fileselect.c
     * HandlePortSettingsMenu): same four knobs, same Left/Right ergonomics,
     * but reachable mid-game via F8 instead of only on the title screen.
     * Each item has a labelFn that re-reads the current value every frame
     * so the row updates immediately as you cycle. */
    MenuPage p;
    p.title = "DISPLAY SETTINGS";

    MenuItem scale;
    scale.cycleLeft = []() { Port_PPU_CycleWindowScale(-1); };
    scale.cycleRight = []() { Port_PPU_CycleWindowScale(+1); };
    scale.labelFn = []() {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Scale       %ux", (unsigned)Port_PPU_WindowScale());
        return std::string(buf);
    };
    p.items.push_back(std::move(scale));

    MenuItem filter;
    filter.cycleLeft = []() { Port_PPU_CyclePresentationMode(-1); };
    filter.cycleRight = []() { Port_PPU_CyclePresentationMode(+1); };
    filter.labelFn = []() {
        const char* name = Port_PPU_PresentationModeName();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Upscale     %s", name ? name : "?");
        return std::string(buf);
    };
    p.items.push_back(std::move(filter));

    MenuItem crtFilter;
    crtFilter.cycleLeft = []() { Port_PPU_CycleFilter(-1); };
    crtFilter.cycleRight = []() { Port_PPU_CycleFilter(+1); };
    crtFilter.labelFn = []() {
        const char* name = Port_PPU_FilterName();
        char buf[80];
        std::snprintf(buf, sizeof(buf), "CRT filter  %s", name ? name : "?");
        return std::string(buf);
    };
    p.items.push_back(std::move(crtFilter));

    MenuItem fps;
    fps.cycleLeft = []() { Port_Config_CycleTargetFps(-1); };
    fps.cycleRight = []() { Port_Config_CycleTargetFps(+1); };
    fps.labelFn = []() {
        unsigned int v = Port_Config_TargetFps();
        char buf[32];
        if (v == 0) {
            std::snprintf(buf, sizeof(buf), "FPS         uncapped");
        } else {
            std::snprintf(buf, sizeof(buf), "FPS         %u", v);
        }
        return std::string(buf);
    };
    p.items.push_back(std::move(fps));

    MenuItem fs;
    /* Fullscreen is binary, so left/right both toggle. */
    fs.cycleLeft = []() { Port_PPU_ToggleFullscreen(); };
    fs.cycleRight = []() { Port_PPU_ToggleFullscreen(); };
    fs.labelFn = []() {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Fullscreen  %s", Port_PPU_IsFullscreen() ? "on" : "off");
        return std::string(buf);
    };
    p.items.push_back(std::move(fs));

    MenuItem internalScale;
    internalScale.cycleLeft = []() { Port_Config_CycleInternalScale(-1); };
    internalScale.cycleRight = []() { Port_Config_CycleInternalScale(+1); };
    internalScale.labelFn = []() {
        char buf[64];
        unsigned s = (unsigned)Port_Config_InternalScale();
        /* Affine OAM is sub-pixel at scale > 1; everything else is S*S
         * replicate. Affine BG2 / mode 7 are still TODO. */
        std::snprintf(buf, sizeof(buf), s == 1 ? "Internal    %ux  (off)" : "Internal    %ux  (affine OBJ sub-pixel)",
                      s);
        return std::string(buf);
    };
    p.items.push_back(std::move(internalScale));

    MenuItem widescreen;
    widescreen.cycleLeft = []() {
        Port_Config_SetWidescreenEnabled(!Port_Config_WidescreenEnabled());
        Port_PPU_ApplyWindowScale();
    };
    widescreen.cycleRight = []() {
        Port_Config_SetWidescreenEnabled(!Port_Config_WidescreenEnabled());
        Port_PPU_ApplyWindowScale();
    };
    widescreen.labelFn = []() {
        char buf[64];
#if defined(MODE1_GBA_WIDTH) && (MODE1_GBA_WIDTH > 240)
        std::snprintf(buf, sizeof(buf), "Widescreen  %s (WIP)", Port_Config_WidescreenEnabled() ? "on" : "off");
#else
        std::snprintf(buf, sizeof(buf), "Widescreen  unavailable");
#endif
        return std::string(buf);
    };
    p.items.push_back(std::move(widescreen));

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Soft-slot assignment page. Each row is a cycle item: Left/Right walks
 * through the items the player owns. The label is regenerated every frame
 * via labelFn, so the displayed assignment updates immediately on cycle. */
MenuPage BuildSoftSlotsPage(void) {
    MenuPage p;
    p.title = "EXTRA EQUIP SLOTS";
    for (int s = 0; s < 4; ++s) {
        MenuItem it;
        it.cycleLeft = [s]() { Port_SoftSlots_CycleAssignment(s, -1); };
        it.cycleRight = [s]() { Port_SoftSlots_CycleAssignment(s, +1); };
        it.labelFn = [s]() { return std::string(Port_SoftSlots_GetSlotLabel(s)); };
        p.items.push_back(std::move(it));
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Save-state slot management. One row per slot (0 = quicksave, 1-4 =
 * manual numbered, 5-7 = auto-save ring), each labeled with its
 * populated/empty state and saved-at time. Action = load (with toast),
 * cycleLeft = save (so Left arrow re-saves the slot from the same UI). */
static std::string FormatSlotLabel(int slot) {
    char buf[80];
    const char* tag;
    if (slot == 0) {
        tag = "Quick";
    } else if (slot < Port_QuickSave_AutoSlotBase()) {
        static char tagbuf[8];
        std::snprintf(tagbuf, sizeof(tagbuf), "Slot %d", slot);
        tag = tagbuf;
    } else {
        static char tagbuf[16];
        std::snprintf(tagbuf, sizeof(tagbuf), "Auto %d ", slot - Port_QuickSave_AutoSlotBase() + 1);
        tag = tagbuf;
    }
    if (!Port_QuickSave_HasSlot(slot)) {
        std::snprintf(buf, sizeof(buf), "%-7s (empty)", tag);
        return std::string(buf);
    }
    unsigned long long t = Port_QuickSave_SlotTimestamp(slot);
    time_t tt = (time_t)t;
    char timestr[32] = "?";
    if (t != 0) {
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &tt);
#else
        localtime_r(&tt, &tm_buf);
#endif
        std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm_buf);
    }
    std::snprintf(buf, sizeof(buf), "%-7s %s", tag, timestr);
    return std::string(buf);
}

MenuPage BuildSaveStatesPage(void) {
    MenuPage p;
    p.title = "SAVE STATES (Enter=load, Left=save)";
    const int n = Port_QuickSave_SlotCount();
    for (int s = 0; s < n; ++s) {
        MenuItem it;
        it.action = [s]() {
            if (Port_QuickSave_LoadSlot(s))
                Toast("Loaded");
            else
                Toast("Slot empty");
        };
        it.cycleLeft = [s]() {
            if (Port_QuickSave_SaveSlot(s))
                Toast("Saved");
        };
        it.cycleRight = [s]() {
            if (Port_QuickSave_SaveSlot(s))
                Toast("Saved");
        };
        it.labelFn = [s]() { return FormatSlotLabel(s); };
        p.items.push_back(std::move(it));
    }

    auto flipAuto = []() {
        bool next = !Port_QuickSave_AutoEnabled();
        Port_QuickSave_SetAutoEnabled(next ? 1 : 0);
        Port_Config_SetAutosaveEnabled(next);
    };
    MenuItem autoToggle;
    autoToggle.cycleLeft = flipAuto;
    autoToggle.cycleRight = flipAuto;
    autoToggle.action = flipAuto;
    autoToggle.labelFn = []() {
        char b[64];
        std::snprintf(b, sizeof(b), "Auto-save   %s", Port_QuickSave_AutoEnabled() ? "on" : "off");
        return std::string(b);
    };
    p.items.push_back(std::move(autoToggle));

    MenuItem autoInterval;
    autoInterval.cycleLeft = []() {
        unsigned int cur = Port_QuickSave_AutoIntervalMs();
        unsigned int next = cur > 15000 ? cur - 15000 : 15000;
        Port_QuickSave_SetAutoIntervalMs(next);
        Port_Config_SetAutosaveIntervalMs(next);
    };
    autoInterval.cycleRight = []() {
        unsigned int cur = Port_QuickSave_AutoIntervalMs();
        unsigned int next = cur + 15000;
        Port_QuickSave_SetAutoIntervalMs(next);
        Port_Config_SetAutosaveIntervalMs(next);
    };
    autoInterval.labelFn = []() {
        char b[64];
        unsigned int ms = Port_QuickSave_AutoIntervalMs();
        std::snprintf(b, sizeof(b), "Interval    %us", ms / 1000u);
        return std::string(b);
    };
    p.items.push_back(std::move(autoInterval));

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Save-profile picker. Each row is one `tmc[_*].sav` file discovered in
 * the working directory; activating it switches future EEPROM reads/writes
 * to that file. Includes a "+ New profile from current" row that copies
 * the in-memory EEPROM to the next available `tmc_NN.sav`. */
MenuPage BuildSaveProfilesPage(void) {
    MenuPage p;
    p.title = "SAVE PROFILES";

    /* Discover available .sav files. 32-entry cap is enough — even a
     * dedicated speedrun setup is unlikely to keep that many runs. */
    char names[32][64];
    const int n = Port_Save_ListProfiles(names, 32);
    const std::string activeNow = Port_Save_GetActivePath();

    for (int i = 0; i < n; ++i) {
        std::string name = names[i];
        bool isActive = (name == activeNow);
        MenuItem it;
        it.action = [name]() {
            Port_Save_SetActivePath(name.c_str());
            Port_Config_SetActiveSaveProfile(name.c_str());
            Toast(("Active: " + name + " — go to title to load").c_str());
        };
        /* labelFn so the active marker updates after switching without
         * forcing a page rebuild. */
        it.labelFn = [name]() {
            std::string active = Port_Save_GetActivePath();
            char buf[80];
            std::snprintf(buf, sizeof(buf), "%s %s", (name == active) ? "*" : " ", name.c_str());
            return std::string(buf);
        };
        p.items.push_back(std::move(it));
        (void)isActive;
    }

    if (n == 0) {
        MenuItem none;
        none.label = "(no .sav files found in cwd)";
        none.action = []() {};
        p.items.push_back(std::move(none));
    }

    /* "Save current as new profile" — copies the in-memory EEPROM to
     * the next available tmc_NN.sav. Doesn't switch active. */
    MenuItem newProfile;
    newProfile.action = []() {
        /* Find the next free tmc_<n>.sav. Linear probe up to 99. */
        char name[64];
        int n = 1;
        for (; n <= 99; ++n) {
            std::snprintf(name, sizeof(name), "tmc_%d.sav", n);
            FILE* probe = std::fopen(name, "rb");
            if (!probe)
                break;
            std::fclose(probe);
        }
        if (n > 99) {
            Toast("No free profile slots (1-99)");
            return;
        }
        if (Port_Save_SaveAsProfile(name)) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "Saved current as %s", name);
            Toast(msg);
        } else {
            Toast("Save failed");
        }
    };
    newProfile.labelFn = []() { return std::string("+ Save current as new profile"); };
    p.items.push_back(std::move(newProfile));

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Read-only entity viewer. Snapshot taken when the page is built; "Refresh"
 * re-snapshots via the deferred rebuild (RequestRebuild + page.rebuild), since
 * Pop()+Push() would let the deferred pop discard the just-pushed page.
 * Rows are non-actionable. */
MenuPage BuildEntitiesPage(void) {
    MenuPage p;
    p.title = "ENTITIES";
    const int n = Port_DebugQuery_RefreshEntities();

    MenuItem hdr;
    hdr.label = "Refresh";
    hdr.labelFn = []() -> std::string {
        char b[48];
        std::snprintf(b, sizeof(b), "Live: %d  (select to refresh)", Port_DebugQuery_EntitySnapshotCount());
        return std::string(b);
    };
    hdr.action = []() { RequestRebuild(); };
    p.rebuild = []() { return BuildEntitiesPage(); };
    p.items.push_back(std::move(hdr));

    for (int i = 0; i < n; ++i) {
        const PortEntityInfo* e = Port_DebugQuery_Entity(i);
        if (!e)
            continue;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "L%d %s id%02X t%02X (%d,%d) hp%u", e->listIndex,
                      Port_DebugQuery_EntityKindName(e->kind), (unsigned)e->id, (unsigned)e->type, e->x, e->y,
                      (unsigned)e->health);
        p.items.push_back({ std::string(buf), []() {} });
    }

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Live memory-watch list. The top rows compose a candidate (address / step /
 * width) and "Add watch" appends it; below, each stored watch shows its live
 * value (labelFn re-reads every frame via the fault-safe Port_DebugQuery_MemRead)
 * and Enter removes it. The ribbon "Memory" tab has the hex-input equivalent;
 * this is the cycle-based fallback for the classic menu. */
MenuPage BuildMemWatchPage(void) {
    MenuPage p;
    p.title = "MEMORY WATCH";
    p.rebuild = []() { return BuildMemWatchPage(); }; /* add/remove rebuild this page in place */

    MenuItem ad;
    ad.cycleLeft = []() { sClassicMemAddr -= (1u << (4 * sClassicMemStepIdx)); };
    ad.cycleRight = []() { sClassicMemAddr += (1u << (4 * sClassicMemStepIdx)); };
    ad.labelFn = []() -> std::string {
        unsigned int v = 0;
        const int ok = Port_DebugQuery_MemRead(sClassicMemAddr, sClassicMemWidth, &v);
        const char* wn = Port_DebugQuery_MemWidthName(sClassicMemWidth);
        char buf[80];
        if (ok) {
            std::snprintf(buf, sizeof(buf), "Addr 0x%08X = 0x%0*X (%s)   (<- ->)", sClassicMemAddr,
                          (1 << sClassicMemWidth) * 2, v, wn);
        } else {
            std::snprintf(buf, sizeof(buf), "Addr 0x%08X = <unmapped> (%s)   (<- ->)", sClassicMemAddr, wn);
        }
        return buf;
    };
    ad.label = "Address";
    p.items.push_back(std::move(ad));

    MenuItem st;
    st.cycleLeft = []() { sClassicMemStepIdx = (sClassicMemStepIdx + 7) % 8; };
    st.cycleRight = []() { sClassicMemStepIdx = (sClassicMemStepIdx + 1) % 8; };
    st.labelFn = []() -> std::string {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "Step: 0x%X   (<- ->)", (unsigned)(1u << (4 * sClassicMemStepIdx)));
        return buf;
    };
    st.label = "Step";
    p.items.push_back(std::move(st));

    MenuItem wd;
    wd.cycleLeft = []() { sClassicMemWidth = (sClassicMemWidth + 2) % 3; };
    wd.cycleRight = []() { sClassicMemWidth = (sClassicMemWidth + 1) % 3; };
    wd.labelFn = []() -> std::string {
        return std::string("Width: ") + Port_DebugQuery_MemWidthName(sClassicMemWidth) + "   (<- ->)";
    };
    wd.label = "Width";
    p.items.push_back(std::move(wd));

    p.items.push_back({ "Add watch", []() {
                           const int idx = Port_DebugAction_MemWatchAdd(sClassicMemAddr, sClassicMemWidth);
                           Toast(idx >= 0 ? "Watch added" : "Watch list full (32 max)");
                           RequestRebuild();
                       } });

    const int n = Port_DebugQuery_MemWatchCount();
    if (n > 0) {
        p.items.push_back({ "Clear all watches", []() {
                               Port_DebugAction_MemWatchClear();
                               Toast("Watches cleared");
                               RequestRebuild();
                           } });
    }
    for (int i = 0; i < n; ++i) {
        MenuItem it;
        it.labelFn = [i]() -> std::string {
            const unsigned int a = Port_DebugQuery_MemWatchAddr(i);
            const int w = Port_DebugQuery_MemWatchWidth(i);
            unsigned int v = 0;
            const int ok = Port_DebugQuery_MemRead(a, w, &v);
            const char* wn = Port_DebugQuery_MemWidthName(w);
            char buf[80];
            if (ok) {
                std::snprintf(buf, sizeof(buf), "0x%08X (%s) = 0x%0*X   [Enter removes]", a, wn, (1 << w) * 2, v);
            } else {
                std::snprintf(buf, sizeof(buf), "0x%08X (%s) = <unmapped>   [Enter removes]", a, wn);
            }
            return buf;
        };
        it.action = [i]() {
            Port_DebugAction_MemWatchRemove(i);
            RequestRebuild();
        };
        it.label = "watch";
        p.items.push_back(std::move(it));
    }

    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

MenuPage BuildMainPage(void) {
    MenuPage p;
    p.title = "DEBUG MENU (F8 to close)";
    p.items.push_back({ "Items / progress", []() { Push(BuildItemsPage()); } });
    p.items.push_back({ "Warp", []() { Push(BuildWarpPage()); } });
    p.items.push_back({ "Save states", []() { Push(BuildSaveStatesPage()); } });
    p.items.push_back({ "Save profiles", []() { Push(BuildSaveProfilesPage()); } });
    p.items.push_back({ "Display settings", []() { Push(BuildDisplaySettingsPage()); } });
    p.items.push_back({ "Extra equip slots", []() { Push(BuildSoftSlotsPage()); } });
    p.items.push_back({ "Flag browser", []() { Push(BuildFlagsPage()); } });
    p.items.push_back({ "Entity viewer", []() { Push(BuildEntitiesPage()); } });
    p.items.push_back({ "Memory watch", []() { Push(BuildMemWatchPage()); } });
    p.items.push_back({ "Heal to full", []() {
                           Port_DebugAction_HealFull();
                           Toast("Healed");
                       } });
    p.items.push_back({ "Close menu", []() { Pop(); } });
    return p;
}

} /* namespace */

/* ============================================================ */
/*                          Public API                          */
/* ============================================================ */

/* ------------------------------------------------------------------ */
/*   Accessors used by port_imgui_menu.cpp                            */
/* ------------------------------------------------------------------ */
/* These let the ImGui renderer walk the existing page stack without
 * forking the state machine — Enter / Left / Right from ImGui call
 * back into the same action lambdas the legacy key handler invokes,
 * so behaviour stays identical regardless of which renderer is on. */
extern "C" int Port_DebugMenu_PageDepth(void) {
    return static_cast<int>(sPageStack.size());
}
extern "C" const char* Port_DebugMenu_PageTitle(int depth) {
    if (depth < 0 || depth >= (int)sPageStack.size())
        return nullptr;
    return sPageStack[depth].title.c_str();
}
extern "C" int Port_DebugMenu_PageItemCount(int depth) {
    if (depth < 0 || depth >= (int)sPageStack.size())
        return 0;
    return static_cast<int>(sPageStack[depth].items.size());
}
extern "C" const char* Port_DebugMenu_PageItemLabel(int depth, int idx) {
    if (depth < 0 || depth >= (int)sPageStack.size())
        return nullptr;
    const auto& page = sPageStack[depth];
    if (idx < 0 || idx >= (int)page.items.size())
        return nullptr;
    /* labelFn callbacks reconstruct a fresh string each frame; we
     * stash the result in a per-thread static so the c_str() pointer
     * stays valid until the next call. ImGui consumes the pointer
     * synchronously inside DrawMenuPage so this is safe. */
    static thread_local std::string sLabelCache;
    const auto& it = page.items[idx];
    sLabelCache = it.labelFn ? it.labelFn() : it.label;
    return sLabelCache.c_str();
}
extern "C" int Port_DebugMenu_PageCursor(int depth) {
    if (depth < 0 || depth >= (int)sPageStack.size())
        return -1;
    return sPageStack[depth].cursor;
}
extern "C" void Port_DebugMenu_PageSetCursor(int depth, int idx) {
    if (depth < 0 || depth >= (int)sPageStack.size())
        return;
    auto& page = sPageStack[depth];
    if (idx < 0 || idx >= (int)page.items.size())
        return;
    page.cursor = idx;
}
extern "C" void Port_DebugMenu_PageActivate(int depth, int idx) {
    if (depth < 0 || depth >= (int)sPageStack.size())
        return;
    auto& page = sPageStack[depth];
    if (idx < 0 || idx >= (int)page.items.size())
        return;
    /* Mirror Enter-handling from the legacy key path: prefer .action,
     * fall back to .cycleRight for value-style items. */
    auto& item = page.items[idx];
    if (item.action)
        item.action();
    else if (item.cycleRight)
        item.cycleRight();
    ApplyPendingMutations();
}
extern "C" void Port_DebugMenu_PageCycleLeft(int depth, int idx) {
    if (depth < 0 || depth >= (int)sPageStack.size())
        return;
    auto& page = sPageStack[depth];
    if (idx < 0 || idx >= (int)page.items.size())
        return;
    auto& item = page.items[idx];
    if (item.cycleLeft)
        item.cycleLeft();
    ApplyPendingMutations();
}
extern "C" void Port_DebugMenu_PageCycleRight(int depth, int idx) {
    if (depth < 0 || depth >= (int)sPageStack.size())
        return;
    auto& page = sPageStack[depth];
    if (idx < 0 || idx >= (int)page.items.size())
        return;
    auto& item = page.items[idx];
    if (item.cycleRight)
        item.cycleRight();
    ApplyPendingMutations();
}
extern "C" const char* Port_DebugMenu_Toast(void) {
    if (sToast.empty() || SDL_GetTicks() >= sToastUntilTicks)
        return nullptr;
    return sToast.c_str();
}

extern "C" void Port_DebugMenu_ToastFromExternal(const char* msg) {
    if (!msg)
        return;
    Toast(msg);
}

extern "C" void Port_DebugMenu_Toggle(void) {
    if (sOpen) {
        sOpen = false;
        sPageStack.clear();
    } else {
        sOpen = true;
        sPageStack.clear();
        sPageStack.push_back(BuildMainPage());
    }
}

extern "C" bool Port_DebugMenu_IsOpen(void) {
    return sOpen;
}

extern "C" bool Port_DebugMenu_HandleKey(int sdlKey) {
    if (!sOpen || sPageStack.empty()) {
        return false;
    }
    bool consumed = false;
    {
        MenuPage& page = sPageStack.back();
        int n = static_cast<int>(page.items.size());

        auto clampViewport = [&]() {
            int visible = std::min(n, kVisibleItemsMax);
            if (page.cursor < page.viewportTop) {
                page.viewportTop = page.cursor;
            } else if (page.cursor >= page.viewportTop + visible) {
                page.viewportTop = page.cursor - visible + 1;
            }
            if (page.viewportTop < 0) {
                page.viewportTop = 0;
            }
            if (page.viewportTop + visible > n) {
                page.viewportTop = std::max(0, n - visible);
            }
        };

        switch (sdlKey) {
            case SDLK_UP:
                if (n > 0) {
                    page.cursor = (page.cursor - 1 + n) % n;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_DOWN:
                if (n > 0) {
                    page.cursor = (page.cursor + 1) % n;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_PAGEUP:
                if (n > 0) {
                    page.cursor = std::max(0, page.cursor - kVisibleItemsMax);
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_PAGEDOWN:
                if (n > 0) {
                    page.cursor = std::min(n - 1, page.cursor + kVisibleItemsMax);
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_HOME:
                if (n > 0) {
                    page.cursor = 0;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_END:
                if (n > 0) {
                    page.cursor = n - 1;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (page.cursor >= 0 && page.cursor < n) {
                    /* Copy the function so the std::function we're calling
                     * stays alive even if the page (and its items) get
                     * popped/cleared inside the lambda. For cycle items,
                     * Enter behaves like Right (forward cycle). */
                    auto& it = page.items[page.cursor];
                    auto fn = it.action ? it.action : it.cycleRight;
                    if (fn)
                        fn();
                }
                consumed = true;
                break;
            case SDLK_LEFT:
                if (page.cursor >= 0 && page.cursor < n) {
                    auto fn = page.items[page.cursor].cycleLeft;
                    if (fn)
                        fn();
                }
                consumed = true;
                break;
            case SDLK_RIGHT:
                if (page.cursor >= 0 && page.cursor < n) {
                    auto fn = page.items[page.cursor].cycleRight;
                    if (fn)
                        fn();
                }
                consumed = true;
                break;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                Pop();
                consumed = true;
                break;
            default:
                break;
        }
        /* `page` reference must not be used after this scope ends — the
         * pending-mutation step below may invalidate it. */
    }
    ApplyPendingMutations();
    return consumed;
}

extern "C" void Port_DebugMenu_Render(SDL_Renderer* renderer, int winW, int winH) {
    if (!renderer) {
        return;
    }

    /* Toast: visible whether menu is open or not, e.g. after a warp. */
    if (!sToast.empty() && SDL_GetTicks() < sToastUntilTicks) {
        const int charW = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
        int textW = static_cast<int>(sToast.size()) * charW;
        SDL_FRect bg = { (winW - textW) * 0.5f - 6.0f, winH - 28.0f, static_cast<float>(textW) + 12.0f, 18.0f };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_RenderFillRect(renderer, &bg);
        SDL_SetRenderDrawColor(renderer, 255, 240, 64, 255);
        SDL_RenderDebugText(renderer, bg.x + 6.0f, bg.y + 5.0f, sToast.c_str());
    }

    if (!sOpen || sPageStack.empty()) {
        return;
    }

    const MenuPage& page = sPageStack.back();
    const int charW = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

    /* Scroll viewport: clamp to a window of kVisibleItemsMax items. The
     * key handler keeps page.cursor inside [viewportTop, viewportTop + visible). */
    const int total = static_cast<int>(page.items.size());
    const int visible = std::min(total, kVisibleItemsMax);
    int top = page.viewportTop;
    if (top < 0) {
        top = 0;
    }
    if (top + visible > total) {
        top = std::max(0, total - visible);
    }
    const bool moreAbove = top > 0;
    const bool moreBelow = (top + visible) < total;

    /* Materialize each visible label up-front: cycle items reconstruct a
     * fresh string from labelFn() each frame, and we need the same value
     * for both column-width sizing and rendering below. */
    std::vector<std::string> visibleLabels;
    visibleLabels.reserve(static_cast<size_t>(visible));
    for (int i = top; i < top + visible && i < total; ++i) {
        const MenuItem& it = page.items[i];
        visibleLabels.push_back(it.labelFn ? it.labelFn() : it.label);
    }

    /* Reserve up to 4 extra rows for: title, "..." above, "..." below,
     * blank, and 2 hint lines at the bottom. */
    int rows = 2 + visible + (moreAbove ? 1 : 0) + (moreBelow ? 1 : 0) + 3;
    int cols = static_cast<int>(page.title.size());
    for (const auto& lbl : visibleLabels) {
        cols = std::max(cols, static_cast<int>(lbl.size()) + 4);
    }
    cols = std::max(cols, 36);

    float boxW = static_cast<float>(cols * charW + 16);
    float boxH = static_cast<float>(rows * (charW + 4) + 12);
    SDL_FRect box = { (winW - boxW) * 0.5f, (winH - boxH) * 0.5f, boxW, boxH };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderRect(renderer, &box);

    float y = box.y + 8.0f;
    SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
    char titleBuf[160];
    if (total > kVisibleItemsMax) {
        std::snprintf(titleBuf, sizeof(titleBuf), "%s  [%d/%d]", page.title.c_str(), page.cursor + 1, total);
    } else {
        std::snprintf(titleBuf, sizeof(titleBuf), "%s", page.title.c_str());
    }
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, titleBuf);
    y += charW + 8.0f;

    if (moreAbove) {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDebugText(renderer, box.x + 8.0f, y, "  ^ ^ ^");
        y += charW + 4.0f;
    }

    for (int i = top; i < top + visible && i < total; ++i) {
        bool sel = i == page.cursor;
        const std::string& lbl = visibleLabels[static_cast<size_t>(i - top)];
        std::string line = (sel ? "> " : "  ") + lbl;
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 255, 240, 64, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
        }
        SDL_RenderDebugText(renderer, box.x + 8.0f, y, line.c_str());
        y += charW + 4.0f;
    }

    if (moreBelow) {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDebugText(renderer, box.x + 8.0f, y, "  v v v");
        y += charW + 4.0f;
    }

    y += 4.0f;
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, "Up/Dn move  PgUp/PgDn page  Home/End ends");
    y += charW + 4.0f;
    SDL_RenderDebugText(renderer, box.x + 8.0f, y, "Enter select  L/R cycle  Esc back  F5/F6 save/load");
}
