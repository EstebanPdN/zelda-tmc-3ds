/*
 * port_imgui_menu.cpp — Dear ImGui drawing layer for the F8 debug menu.
 *
 * Replaces the SDL_RenderDebugText-based overlay in port_debug_menu.cpp
 * with an ImGui window that looks (and feels) like a proper modern UI:
 * styled panels, hover/selection highlights, real fonts, scrollable lists.
 *
 * Architecture choice — keep the menu *state* (page stack, cursor,
 * action lambdas, label callbacks) in port_debug_menu.cpp untouched, and
 * have this file render *from* that state. Input still flows through
 * Port_DebugMenu_HandleKey so all the existing key bindings (Up/Down,
 * Enter, Left/Right cycle, Esc back, PgUp/PgDn, Home/End) keep working.
 *
 * The ImGui context is owned here. Init/Shutdown are called from
 * port_main.c after SDL is up. The per-frame begin/end pair is called
 * from port_ppu.cpp around the SDL_RenderPresent so the menu draws on
 * top of the rasterized GBA frame.
 *
 * Toggling between ImGui and the legacy SDL-text path: set
 * sPortImGuiEnabled from outside (default on) — when off, this whole TU
 * is a no-op and port_debug_menu.cpp's classic renderer runs instead.
 */

#include <SDL3/SDL.h>
#include "port_imgui_menu.h"
#include <imgui.h>

/* .glslp runtime hooks (port_glslp_runtime.cpp). File-scope so the F8
 * preset-picker lambda below can call them through C linkage. */
extern "C" int Port_GlslpRuntime_Load(const char*);
extern "C" void Port_GlslpRuntime_Unload(void);
extern "C" int Port_GlslpRuntime_IsActive(void);
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#ifdef TMC_GPU_RENDERER
#include <SDL3/SDL_gpu.h>
#include <backends/imgui_impl_sdlgpu3.h>
#endif

#include "port_debug_query.h"
#include "port_debug_actions.h"
#include "port_runtime_config.h" /* PortInput enum (PORT_INPUT_*) */
#include "item_ids.h"            /* ITEM_* / BOTTLE_CHARM_* enum ids (C++-safe split header) */
#include <cstring>               /* strcmp — group-header breaks in the item toggle list */
#include <cstdio>                /* snprintf — dungeon selector labels */

extern "C" u32* gTranslations[];
extern "C" void Port_ApplyLanguage(void);

#include "port_widescreen.h"
#include "port_gpu_renderer.h"
#include "port_reborn.h"
#include "port_discord_rpc.h" /* Port_DiscordRpc_IsEnabled / SetEnabled */
#include "port_tts.h"         /* Port_TTS_* — accessibility tab + focus reader */
#include "port_a11y_cues.h"   /* Port_A11y_ScanSurroundings — navigation cues */
#include "port_softslots.h"
#include "item_ids.h"

extern "C" {
unsigned GetInventoryValue(unsigned item);
unsigned CheckLocalFlagByBank(unsigned bankOffset, unsigned flag);
unsigned GetFlagBankOffset(unsigned area);
unsigned CheckGlobalFlag(unsigned flag);
}

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

/* The menu state machine lives in port_debug_menu.cpp. We don't include
 * its header (it doesn't expose the page-stack internals) — instead the
 * legacy file exposes a small accessor API just for us. */
extern "C" {
bool Port_DebugMenu_IsOpen(void);
int Port_DebugMenu_PageDepth(void);
const char* Port_DebugMenu_PageTitle(int depth);
int Port_DebugMenu_PageItemCount(int depth);
const char* Port_DebugMenu_PageItemLabel(int depth, int idx);
int Port_DebugMenu_PageCursor(int depth);
void Port_DebugMenu_PageSetCursor(int depth, int idx);
void Port_DebugMenu_PageActivate(int depth, int idx);   /* Enter on item */
void Port_DebugMenu_PageCycleLeft(int depth, int idx);  /* Left arrow */
void Port_DebugMenu_PageCycleRight(int depth, int idx); /* Right arrow */
const char* Port_DebugMenu_Toast(void);                 /* NULL if expired */
}

static bool sImGuiInited = false;
static bool sRibbonEnabled = true; /* Office-style ribbon at top */
static SDL_Window* sWindow = nullptr;
static SDL_Renderer* sRenderer = nullptr;

extern "C" void Port_ImGui_Init(SDL_Window* window, SDL_Renderer* renderer) {
    if (sImGuiInited)
        return;
    if (!window)
        return;
    /* On GPU builds renderer is intentionally NULL — Port_PPU_Init passes
     * null when the SDL_GPU pipeline owns the swapchain. The GPU branch
     * below handles that case; the SDL_Renderer branch still requires
     * a non-null renderer. */
#ifndef TMC_GPU_RENDERER
    if (!renderer)
        return;
#endif

    /* Apply the persisted F8 menu style (ribbon vs classic) now that config
     * has been loaded (issue #146). */
    sRibbonEnabled = Port_Config_GetRibbonEnabled();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; /* don't write imgui.ini next to binary */
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* Gamepad nav so Steam Deck users (and anyone on a controller) can
     * drive the menu without keyboard/mouse. SDL3 backend forwards the
     * connected gamepad's stick + D-pad + A/B as ImGui nav inputs. */
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    /* Don't capture keyboard from the game — we render to a window
     * that's also receiving game input; let game keys pass through
     * unless an ImGui widget genuinely wants them. */
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    /* Modern dark style with chunky padding so the UI stays touch- and
     * Steam-Deck-friendly. The Deck's 7" 1280×800 screen is small in
     * physical pixels but high DPI relative to the player's hands; what
     * looks chunky on a desktop monitor reads as comfortably sized on
     * the Deck. Players on a normal monitor still get a clean look. */
    /* Project Picori theme — heavier rounding + deep-green accents
     * inspired by the Dusklight TP PC port UI. The previous blue palette
     * stayed for the in-game F8 dev menu vibe; this theme leans into the
     * Minish-Cap green character (Ezlo, Link's hat, Minish leaves) and
     * card-like surfaces with bigger rounding so the launcher screen
     * and config tabs feel cohesive. */
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.WindowBorderSize = 0.0f; /* card look — solid fills, no outline */
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.WindowPadding = ImVec2(18, 16);
    style.FramePadding = ImVec2(14, 9); /* bigger touch targets */
    style.ItemSpacing = ImVec2(12, 10);
    style.ItemInnerSpacing = ImVec2(10, 6);
    style.ScrollbarSize = 18.0f; /* finger-draggable */
    style.GrabMinSize = 18.0f;
    style.IndentSpacing = 22.0f;
    /* Bump the global font size 1.4× without re-loading a font atlas.
     * ImGui scales the default ProggyClean upward; the resulting glyphs
     * are crisp enough at native resolution for menu use, and big
     * enough to be readable on the Deck at hand-held distance. */
    io.FontGlobalScale = 1.4f;
#ifdef __ANDROID__
    /* Touch pass: a tablet is driven by fingers at arm's length, not a
     * pointer. Scale the whole style so every hit target clears ~48dp
     * (Android's minimum comfortable touch target), fatten scrollbars
     * into real drag handles, and bump the font again over the desktop
     * 1.4x. ScaleAllSizes multiplies paddings/rounding/grab sizes in
     * one shot so proportions stay intact. */
    style.ScaleAllSizes(1.55f);
    style.ScrollbarSize = 34.0f;                  /* fat, thumb-sized scroll handle  */
    style.GrabMinSize = 30.0f;                    /* slider grabs                    */
    style.FramePadding.y += 6.0f;                 /* taller rows = taller tap areas  */
    style.ItemSpacing.y += 4.0f;                  /* breathing room between rows     */
    style.TouchExtraPadding = ImVec2(6.0f, 6.0f); /* forgiving hit test */
    io.FontGlobalScale = 2.0f;
#endif
    ImVec4* colors = style.Colors;
    /* Greens — primary accent (a deep, slightly-warm green that
     * reads as "Minish leaf"), with brighter / dimmer variants. */
    const ImVec4 accentDim = ImVec4(0.18f, 0.32f, 0.22f, 1.00f);
    const ImVec4 accent = ImVec4(0.28f, 0.55f, 0.34f, 1.00f);
    const ImVec4 accentLit = ImVec4(0.40f, 0.72f, 0.46f, 1.00f);
    /* Surface — near-black with a faint cool tint so the green pops. */
    const ImVec4 bgBase = ImVec4(0.058f, 0.07f, 0.07f, 0.96f);
    const ImVec4 bgChild = ImVec4(0.085f, 0.10f, 0.10f, 1.00f);
    const ImVec4 bgFrame = ImVec4(0.13f, 0.15f, 0.15f, 1.00f);
    const ImVec4 bgFrameH = ImVec4(0.17f, 0.21f, 0.20f, 1.00f);

    colors[ImGuiCol_WindowBg] = bgBase;
    colors[ImGuiCol_ChildBg] = bgChild;
    colors[ImGuiCol_PopupBg] = bgBase;
    colors[ImGuiCol_FrameBg] = bgFrame;
    colors[ImGuiCol_FrameBgHovered] = bgFrameH;
    colors[ImGuiCol_FrameBgActive] = accentDim;
    colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.10f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = accentDim;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.07f, 0.06f, 0.75f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.12f, 0.11f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.32f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Button] = bgFrame;
    colors[ImGuiCol_ButtonHovered] = accent;
    colors[ImGuiCol_ButtonActive] = accentLit;
    colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.13f, 0.11f, 1.00f);
    colors[ImGuiCol_TabHovered] = accent;
    colors[ImGuiCol_TabActive] = accentDim;
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.09f, 0.08f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.18f, 0.15f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.24f, 0.22f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accentLit;
    colors[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentLit;
    colors[ImGuiCol_CheckMark] = accentLit;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.24f, 0.22f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = accentDim;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.54f, 0.50f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.40f);

#ifdef TMC_GPU_RENDERER
    /* GPU path: renderer arg is NULL (Port_PPU_Init passed null when the
     * SDL_GPU pipeline owns the window). Initialise the SDL_GPU ImGui
     * backend instead — its NewFrame/PrepareDrawData/RenderDrawData
     * trio integrates with our existing SDL_GPU PresentFrame. */
    if (renderer == nullptr) {
        SDL_GPUDevice* dev = Port_GPU_GetDevice();
        SDL_GPUTextureFormat fmt = Port_GPU_GetSwapchainFormat();
        if (!dev || fmt == SDL_GPU_TEXTUREFORMAT_INVALID) {
            fprintf(stderr, "[imgui] GPU device/format unavailable - F8 menu disabled\n");
            ImGui::DestroyContext();
            return;
        }
        if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
            fprintf(stderr, "[imgui] ImGui_ImplSDL3_InitForSDLGPU failed\n");
            ImGui::DestroyContext();
            return;
        }
        ImGui_ImplSDLGPU3_InitInfo info = {};
        info.Device = dev;
        info.ColorTargetFormat = fmt;
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        if (!ImGui_ImplSDLGPU3_Init(&info)) {
            fprintf(stderr, "[imgui] ImGui_ImplSDLGPU3_Init failed\n");
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return;
        }
        sWindow = window;
        sRenderer = nullptr; /* GPU backend signals "no SDL_Renderer" */
        sImGuiInited = true;
        fprintf(stderr, "[imgui] initialized (v%s, SDL_GPU backend)\n", IMGUI_VERSION);
        return;
    }
#endif

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
        fprintf(stderr, "[imgui] ImGui_ImplSDL3 init failed\n");
        ImGui::DestroyContext();
        return;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
        fprintf(stderr, "[imgui] ImGui_ImplSDLRenderer3 init failed\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return;
    }

    sWindow = window;
    sRenderer = renderer;
    sImGuiInited = true;
    fprintf(stderr, "[imgui] initialized (v%s, SDL_Renderer backend)\n", IMGUI_VERSION);
}

extern "C" void Port_ImGui_Shutdown(void) {
    if (!sImGuiInited)
        return;
#ifdef TMC_GPU_RENDERER
    if (!sRenderer) {
        ImGui_ImplSDLGPU3_Shutdown();
    } else
#endif
    {
        ImGui_ImplSDLRenderer3_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    sImGuiInited = false;
}


extern "C" bool Port_ImGui_CanPresent(void) {
    if (!sImGuiInited)
        return false;
#ifndef TMC_GPU_RENDERER
    if (!sRenderer)
        return false;
#endif
    return true;
}

/* True when an ImGui text widget currently has keyboard focus (e.g. the
 * seed entry field). The port input layer consults this before letting a
 * keyboard key that doubles as a GBA button (default L = 'a', a valid seed
 * char) close the file-select setup sidebar, so typing a seed isn't
 * interrupted. */
extern "C" bool Port_ImGui_WantsTextInput(void) {
    if (!sImGuiInited)
        return false;
    return ImGui::GetIO().WantTextInput;
}
extern "C" void Port_ImGui_HandleEvent(const SDL_Event* event) {
    if (!sImGuiInited)
        return;
    ImGui_ImplSDL3_ProcessEvent(event);
#ifdef __ANDROID__
    /* Touch drag-to-scroll: ImGui has no native flick/drag scrolling —
     * on desktop the wheel does it; on a tablet nothing does, and long
     * tabs (Warp's area list, Items) are unusable. Convert vertical
     * finger motion into wheel events while a menu is up.
     *
     * Deliberately NOT gated on IsAnyItemActive: a finger resting on a
     * row button activates it instantly, which would veto the very drag
     * that's supposed to scroll. Buttons commit on RELEASE and ImGui
     * cancels a press whose item scrolls out from under the pointer, so
     * scrolling over buttons is safe. Horizontal wheel is dropped —
     * sliders are horizontal drags; injecting dx would fight them and
     * almost nothing scrolls horizontally. Text-input focus (drag =
     * text selection) suppresses injection entirely. */
    if (event->type == SDL_EVENT_FINGER_MOTION && !ImGui::GetIO().WantTextInput &&
        Port_DebugMenu_IsOpen()) {
        ImGuiIO& io = ImGui::GetIO();
        const float dyPx = event->tfinger.dy * io.DisplaySize.y;
        /* One wheel notch scrolls ~67px in ImGui; convert px so content
         * tracks the finger 1:1-ish. Sign: finger down = content down. */
        io.AddMouseWheelEvent(0.0f, dyPx / 67.0f);
    }
#endif
}

extern "C" bool Port_ImGui_IsEnabled(void) {
    return true;
}
extern "C" bool Port_ImGui_RibbonEnabled(void) {
    return sRibbonEnabled;
}
extern "C" void Port_ImGui_SetRibbonEnabled(bool enabled) {
    sRibbonEnabled = enabled;
}

static void HelpTooltip(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(420.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
/* ------------------------------------------------------------------ */
/*   Externs for the ribbon's direct-action widgets                   */
/* ------------------------------------------------------------------ */
/* The classic menu builders compose action lambdas internally; the
 * ribbon bypasses the page stack and calls these underlying actions
 * directly, exposing each setting as a proper ImGui widget instead of
 * a list row. Same backing functions either way, so behaviour matches. */
extern "C" {
void Port_DebugAction_GiveAllItems(void);
void Port_DebugAction_MaxHearts(void);
void Port_DebugAction_HealFull(void);
void Port_DebugAction_MaxRupees(void);
void Port_DebugAction_MaxShells(void);
void Port_DebugAction_AllKinstones(void);

void Port_PPU_ToggleFullscreen(void);
bool Port_PPU_IsFullscreen(void);
void Port_PPU_ApplyCursorVisibility(void);
void Port_PPU_SetVSync(bool enabled);
bool Port_PPU_VSyncEnabled(void);
void Port_PPU_SetColorCorrection(bool enabled);
bool Port_PPU_ColorCorrectionEnabled(void);
void Port_PPU_SetPersistence(bool enabled, float rho);
void Port_PPU_CycleWindowScale(int direction);
void Port_PPU_ApplyWindowScale(void);
unsigned char Port_PPU_WindowScale(void);
void Port_PPU_CyclePresentationMode(int direction);
const char* Port_PPU_PresentationModeName(void);
void Port_PPU_CycleFilter(int direction);
const char* Port_PPU_FilterName(void);
unsigned int Port_Config_TargetFps(void);
void Port_Config_CycleTargetFps(int direction);
unsigned char Port_Config_InternalScale(void);
void Port_Config_CycleInternalScale(int direction);
void Port_Audio_SetGbaAccurate(bool accurate);
bool Port_Audio_IsGbaAccurate(void);
void Port_Audio_SetWidth(float width);
float Port_Audio_GetWidth(void);
void Port_Audio_SetReverbLevel(int level);
int Port_Audio_GetReverbLevel(void);
void Port_Audio_SetMasterVolume(float volume);
float Port_Audio_GetMasterVolume(void);

int Port_QuickSave_SaveSlot(int slot);
int Port_QuickSave_LoadSlot(int slot);
int Port_QuickSave_HasSlot(int slot);
unsigned long long Port_QuickSave_SlotTimestamp(int slot);
int Port_QuickSave_SlotCount(void);
int Port_QuickSave_AutoSlotBase(void);
int Port_QuickSave_AutoEnabled(void);
void Port_QuickSave_SetAutoEnabled(int enabled);
unsigned int Port_QuickSave_AutoIntervalMs(void);
void Port_QuickSave_SetAutoIntervalMs(unsigned int ms);
bool Port_Config_AutosaveEnabled(void);
void Port_Config_SetAutosaveEnabled(bool enabled);
void Port_Config_SetAutosaveIntervalMs(unsigned int ms);

const char* Port_Save_GetActivePath(void);
int Port_Save_SetActivePath(const char* path);
int Port_Save_SaveAsProfile(const char* path);
int Port_Save_ListProfiles(char (*out)[64], int max);
int Port_Save_DeleteProfile(const char* path);
int Port_Save_RenameProfile(const char* oldPath, const char* newPath);
void Port_Config_SetActiveSaveProfile(const char* path);

const char* Port_SoftSlots_GetSlotLabel(int slot);
void Port_SoftSlots_CycleAssignment(int slot, int direction);

const char* Port_Config_InputName(PortInput input);
int Port_Config_BindingCount(PortInput input);
void Port_Config_BindingLabel(PortInput input, int idx, char* out, int cap);
void Port_Config_ClearBindings(PortInput input);
void Port_Config_BeginCaptureBinding(PortInput input);
void Port_Config_BeginAddBinding(PortInput input);
int Port_Config_IsCapturingBinding(void);
int Port_Config_CapturingBindingInput(void);
void Port_Config_CancelCaptureBinding(void);
void Port_Config_ResetAllBindings(void);

void Port_DebugMenu_Toggle(void);

/* Speedrun practice mode (port_practice.c). u16/u64 are declared here as the
 * underlying fixed-width types; extern "C" matches by symbol name so this
 * stays ABI-compatible with the C definitions. */
unsigned long long Port_Practice_ElapsedFrames(void);
bool Port_Practice_TimerRunning(void);
void Port_Practice_TimerReset(void);
void Port_Practice_TimerToggle(void);
void Port_Practice_AddSplit(void);
int Port_Practice_SplitCount(void);
unsigned long long Port_Practice_SplitAt(int i);
void Port_Practice_ClearSplits(void);
unsigned short Port_Practice_CurrentInputMask(void);
unsigned short Port_Practice_HistoryAt(int index);
int Port_Practice_HistoryCount(void);
int Port_Practice_SetPoint(void);
int Port_Practice_LoadPoint(void);
bool Port_Practice_HasPoint(void);
bool Port_Practice_IsPaused(void);
void Port_Practice_TogglePause(void);
}

/* Mini-toast for ribbon actions so the user sees "Saved" etc. without
 * having to look at stderr. Reuses the legacy Toast() path through the
 * existing public toast accessor. */
extern "C" void Port_DebugMenu_ToastFromExternal(const char* msg);

/* ---- Feature 1 (per-item toggle) + 6 (charm / picolyte) ribbon widgets ----
 * All game-state knowledge lives in port_debug_actions.c; these helpers only
 * enumerate the C layer (index/name/group) and drive the corresponding
 * Set/Query actions, so no ITEM_* logic crosses into this C++ TU beyond the
 * C++-safe enum constants from item_ids.h. */

/* Per-item ownership grid. Items arrive from the C layer already grouped
 * (contiguous by group string); each group becomes a collapsible header so
 * the long list doesn't dominate the Items tab. Each checkbox reflects live
 * ownership, so slot-exclusivity clears show up on the next frame. */
static void DrawRibbonItemToggles(void) {
    const int count = Port_DebugQuery_ToggleItemCount();
    const char* curGroup = nullptr;
    bool groupOpen = false;
    for (int i = 0; i < count; ++i) {
        const char* group = Port_DebugQuery_ToggleItemGroup(i);
        if (!group)
            continue;
        if (!curGroup || strcmp(group, curGroup) != 0) {
            curGroup = group;
            groupOpen = ImGui::CollapsingHeader(group);
        }
        if (!groupOpen)
            continue;
        bool owned = Port_DebugQuery_ToggleItemOwned(i) != 0;
        ImGui::PushID(i);
        if (ImGui::Checkbox(Port_DebugQuery_ToggleItemName(i), &owned)) {
            Port_DebugAction_SetToggleItem(i, owned ? 1 : 0);
        }
        ImGui::PopID();
    }
}

/* Stable per-frame label for dungeon id d, marking the current dungeon. */
static const char* DungeonLabel(int d, int cur) {
    static char buf[40];
    snprintf(buf, sizeof(buf), "Dungeon %d%s", d, (d == cur) ? "  (current)" : "");
    return buf;
}

/* Any-dungeon Map / Compass / Big Key / Small Key editor. The arrays are
 * indexed by dungeon id; the engine only ever writes the current area's
 * slot, so we write them directly to reach any dungeon. */
static void DrawRibbonDungeonItems(void) {
    static int sDungeon = 0;
    const int cur = Port_DebugQuery_CurrentDungeon();
    if (sDungeon < 0 || sDungeon > 15)
        sDungeon = 0;

    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Dungeon", DungeonLabel(sDungeon, cur))) {
        for (int d = 0; d < 16; ++d) {
            const bool sel = (d == sDungeon);
            ImGui::PushID(d);
            if (ImGui::Selectable(DungeonLabel(d, cur), sel))
                sDungeon = d;
            if (sel)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (cur >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Jump to current"))
            sDungeon = cur;
    }

    const int bits = Port_DebugQuery_DungeonItems(sDungeon);
    bool map = (bits & 0x1) != 0;
    bool comp = (bits & 0x2) != 0;
    bool big = (bits & 0x4) != 0;
    if (ImGui::Checkbox("Map", &map))
        Port_DebugAction_SetDungeonItem(sDungeon, 0, map);
    ImGui::SameLine();
    if (ImGui::Checkbox("Compass", &comp))
        Port_DebugAction_SetDungeonItem(sDungeon, 1, comp);
    ImGui::SameLine();
    if (ImGui::Checkbox("Big Key", &big))
        Port_DebugAction_SetDungeonItem(sDungeon, 2, big);

    int keys = Port_DebugQuery_DungeonKeys(sDungeon);
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Small keys", &keys)) {
        if (keys < 0)
            keys = 0;
        if (keys > 255)
            keys = 255;
        Port_DebugAction_SetDungeonKeys(sDungeon, keys);
    }
}

extern "C" {
typedef void (*PortBuffApplyFn)(int, int);
typedef int (*PortBuffQueryFn)(int*, int*);
}

/* Charm and picolyte share the same combo + frames-slider + Apply + live-
 * status shape; only the labels, id list, default state, and apply/query
 * hooks differ. Selection + frames state is caller-owned so the two buffs
 * don't share it. */
static void DrawTimedBuff(const char* label, const char* lname, const char* applyId, const char** names, const int* ids,
                          int count, int* sel, int* frames, PortBuffApplyFn apply, PortBuffQueryFn query) {
    char tag[32];
    ImGui::SetNextItemWidth(200);
    std::snprintf(tag, sizeof(tag), "%s type", label);
    ImGui::Combo(tag, sel, names, count);
    ImGui::SetNextItemWidth(200);
    std::snprintf(tag, sizeof(tag), "%s frames", label);
    ImGui::SliderInt(tag, frames, 0, 65535, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    std::snprintf(tag, sizeof(tag), "Apply##%s", applyId);
    if (ImGui::Button(tag)) {
        apply(ids[*sel], *frames);
        char toast[40];
        std::snprintf(toast, sizeof(toast), "%s %s", label, *sel == 0 ? "cleared" : "applied");
        Port_DebugMenu_ToastFromExternal(toast);
    }
    int id = 0, timer = 0;
    if (query(&id, &timer))
        ImGui::TextDisabled("%s active: id %d, %d frames (%.1fs left)", lname, id, timer, timer / 60.0f);
    else
        ImGui::TextDisabled("%s: inactive", lname);
}

/* Charm + Picolyte activator. The combo + slider compose a buff to apply on
 * the button; the live line shows what's currently ticking (the engine
 * counts the timer down each frame, so the slider isn't bound to it). */
static void DrawRibbonBuffs(void) {
    static const char* kCharmNames[] = { "Off", "Nayru (1/4 dmg taken)", "Farore (1/2 dmg taken)",
                                         "Din (2x dmg dealt)" };
    static const int kCharmIds[] = { 0, BOTTLE_CHARM_NAYRU, BOTTLE_CHARM_FARORE, BOTTLE_CHARM_DIN };
    static int sCharmSel = 1;
    static int sCharmFrames = 3600;
    DrawTimedBuff("Charm", "charm", "charm", kCharmNames, kCharmIds, IM_ARRAYSIZE(kCharmNames), &sCharmSel,
                  &sCharmFrames, Port_DebugAction_SetCharm, Port_DebugQuery_Charm);

    ImGui::Spacing();

    static const char* kPicoNames[] = { "Off", "Red", "Orange", "Yellow", "Green", "Blue", "White" };
    static const int kPicoIds[] = { 0,
                                    ITEM_BOTTLE_PICOLYTE_RED,
                                    ITEM_BOTTLE_PICOLYTE_ORANGE,
                                    ITEM_BOTTLE_PICOLYTE_YELLOW,
                                    ITEM_BOTTLE_PICOLYTE_GREEN,
                                    ITEM_BOTTLE_PICOLYTE_BLUE,
                                    ITEM_BOTTLE_PICOLYTE_WHITE };
    static int sPicoSel = 1;
    static int sPicoFrames = 900;
    DrawTimedBuff("Picolyte", "picolyte", "pico", kPicoNames, kPicoIds, IM_ARRAYSIZE(kPicoNames), &sPicoSel,
                  &sPicoFrames, Port_DebugAction_SetPicolyte, Port_DebugQuery_Picolyte);
}

/* Numeric count / capacity sliders. Bounds come from the C layer (counts clamp
 * to the live capacity tier), so each row is a plain min..max slider. */
static void DrawRibbonStats(void) {
    const int count = Port_DebugQuery_StatCount();
    for (int i = 0; i < count; ++i) {
        int v = Port_DebugQuery_StatValue(i);
        const int lo = Port_DebugQuery_StatMin(i);
        const int hi = Port_DebugQuery_StatMax(i);
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderInt(Port_DebugQuery_StatName(i), &v, lo, hi)) {
            Port_DebugAction_SetStat(i, v);
        }
        ImGui::PopID();
    }
}

/* Per-bottle content picker. Choosing a content also grants the bottle. */
static void DrawRibbonBottles(void) {
    const int nContents = Port_DebugQuery_BottleContentCount();
    for (int b = 0; b < 4; ++b) {
        ImGui::PushID(b);
        const bool owned = Port_DebugQuery_BottleOwned(b) != 0;
        const int curIdx = Port_DebugQuery_BottleContentIndex(Port_DebugQuery_BottleContent(b));
        char label[16];
        snprintf(label, sizeof(label), "Bottle %d", b + 1);
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo(label, Port_DebugQuery_BottleContentName(curIdx))) {
            for (int i = 0; i < nContents; ++i) {
                const bool sel = (i == curIdx);
                if (ImGui::Selectable(Port_DebugQuery_BottleContentName(i), sel)) {
                    Port_DebugAction_SetBottleContent(b, Port_DebugQuery_BottleContentId(i));
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!owned) {
            ImGui::SameLine();
            ImGui::TextDisabled("(not owned — pick to grant)");
        }
        ImGui::PopID();
    }
}

static void DrawRibbonItemsTab(void) {
    if (ImGui::BeginTable("##items_tab_table", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Stats & Unlocks", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Recovery & Cheats", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::SeparatorText("Stats & Unlocks");
        if (ImGui::Button("Unlock all items", ImVec2(200, 0))) {
            Port_DebugAction_GiveAllItems();
            Port_DebugMenu_ToastFromExternal("All items granted");
        }
        if (ImGui::Button("All kinstones fused", ImVec2(200, 0))) {
            Port_DebugAction_AllKinstones();
            Port_DebugMenu_ToastFromExternal("All kinstones");
        }
        if (ImGui::Button("All figurines (130)", ImVec2(200, 0))) {
            Port_DebugAction_AllFigurines130();
            Port_DebugMenu_ToastFromExternal("All 130 figurines (no game-clear)");
        }
        if (ImGui::Button("Figurines 100% (marks beaten)", ImVec2(200, 0))) {
            Port_DebugAction_AllFigurines100();
            Port_DebugMenu_ToastFromExternal("136 figurines + game marked cleared");
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::SeparatorText("Recovery & Cheats");
        if (ImGui::Button("Heal", ImVec2(120, 0))) {
            Port_DebugAction_HealFull();
            Port_DebugMenu_ToastFromExternal("Healed");
        }
        ImGui::SameLine();
        if (ImGui::Button("Max hearts", ImVec2(120, 0))) {
            Port_DebugAction_MaxHearts();
            Port_DebugMenu_ToastFromExternal("Hearts maxed");
        }
        if (ImGui::Button("999 rupees", ImVec2(120, 0))) {
            Port_DebugAction_MaxRupees();
            Port_DebugMenu_ToastFromExternal("999 rupees");
        }
        ImGui::SameLine();
        if (ImGui::Button("999 shells", ImVec2(120, 0))) {
            Port_DebugAction_MaxShells();
            Port_DebugMenu_ToastFromExternal("999 shells");
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Per-item toggle");
    DrawRibbonItemToggles();

    ImGui::Spacing();
    ImGui::SeparatorText("Dungeon items (any dungeon)");
    DrawRibbonDungeonItems();

    ImGui::Spacing();
    ImGui::SeparatorText("Counts & capacities");
    DrawRibbonStats();

    ImGui::Spacing();
    ImGui::SeparatorText("Bottle contents");
    DrawRibbonBottles();

    ImGui::Spacing();
    ImGui::SeparatorText("Charm / Picolyte");
    DrawRibbonBuffs();
}

/* Raw flag browser tab (wishlist #5). Pick a bank, scroll the flag grid,
 * toggle bits. The list is clipped so bank 12 (1408 flags) stays cheap. */
static void DrawRibbonFlagsTab(void) {
    static int sBank = 0;
    const int nBanks = Port_DebugQuery_FlagBankCount();
    if (sBank < 0 || sBank >= nBanks)
        sBank = 0;
    const int cur = Port_DebugQuery_CurrentFlagBank();

    ImGui::TextUnformatted("Raw save flags (gSave.flags). Bank 0 = global; 1-12 = local pools.");
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Bank", Port_DebugQuery_FlagBankName(sBank))) {
        for (int b = 0; b < nBanks; ++b) {
            const bool selected = (b == sBank);
            ImGui::PushID(b);
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "%s%s", Port_DebugQuery_FlagBankName(b), (b == cur) ? "  (current area)" : "");
            if (ImGui::Selectable(lbl, selected))
                sBank = b;
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (cur >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Jump to current area"))
            sBank = cur;
    }

    const int size = Port_DebugQuery_FlagBankSize(sBank);
    const unsigned int off = Port_DebugQuery_FlagBankOffset(sBank);
    ImGui::Text("%d flags  (bit offset 0x%03X)", size, off);
    ImGui::TextDisabled("Heads-up: some flags fire cutscenes / credits the moment they're set.");

    ImGui::BeginChild("##flag_list", ImVec2(0, 300), true);
    ImGuiListClipper clipper;
    clipper.Begin(size);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            bool on = Port_DebugQuery_Flag(sBank, i) != 0;
            ImGui::PushID(i);
            char lbl[48];
            snprintf(lbl, sizeof(lbl), "idx %4d (0x%03X)   bit 0x%03X", i, i, off + (unsigned)i);
            if (ImGui::Checkbox(lbl, &on)) {
                Port_DebugAction_SetFlag(sBank, i, on ? 1 : 0);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

#include "port_imgui_display_tab.inc"

/* Save current game to EEPROM, then drop the player back at the
 * title screen. Issue #92 / "sleep menu goes back to title".
 *
 * Calling the engine's SetTask(TASK_TITLE) directly is fine here
 * because the F8 menu only opens while the game is in TASK_GAME —
 * SetTask is valid in that state. The save path is gated behind
 * Port_Save_Quicksave so we go through the same EEPROM-write code
 * the F5 quicksave uses (which is known good). */
extern "C" {
void SetTask(unsigned int task);
}
extern "C" int Port_QuickSave_SaveSlot(int slot);
extern "C" int Port_QuickSave_AutoOnAreaChangeEnabled(void);
extern "C" void Port_QuickSave_SetAutoOnAreaChange(int on);

static void DoQuitToTitle(bool saveFirst) {
    if (saveFirst) {
        /* Slot 0 is the F5/F6 quicksave slot — writing there mirrors
         * the user pressing F5 first. They can still F6-load it on
         * the next launch. */
        Port_QuickSave_SaveSlot(0);
    }
    SetTask(0 /* TASK_TITLE */);
    Port_DebugMenu_Toggle(); /* close the F8 ribbon */
}
static bool DrawRegionLanguageControls(bool prelaunch) {
    bool regionChanged = false;
    ImGui::SeparatorText("ROM Region & Language");

    int preferredRegion = Port_Config_PreferredRegion();
    if (preferredRegion < -1 || preferredRegion > 2)
        preferredRegion = -1;

    const char* regionNames[] = {
        "Auto (Use first valid ROM)",
        "USA (baserom.gba)",
        "EU (baserom_eu.gba)",
        "JP (baserom_jp.gba)",
    };
    int regionIdx = preferredRegion + 1; // map -1..2 to 0..3
    ImGui::SetNextItemWidth(270);
    if (ImGui::Combo("Preferred ROM", &regionIdx, regionNames, 4)) {
        Port_Config_SetPreferredRegion(regionIdx - 1);
        regionChanged = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(prelaunch ? "(used when Play starts)" : "(restart required)");

    constexpr int kLanguageCount = 6;
    int preferredLanguage = Port_Config_PreferredLanguage();
    if (preferredLanguage < -1 || preferredLanguage >= kLanguageCount)
        preferredLanguage = -1;

    const char* langNames[] = {
        "Auto (ROM/save default)", "Japanese", "English", "French", "German", "Spanish", "Italian",
    };
    int langIdx = preferredLanguage + 1; // map -1..5 to 0..6

    ImGui::SetNextItemWidth(270);
    if (ImGui::BeginCombo("Language", langNames[langIdx])) {
        for (int i = 0; i < 7; ++i) {
            bool isSupported = true;
            char label[128];
            std::strcpy(label, langNames[i]);

            if (!prelaunch && i > 0) {
                const int langVal = i - 1;
                if (gTranslations[langVal] == nullptr) {
                    isSupported = false;
                    std::strcat(label, " (not supported by loaded ROM)");
                }
            }

            const bool selected = (i == langIdx);
            if (!isSupported)
                ImGui::BeginDisabled();
            if (ImGui::Selectable(label, selected)) {
                Port_Config_SetPreferredLanguage(i - 1);
                if (!prelaunch)
                    Port_ApplyLanguage();
            }
            if (!isSupported)
                ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    if (prelaunch) {
        ImGui::TextDisabled("Language is applied after the selected ROM loads.");
    }
    return regionChanged;
}

static void DrawRibbonSavesTab(void) {
    /* Quit-to-title actions at the top of the tab — high-visibility
     * because the existing pause menu doesn't expose them. */
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.55f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.70f, 0.40f, 1.0f));
    if (ImGui::Button("Save & Quit to Title"))
        DoQuitToTitle(true);
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.45f, 0.40f, 1.0f));
    if (ImGui::Button("Quit to Title (no save)"))
        DoQuitToTitle(false);
    ImGui::PopStyleColor(2);
    ImGui::Separator();

    /* Console-Parity — run-integrity master switch. Lives here because it
     * makes the save-state controls below inert. */
    {
        bool parity = Port_Config_GetConsoleParity();
        if (ImGui::Checkbox("Console-Parity mode (legit-run integrity)", &parity)) {
            Port_Config_SetConsoleParity(parity);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted("Holds the port provably equivalent to GBA "
                                   "hardware for legitimate speedruns:\n"
                                   "  - input edge-cache off (1-frame granularity)\n"
                                   "  - save-states inert (no mid-run restores)\n"
                                   "  - widescreen forced off (no early off-screen "
                                   "AI / RNG advance)\n"
                                   "  - frame pacing locked to 59.7275 Hz\n"
                                   "Leave OFF for practice/casual play.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        if (parity) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Save-states disabled while Console-Parity is ON.");
        }
    }
    ImGui::Separator();

    /* Auto-save controls at the top. */
    bool autoOn = Port_QuickSave_AutoEnabled();
    if (ImGui::Checkbox("Auto-save", &autoOn)) {
        Port_QuickSave_SetAutoEnabled(autoOn ? 1 : 0);
        Port_Config_SetAutosaveEnabled(autoOn);
    }
    ImGui::SameLine(180);
    int sec = (int)(Port_QuickSave_AutoIntervalMs() / 1000u);
    if (ImGui::SliderInt("Interval (s)", &sec, 5, 600)) {
        Port_QuickSave_SetAutoIntervalMs((unsigned)sec * 1000u);
        Port_Config_SetAutosaveIntervalMs((unsigned)sec * 1000u);
    }
    {
        bool areaOn = Port_QuickSave_AutoOnAreaChangeEnabled() != 0;
        if (ImGui::Checkbox("Auto-save on area change", &areaOn)) {
            Port_QuickSave_SetAutoOnAreaChange(areaOn ? 1 : 0);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted("Fires a snapshot to the auto ring "
                                   "every time you transition between "
                                   "areas/rooms. Independent of the "
                                   "interval timer above.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ImGui::Separator();

    /* Slot grid: each slot is one row with Save / Load buttons + timestamp. */
    const int n = Port_QuickSave_SlotCount();
    const int autoBase = Port_QuickSave_AutoSlotBase();
    if (ImGui::BeginTable("##quicksaves_table", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthStretch);

        for (int s = 0; s < n; ++s) {
            ImGui::PushID(s);
            ImGui::TableNextRow();

            // Column 1: Slot name
            ImGui::TableSetColumnIndex(0);
            const char* tag;
            char tagbuf[16];
            if (s == 0)
                tag = "Quick";
            else if (s < autoBase) {
                std::snprintf(tagbuf, sizeof(tagbuf), "Slot %d", s);
                tag = tagbuf;
            } else {
                std::snprintf(tagbuf, sizeof(tagbuf), "Auto %d", s - autoBase + 1);
                tag = tagbuf;
            }
            ImGui::Text("%s", tag);

            // Column 2: Actions
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Save")) {
                if (Port_QuickSave_SaveSlot(s))
                    Port_DebugMenu_ToastFromExternal("Saved");
            }
            ImGui::SameLine();
            if (Port_QuickSave_HasSlot(s)) {
                if (ImGui::Button("Load")) {
                    if (Port_QuickSave_LoadSlot(s))
                        Port_DebugMenu_ToastFromExternal("Loaded");
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("Load");
                ImGui::EndDisabled();
            }

            // Column 3: Timestamp
            ImGui::TableSetColumnIndex(2);
            unsigned long long ts = Port_QuickSave_SlotTimestamp(s);
            if (ts == 0) {
                ImGui::TextDisabled("(empty)");
            } else {
                time_t tt = (time_t)ts;
                struct tm tm_buf;
#ifdef _WIN32
                localtime_s(&tm_buf, &tt);
#else
                localtime_r(&tt, &tm_buf);
#endif
                char timestr[32];
                std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm_buf);
                ImGui::TextDisabled("%s", timestr);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    (void)DrawRegionLanguageControls(false);
}

static void DrawRibbonProfilesTab(void) {
    char names[32][64];
    const int n = Port_Save_ListProfiles(names, 32);
    const std::string activeNow = Port_Save_GetActivePath();

    ImGui::Text("Active profile: %s", activeNow.c_str());
    ImGui::Separator();

    /* Rename buffer keyed by index, so each row has its own inline
     * editor that survives across frames while the user is typing. */
    static char sRenameBuf[32][64] = {};
    static int sRenameRow = -1;
    static int sConfirmDeleteRow = -1;

    if (ImGui::BeginTable("##profiles_table", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Profile File", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);

        for (int i = 0; i < n; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            // Column 1: Profile Name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", names[i]);

            // Column 2: Status
            ImGui::TableSetColumnIndex(1);
            bool isActive = (std::string(names[i]) == activeNow);
            if (isActive) {
                ImGui::TextColored(ImVec4(1.0f, 0.94f, 0.25f, 1.0f), "active");
            } else {
                ImGui::TextDisabled("-");
            }

            // Column 3: Actions
            ImGui::TableSetColumnIndex(2);
            if (!isActive) {
                if (ImGui::Button("Activate")) {
                    if (Port_Save_SetActivePath(names[i])) {
                        Port_Config_SetActiveSaveProfile(names[i]);
                        Port_DebugMenu_ToastFromExternal("Profile activated - go to title to load");
                    } else {
                        Port_DebugMenu_ToastFromExternal("Profile switch failed - pending save was not written");
                    }
                }
                ImGui::SameLine();
            }
            const bool isDefault = (std::strcmp(names[i], "tmc.sav") == 0);
            if (!isDefault) {
                if (ImGui::Button("Rename")) {
                    sRenameRow = i;
                    snprintf(sRenameBuf[i], sizeof(sRenameBuf[i]), "%s", names[i]);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                    sConfirmDeleteRow = i;
            }

            if (sRenameRow == i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushItemWidth(160);
                ImGui::InputText("##rename", sRenameBuf[i], sizeof(sRenameBuf[i]));
                ImGui::PopItemWidth();
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button("OK")) {
                    if (Port_Save_RenameProfile(names[i], sRenameBuf[i])) {
                        Port_DebugMenu_ToastFromExternal("Profile renamed");
                    } else {
                        Port_DebugMenu_ToastFromExternal("Rename refused (clash / bad name / default)");
                    }
                    sRenameRow = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("X"))
                    sRenameRow = -1;
            }
            if (sConfirmDeleteRow == i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "Delete %s?", names[i]);
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button("Confirm delete")) {
                    if (Port_Save_DeleteProfile(names[i])) {
                        Port_DebugMenu_ToastFromExternal("Profile deleted");
                    } else {
                        Port_DebugMenu_ToastFromExternal("Delete refused (active / bad name)");
                    }
                    sConfirmDeleteRow = -1;
                    sRenameRow = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    sConfirmDeleteRow = -1;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::Button("+ Save current as new profile")) {
        char name[64];
        int k = 1;
        for (; k <= 99; ++k) {
            std::snprintf(name, sizeof(name), "tmc_%d.sav", k);
            FILE* probe = std::fopen(name, "rb");
            if (!probe)
                break;
            std::fclose(probe);
        }
        if (k > 99)
            Port_DebugMenu_ToastFromExternal("No free profile slots (1-99)");
        else if (Port_Save_SaveAsProfile(name)) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "Saved current as %s", name);
            Port_DebugMenu_ToastFromExternal(msg);
        } else {
            Port_DebugMenu_ToastFromExternal("Save failed");
        }
    }
}

/* Friendly display name for each action — matches the GBA button names
 * users actually think in. The Port_Config side stores them as
 * short ids ("a", "b", "soft_l2") for config.json compactness. */
static const char* InputLabel(int input) {
    switch (input) {
        case PORT_INPUT_A:
            return "A button (action)";
        case PORT_INPUT_B:
            return "B button (sword)";
        case PORT_INPUT_SELECT:
            return "Select";
        case PORT_INPUT_START:
            return "Start (pause)";
        case PORT_INPUT_RIGHT:
            return "D-pad Right";
        case PORT_INPUT_LEFT:
            return "D-pad Left";
        case PORT_INPUT_UP:
            return "D-pad Up";
        case PORT_INPUT_DOWN:
            return "D-pad Down";
        case PORT_INPUT_R:
            return "R (item slot 2)";
        case PORT_INPUT_L:
            return "L (item slot 1)";
        case PORT_INPUT_SOFT_X:
            return "Soft slot X";
        case PORT_INPUT_SOFT_Y:
            return "Soft slot Y";
        case PORT_INPUT_SOFT_L2:
            return "Soft slot L2";
        case PORT_INPUT_SOFT_R2:
            return "Soft slot R2";
        case PORT_INPUT_ROLL_ATTACK:
            return "Roll attack (D / R3)";
        default:
            return Port_Config_InputName((PortInput)input);
    }
}

static void DrawRibbonControlsTab(void) {
    if (ImGui::CollapsingHeader("Keyboard shortcuts")) {
        struct HotkeyRow {
            const char* key;
            const char* action;
        };
        static const HotkeyRow kHotkeys[] = {
            { "F8", "Open / close this settings menu (gamepad: Select+Start)" },
            { "F5 / F6", "Quicksave / quickload" },
            { "F1-F4", "Load save-state slot 1-4  (Shift+Fn = save to slot)" },
            { "F7", "Toggle text-to-speech" },
            { "F9", "Capture a bug report (screenshot + save + state)" },
            { "F10", "Speak nearby points of interest  (Shift: next, Ctrl: orient)" },
            { "F11 / Alt+Enter", "Toggle fullscreen" },
            { "F12", "Cycle the display filter / smoothing" },
            { "Tab (hold)", "Fast-forward" },
            { "[  ]", "Practice: set / reload practice point" },
            { "P  .", "Practice: pause / frame-advance while paused" },
            { "'  ;", "Practice: reset timer / record split" },
        };
        if (ImGui::BeginTable("##hotkeys", 2,
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            for (const HotkeyRow& row : kHotkeys) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s", row.key);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.action);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Save-states (F1-F6) are disabled in Console-Parity mode.");
    }
#ifdef __ANDROID__
    if (ImGui::CollapsingHeader("Touch controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        {
            int scheme = (Port_Config_TouchScheme() == PORT_TOUCH_SCHEME_DPAD) ? 1 : 0;
            const char* items[] = { "Joystick (floating)", "D-pad" };
            if (ImGui::Combo("Movement", &scheme, items, 2)) {
                Port_Config_SetTouchScheme(scheme == 1 ? PORT_TOUCH_SCHEME_DPAD : PORT_TOUCH_SCHEME_JOYSTICK);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Joystick: touch anywhere on the lower-left of the screen to "
                                  "plant the stick there.\nD-pad: fixed four-way pad.");
            }
        }
        {
            float v = Port_Config_TouchScale();
            if (ImGui::SliderFloat("Button size", &v, 0.6f, 1.6f, "%.2fx")) {
                Port_Config_SetTouchScale(v);
            }
        }
        {
            float v = Port_Config_TouchOpacity();
            if (ImGui::SliderFloat("Overlay opacity", &v, 0.3f, 1.5f, "%.2fx")) {
                Port_Config_SetTouchOpacity(v);
            }
        }
        ImGui::TextDisabled("The R button glows green when it has an action (talk, read, lift...).");
    }
    ImGui::Separator();
#endif
    {
        bool on = Port_Config_GetRollAttackMacroEnabled();
        if (ImGui::Checkbox("Roll attack macro", &on)) {
            Port_Config_SetRollAttackMacroEnabled(on);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Hold a direction and press the Roll attack bind to perform a "
                              "start-of-roll attack with your best sword, regardless of A/B "
                              "equip.\nDefault: keyboard D, controller R3 (right stick click).");
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped("Click 'Set' to replace an action's binding, or 'Add' to bind an extra "
                       "key/controller button to it, then press the input. Esc cancels. Mappings "
                       "save to config.json automatically. In Console-Parity mode each physical "
                       "input maps to only one action.");
    ImGui::Separator();

    /* Two-column-ish table: action label | bindings + buttons. */
    if (ImGui::BeginTable("##controls", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 180.0f);

        for (int i = 0; i < PORT_INPUT_COUNT; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(InputLabel(i));

            ImGui::TableSetColumnIndex(1);
            const int n = Port_Config_BindingCount((PortInput)i);
            if (n == 0) {
                ImGui::TextDisabled("(unbound)");
            } else {
                for (int b = 0; b < n; ++b) {
                    char label[64];
                    Port_Config_BindingLabel((PortInput)i, b, label, sizeof(label));
                    if (b > 0)
                        ImGui::SameLine(0, 6);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.30f, 0.45f, 1.0f));
                    ImGui::Button(label);
                    ImGui::PopStyleColor();
                }
            }

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Set")) {
                Port_Config_BeginCaptureBinding((PortInput)i);
                ImGui::OpenPopup("Capture binding");
            }
            ImGui::SameLine();
            if (ImGui::Button("Add")) {
                Port_Config_BeginAddBinding((PortInput)i);
                ImGui::OpenPopup("Capture binding");
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                Port_Config_ClearBindings((PortInput)i);
            }

            /* Modal popup that hangs around until the capture path
             * commits (which clears IsCapturingBinding). We render the
             * popup per-row but OpenPopup is fine because only one is
             * open at a time. */
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Capture binding", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::Text("Press a key or controller button for:");
                ImGui::TextColored(ImVec4(1, 0.94f, 0.25f, 1), "%s", InputLabel(i));
                ImGui::Text("Esc cancels.");
                ImGui::Separator();
                if (ImGui::Button("Cancel") || !Port_Config_IsCapturingBinding()) {
                    Port_Config_CancelCaptureBinding();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.30f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.40f, 0.30f, 1.0f));
    if (ImGui::Button("Reset all to defaults")) {
        Port_Config_ResetAllBindings();
        Port_DebugMenu_ToastFromExternal("Bindings reset");
    }
    ImGui::PopStyleColor(2);
}

/* The < / > soft-slot assignment cycler, shared by the Equip tab and the
 * in-game soft-slot config overlay (caller draws the surrounding row). */
static void DrawSoftSlotCycleButtons(int slot) {
    if (ImGui::Button("<"))
        Port_SoftSlots_CycleAssignment(slot, -1);
    ImGui::SameLine();
    if (ImGui::Button(">"))
        Port_SoftSlots_CycleAssignment(slot, +1);
}

static void DrawRibbonEquipTab(void) {
    if (ImGui::BeginTable("##equip_table", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Assigned Item", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Cycle", ImGuiTableColumnFlags_WidthStretch);

        for (int s = 0; s < 4; ++s) {
            ImGui::PushID(s);
            ImGui::TableNextRow();

            // Column 1: Slot name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", Port_SoftSlots_SlotName(s));

            // Column 2: Assigned item name
            ImGui::TableSetColumnIndex(1);
            const char* label = Port_SoftSlots_GetSlotLabel(s);
            const char* colon = std::strchr(label, ':');
            const char* item_name = colon ? colon + 1 : label;
            while (*item_name == ' ')
                ++item_name;
            ImGui::Text("%s", item_name);

            // Column 3: Buttons
            ImGui::TableSetColumnIndex(2);
            DrawSoftSlotCycleButtons(s);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static char sWarpFilter[64] = "";

/* Override lookup from port_debug_actions.c — returns 1 + fills x/y/layer
 * when (area, room) has a curated safe-spawn entry, else 0. Used by the
 * Warp tab so high-traffic rooms whose geometric center is a wall (boss
 * arenas, dungeon entrances, town buildings) drop Link on walkable
 * ground instead of an obstacle. See issue #94. */
/* Returns 1 if (area) is safe to warp to (has a friendly name and is
 * not on the known-broken deny-list). Same predicate the dispatch
 * layer uses, so the UI list and the action layer agree on what's
 * warpable. See port_debug_actions.c::kBrokenWarpAreas. */

static void DrawRibbonWarpTab(void) {
    /* Filter bar — type to narrow the area list. Empty filter = show
     * everything. Case-insensitive substring match. Steam Deck users
     * can ignore the filter and just scroll. */
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##warpFilter", "filter by area name (e.g. 'castle')", sWarpFilter, sizeof(sWarpFilter));
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        sWarpFilter[0] = '\0';
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("L/R bumpers = page jump");

    /* Free-coordinate teleport within the CURRENT room. Pre-fills from Link's
     * live position; in-game only. (All primitives already exist - this is the
     * same write WarpTick does.) */
    ImGui::Separator();
    {
        static int sTeleX = 0, sTeleY = 0;
        unsigned short px = 0, py = 0;
        const bool inGame = Port_DebugQuery_PlayerXY(&px, &py) != 0;
        ImGui::TextUnformatted("Teleport (current room):");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("X##tele", &sTeleX, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Y##tele", &sTeleY, 0);
        ImGui::BeginDisabled(!inGame);
        ImGui::SameLine();
        if (ImGui::Button("Go##tele")) {
            unsigned short tx = (unsigned short)(sTeleX < 0 ? 0 : sTeleX);
            unsigned short ty = (unsigned short)(sTeleY < 0 ? 0 : sTeleY);
            if (Port_DebugAction_TeleportXY(tx, ty)) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Teleport -> (%u, %u)", tx, ty);
                Port_DebugMenu_ToastFromExternal(msg);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Link's pos")) {
            sTeleX = px;
            sTeleY = py;
        }
        ImGui::EndDisabled();
        if (!inGame) {
            ImGui::SameLine();
            ImGui::TextDisabled("(in-game only)");
        }
    }
    {
        bool noclip = Port_DebugQuery_Noclip() != 0;
        if (ImGui::Checkbox("Noclip (walk through walls)", &noclip)) {
            Port_DebugAction_SetNoclip(noclip ? 1 : 0);
        }
        if (Port_Config_GetConsoleParity()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(disabled in Console-Parity)");
        }
    }
    {
        unsigned short px = 0, py = 0;
        const bool inGame = Port_DebugQuery_PlayerXY(&px, &py) != 0;
        const bool isMinish = inGame && (Port_DebugQuery_IsMinish() != 0);
        ImGui::BeginDisabled(!inGame);
        if (ImGui::Button(isMinish ? "Grow to Normal Size" : "Shrink to Minish")) {
            Port_DebugAction_ToggleMinish();
        }
        ImGui::EndDisabled();
        if (!inGame) {
            ImGui::SameLine();
            ImGui::TextDisabled("(in-game only)");
        }
    }
    ImGui::Separator();

    /* Letter strip — issue #76. The area list is long (>140 entries)
     * and previously the only way to traverse was one-line-at-a-time
     * scrolling. Buttons here filter to areas whose name starts with
     * that letter, giving instant A-Z jumps. */
    static char sLetterFilter = 0; /* 0 = no letter filter */
    {
        const char* kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        if (ImGui::SmallButton("All##warpLet"))
            sLetterFilter = 0;
        for (const char* p = kLetters; *p; ++p) {
            ImGui::SameLine();
            char id[6];
            std::snprintf(id, sizeof(id), "%c##wl", *p);
            if (sLetterFilter == *p) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.30f, 1.0f));
                if (ImGui::SmallButton(id))
                    sLetterFilter = 0;
                ImGui::PopStyleColor();
            } else {
                if (ImGui::SmallButton(id))
                    sLetterFilter = *p;
            }
        }
    }

    /* Scrollable area list. Each area is a collapsible header that
     * reveals its rooms as a button grid when opened. Headers are
     * controller-navigable (D-pad up/down) and the inner buttons take
     * keyboard / gamepad nav too. */
    ImGui::Separator();
    const float listH = ImGui::GetContentRegionAvail().y - 4.0f;
    if (ImGui::BeginChild("##warpList", ImVec2(0, listH), ImGuiChildFlags_NavFlattened, 0)) {
        /* L1/R1 bumpers = page jump while this child is focused/hovered.
         * PgUp / PgDn keys give the same shortcut on keyboard. Home /
         * End jump to the ends of the list. Issue #76. */
        const bool listFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                                 ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        if (listFocused) {
            const float pageStep = listH * 0.9f;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
                ImGui::SetScrollY(ImGui::GetScrollY() - pageStep);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
                ImGui::SetScrollY(ImGui::GetScrollY() + pageStep);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                ImGui::SetScrollY(0.0f);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
            }
        }
        const std::string filter = sWarpFilter[0] ? std::string(sWarpFilter) : std::string();
        std::string filter_lower = filter;
        for (auto& c : filter_lower)
            c = (char)std::tolower((unsigned char)c);

        int shown = 0;
        for (unsigned int area = 0; area < 0x90; ++area) {
            unsigned char a = (unsigned char)area;
            int roomCount = Port_DebugQuery_AreaRoomCount(a);
            if (roomCount <= 0)
                continue;

            const char* name = Port_DebugQuery_AreaName(a);
            /* Skip areas that aren't warpable: no friendly name (AREA_
             * NULL_*, numeric AREA_40-style slots) OR named-but-known-
             * broken (Simon's Sim, etc.). Same predicate the dispatch
             * layer uses so list + action stay in sync. See issue #94. */
            if (!Port_DebugAction_AreaIsWarpable(a))
                continue;
            char header[96];
            std::snprintf(header, sizeof(header), "0x%02X  %s  (%d rooms)", area, name, roomCount);

            if (!filter_lower.empty()) {
                std::string hl(header);
                for (auto& c : hl)
                    c = (char)std::tolower((unsigned char)c);
                if (hl.find(filter_lower) == std::string::npos)
                    continue;
            }
            /* Letter strip — match against the first letter of the
             * area name itself (skip the "0xNN  " prefix). */
            if (sLetterFilter && name) {
                char first = name[0];
                if (first >= 'a' && first <= 'z')
                    first = (char)(first - 'a' + 'A');
                if (first != sLetterFilter)
                    continue;
            }
            shown++;

            ImGui::PushID((int)area);
            if (ImGui::CollapsingHeader(header)) {
                ImGui::Indent();
                /* Per-room buttons in a 3-column grid for compactness. Room 0
                 * is simply the first grid entry below — there is no separate
                 * "Warp here (room 0)" button, which duplicated it (v0.6). */
                int col = 0;
                for (int r = 0; r < roomCount; ++r) {
                    unsigned short w = 0, h = 0;
                    if (!Port_DebugQuery_RoomDimensions(a, (unsigned char)r, &w, &h))
                        continue;
                    char roomLabel[48];
                    std::snprintf(roomLabel, sizeof(roomLabel), "Room 0x%02X", r);
                    ImGui::PushID(r);
                    if (ImGui::Button(roomLabel, ImVec2(120, 0))) {
                        unsigned short cx = 0, cy = 0;
                        unsigned char layer = 1;
                        if (!Port_DebugAction_WarpSpawnOverride(a, (unsigned char)r, &cx, &cy, &layer)) {
                            cx = w ? (unsigned short)(w / 2) : 0x80;
                            cy = h ? (unsigned short)(h / 2) : 0x80;
                            layer = 1;
                        }
                        if (Port_DebugAction_Warp(a, (unsigned char)r, cx, cy, layer)) {
                            char msg[96];
                            std::snprintf(msg, sizeof(msg), "Warp -> 0x%02X room 0x%02X", area, r);
                            Port_DebugMenu_ToastFromExternal(msg);
                        } else {
                            Port_DebugMenu_ToastFromExternal("Warp ignored: not in gameplay");
                        }
                    }
                    ImGui::PopID();
                    if ((col % 3) != 2)
                        ImGui::SameLine();
                    col++;
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (shown == 0) {
            ImGui::TextDisabled("No areas match the filter.");
        }
    }
    ImGui::EndChild();
}

static void DrawRibbonRandomizerTab(void) {
    ImGui::TextUnformatted("This option is not available yet.");
}

extern "C" {
typedef enum {
    AUDIO_MUTE_EZLO_VOICE,
    AUDIO_MUTE_NPC_VOICE,
    AUDIO_MUTE_LOW_HEALTH_BEEP,
    AUDIO_MUTE_COUNT,
} AudioMuteCategory;
bool Port_AudioMute_IsEnabled(AudioMuteCategory c);
void Port_AudioMute_SetEnabled(AudioMuteCategory c, bool on);
const char* Port_AudioMute_Label(AudioMuteCategory c);
const char* Port_AudioMute_Description(AudioMuteCategory c);
}

static void DrawRibbonAudioTab(void) {
    /* Master volume - a basic level control, active in both accurate and
     * enhanced modes (default 100% leaves the mix unchanged). */
    {
        float vol = Port_Audio_GetMasterVolume() * 100.0f;
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("Master volume", &vol, 0.0f, 100.0f, "%.0f%%")) {
            float v = vol / 100.0f;
            Port_Audio_SetMasterVolume(v);
            Port_Config_SetMasterVolume(v);
        }
        HelpTooltip("Scales the final mixed game audio. 100% = unchanged. Persists "
                            "across launches. Leave at 100% for a faithful level match when "
                            "A/B-testing against hardware in GBA-accurate mode.");
        ImGui::Separator();
    }

    bool gbaAccurate = Port_Audio_IsGbaAccurate();
    if (ImGui::Checkbox("GBA-accurate audio", &gbaAccurate)) {
        Port_Audio_SetGbaAccurate(gbaAccurate);
    }
    HelpTooltip("On: NEAREST resampling (the hardware's no-interpolation "
                        "sample-and-hold 'crunch') and the output is handed straight to "
                        "the device with no post-process DSP - for A/B comparison against "
                        "real hardware / mGBA.\n\n"
                        "Off (default): SINC resampling plus the DC-blocker / low-pass / "
                        "stereo-widen / soft-clip chain tuned for modern speakers.");

    ImGui::Separator();

    /* Enhancement sliders — only meaningful while the enhanced (non-accurate)
       post-process chain is running, so grey them out in GBA-accurate mode. */
    ImGui::BeginDisabled(gbaAccurate);

    if (ImGui::BeginTable("##audio_enhancements", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);

        // Stereo width
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Stereo width");
        ImGui::TableSetColumnIndex(1);
        float width = Port_Audio_GetWidth();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##width", &width, 1.00f, 1.50f, "%.2f")) {
            Port_Audio_SetWidth(width);
        }
        HelpTooltip("Mid/side stereo widening. 1.00 = mono image (reference), "
                            "1.20 = default. The mid is never altered, so mono playback always "
                            "collapses cleanly to the original mix. Lower values (~1.12) reduce "
                            "hard-panned peak overshoot. No effect in GBA-accurate mode.");

        // Reverb
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Reverb");
        ImGui::TableSetColumnIndex(1);
        int reverb = Port_Audio_GetReverbLevel();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderInt("##reverb", &reverb, 0, 24)) {
            Port_Audio_SetReverbLevel(reverb);
        }
        HelpTooltip("Adds a short room tail to sampled (PCM) voices - drums, bass, "
                            "some leads - while the chiptune PSG/CGB voices stay dry by the "
                            "synth's mix order, so it adds space without muddying the melody. "
                            "0 = off (default). ~12 is a gentle, musical amount. Applies live "
                            "(does not restart the music). No effect in GBA-accurate mode.");

        ImGui::EndTable();
    }

    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::TextWrapped("Per-category SFX mutes. Each toggle suppresses "
                       "the matching sound IDs at the SoundReq / EnqueueSFX "
                       "entry points - music and other SFX are untouched.");
    ImGui::Separator();

    if (ImGui::BeginTable("##sfx_mutes", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);

        for (int i = 0; i < (int)AUDIO_MUTE_COUNT; ++i) {
            if ((i % 2) == 0)
                ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(i % 2);

            bool on = Port_AudioMute_IsEnabled((AudioMuteCategory)i);
            const char* label = Port_AudioMute_Label((AudioMuteCategory)i);
            const char* desc = Port_AudioMute_Description((AudioMuteCategory)i);
            if (ImGui::Checkbox(label, &on)) {
                Port_AudioMute_SetEnabled((AudioMuteCategory)i, on);
            }
            if (desc && desc[0]) {
                HelpTooltip(desc);
            }
        }
        ImGui::EndTable();
    }
}

static void DrawRibbonAccessibilityTab(void) {
    Port_TTS_Init(); /* idempotent — safe if main.c already initialised */
    const char* backendName = Port_TTS_GetBackendName();

    ImGui::TextWrapped("Text-to-speech reads important UI labels aloud (focused "
                       "buttons, dialogs, errors). Toggle off at any time. Default "
                       "off; settings persist across launches.");
    ImGui::Separator();

    if (ImGui::BeginTable("##tts_table", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);

        // Backend row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Backend");
        ImGui::TableSetColumnIndex(1);
        if (backendName) {
            ImGui::TextUnformatted(backendName);
            if (std::strcmp(backendName, "NVDA") == 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(rate/pitch/volume ignored - NVDA controls those)");
            }
        } else {
            ImGui::TextDisabled("(unavailable - install spd-say / espeak-ng on Linux)");
        }

        // Enable Row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Enable TTS");
        ImGui::TableSetColumnIndex(1);
        bool on = Port_TTS_GetEnabled();
        if (ImGui::Checkbox("##enable_tts", &on)) {
            Port_TTS_SetEnabled(on);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(F7 toggles, F6 stops speech)");

        // Rate
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Rate");
        ImGui::TableSetColumnIndex(1);
        float rate = Port_TTS_GetRate();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##rate", &rate, 0.0f, 1.0f, "%.2f")) {
            Port_TTS_SetRate(rate);
        }

        // Pitch
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Pitch");
        ImGui::TableSetColumnIndex(1);
        float pitch = Port_TTS_GetPitch();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##pitch", &pitch, 0.0f, 1.0f, "%.2f")) {
            Port_TTS_SetPitch(pitch);
        }

        // Volume
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Volume");
        ImGui::TableSetColumnIndex(1);
        float volume = Port_TTS_GetVolume();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##volume", &volume, 0.0f, 1.0f, "%.2f")) {
            Port_TTS_SetVolume(volume);
        }

        // Voice
        static char voiceBuf[128];
        static char langBuf[32];
        static bool inited = false;
        if (!inited) {
            const char* v = Port_TTS_GetVoice();
            const char* l = Port_TTS_GetLanguage();
            std::strncpy(voiceBuf, v ? v : "", sizeof(voiceBuf) - 1);
            std::strncpy(langBuf, l ? l : "", sizeof(langBuf) - 1);
            inited = true;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Voice");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##voice", voiceBuf, sizeof(voiceBuf))) {
            Port_TTS_SetVoice(voiceBuf);
        }

        // Language
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Language");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputText("##lang", langBuf, sizeof(langBuf))) {
            Port_TTS_SetLanguage(langBuf);
        }

        ImGui::EndTable();
    }
    ImGui::TextDisabled("Voice IDs vary by backend (espeak: 'en+f2', say: 'Samantha', SAPI: 'Microsoft David').");

    ImGui::Separator();
    if (ImGui::Button("Test voice")) {
        PortTtsOptions o = {};
        o.rate = o.pitch = o.volume = 0.0f / 0.0f;
        o.dedupe = false;
        Port_TTS_Speak("This is the Project Picori text-to-speech test. "
                       "If you hear this, T T S is wired up.",
                       &o);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        Port_TTS_Stop();
    }
    ImGui::SameLine();
    if (ImGui::Button("Read focus")) {
        Port_TTS_Speak("Focus reader test. The focused control reads aloud as you Tab.", nullptr);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Navigation cues (for blind / low-vision players). On-demand keys "
                       "in game: F10 scans nearby points of interest (chests, items, NPCs, "
                       "animals, enemies, exits); Shift+F10 steps through them one at a "
                       "time; Ctrl+F10 reads the surface under you, walls around you, and "
                       "exits.");
    if (ImGui::Button("Scan surroundings (F10)")) {
        Port_A11y_ScanSurroundings();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cycle (Shift+F10)")) {
        Port_A11y_CycleNext();
    }
    ImGui::SameLine();
    if (ImGui::Button("Look around (Ctrl+F10)")) {
        Port_A11y_LookAround();
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Passive cues play automatically as you move: a tonal enemy radar "
                       "(stereo pan = direction, pitch = distance), footstep sounds tinted "
                       "by surface, fall-hazard warnings, and wall bumps.");
    {
        bool b;
        b = Port_A11y_GetPassiveEnabled();
        if (ImGui::Checkbox("Passive cues", &b))
            Port_A11y_SetPassiveEnabled(b);
        b = Port_A11y_GetFootstepsEnabled();
        if (ImGui::Checkbox("Footsteps", &b))
            Port_A11y_SetFootstepsEnabled(b);
        ImGui::SameLine();
        b = Port_A11y_GetHazardsEnabled();
        if (ImGui::Checkbox("Hazards", &b))
            Port_A11y_SetHazardsEnabled(b);
        ImGui::SameLine();
        b = Port_A11y_GetRadarEnabled();
        if (ImGui::Checkbox("Enemy radar", &b))
            Port_A11y_SetRadarEnabled(b);
        ImGui::SameLine();
        b = Port_A11y_GetWallsEnabled();
        if (ImGui::Checkbox("Walls", &b))
            Port_A11y_SetWallsEnabled(b);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Manual test plan:\n"
                       "  1. Enable above, click Test voice - hear the test line.\n"
                       "  2. Tab through this tab's controls - each label announces.\n"
                       "  3. F7 toggles TTS without opening the menu.\n"
                       "  4. F6 stops mid-utterance.\n"
                       "  5. Open a save-overwrite dialog - modal is announced.");
}

static void DrawRibbonRebornTab(void) {
    ImGui::TextWrapped("Quality-of-life features ported from Minish Cap Reborn "
                       "(GPL-3.0); see THIRD-PARTY-LICENSES.md. Toggles persist "
                       "until tmc_pc closes.");
    ImGui::Separator();
    for (int i = 0; i < REBORN_FEAT_COUNT; ++i) {
        /* Slot 8 (rupee-like overhaul) was removed; its enum slot is kept so
         * the persisted feature bitmask (issue #146) stays stable, but it has
         * no behaviour and is hidden from this tab. */
        if (i == REBORN_FEAT_RUPEE_LIKE_OVERHAUL)
            continue;
        bool on = Port_Reborn_IsEnabled((RebornFeature)i);
        const char* label = Port_Reborn_FeatureLabel((RebornFeature)i);
        const char* desc = Port_Reborn_FeatureDescription((RebornFeature)i);
        if (ImGui::Checkbox(label, &on)) {
            Port_Reborn_SetEnabled((RebornFeature)i, on);
        }
        if (desc && desc[0]) {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(360.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }

    /* Deadzone tuning for the 360° analog feature above — only meaningful
     * while it's enabled, so the slider appears indented underneath it. */
    if (Port_Reborn_IsEnabled(REBORN_FEAT_ANALOG_360_MOVEMENT)) {
        ImGui::Indent(20.0f);
        float dz = Port_Config_GetAnalogDeadzone();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat("Analog deadzone", &dz, 0.0f, 0.95f, "%.2f")) {
            Port_Config_SetAnalogDeadzone(dz);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted("Left-stick displacement below this fraction is ignored, so the "
                                   "D-pad stays authoritative and a thumb resting on the stick won't "
                                   "drift. Raise it for a worn/drifty stick; lower it for a lighter "
                                   "touch. Default 0.30.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::Unindent(20.0f);
    }
}

/* Defined alongside DrawPracticeOverlay below; used here in the ribbon tab. */
static void Practice_FormatFrames(unsigned long long frames, char* out, size_t cap);

static void DrawRibbonPracticeTab(void) {
    ImGui::TextWrapped("Speedrun practice tools. Overlays draw over the game "
                       "whenever their toggle is on (independent of this menu).");
    ImGui::Separator();

    ImGui::SeparatorText("Overlays");
    bool t = Port_Config_GetPracticeShowTimer();
    if (ImGui::Checkbox("Show IGT timer", &t))
        Port_Config_SetPracticeShowTimer(t);
    bool in = Port_Config_GetPracticeShowInputs();
    if (ImGui::Checkbox("Show input display", &in))
        Port_Config_SetPracticeShowInputs(in);
    bool h = Port_Config_GetPracticeShowHistory();
    if (ImGui::Checkbox("Show input history", &h))
        Port_Config_SetPracticeShowHistory(h);

    ImGui::SeparatorText("Timer");
    char buf[32];
    Practice_FormatFrames(Port_Practice_ElapsedFrames(), buf, sizeof(buf));
    ImGui::Text("Elapsed: %s  (%llu frames)", buf, (unsigned long long)Port_Practice_ElapsedFrames());
    if (ImGui::Button(Port_Practice_TimerRunning() ? "Stop" : "Start"))
        Port_Practice_TimerToggle();
    ImGui::SameLine();
    if (ImGui::Button("Reset timer"))
        Port_Practice_TimerReset();
    ImGui::SameLine();
    if (ImGui::Button("Split"))
        Port_Practice_AddSplit();

    int nsplits = Port_Practice_SplitCount();
    if (nsplits > 0) {
        ImGui::SameLine();
        if (ImGui::Button("Clear splits"))
            Port_Practice_ClearSplits();
        if (ImGui::BeginTable("##splits", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("Time");
            ImGui::TableSetupColumn("Delta");
            ImGui::TableHeadersRow();
            unsigned long long prev = 0;
            for (int i = 0; i < nsplits; ++i) {
                unsigned long long f = Port_Practice_SplitAt(i);
                char tbuf[32], dbuf[32];
                Practice_FormatFrames(f, tbuf, sizeof(tbuf));
                Practice_FormatFrames(f - prev, dbuf, sizeof(dbuf));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", i + 1);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(tbuf);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("+%s", dbuf);
                prev = f;
            }
            ImGui::EndTable();
        }
    }

    ImGui::SeparatorText("Practice point");
    if (ImGui::Button("Set point")) {
        if (Port_Practice_SetPoint())
            Port_DebugMenu_ToastFromExternal("Practice point set");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!Port_Practice_HasPoint());
    if (ImGui::Button("Reload point")) {
        Port_DebugMenu_ToastFromExternal(Port_Practice_LoadPoint() ? "Practice point loaded" : "Reload failed");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(Port_Practice_HasPoint() ? "(set)" : "(empty)");

    ImGui::SeparatorText("Speed");
    float sm = Port_Config_GetPracticeSlowmo();
    if (ImGui::SliderFloat("Slow-mo", &sm, 0.1f, 1.0f, "%.2fx")) {
        Port_Config_SetPracticeSlowmo(sm);
    }
    ImGui::SameLine();
    if (ImGui::Button("1x"))
        Port_Config_SetPracticeSlowmo(1.0f);
    if (ImGui::Button(Port_Practice_IsPaused() ? "Resume" : "Pause"))
        Port_Practice_TogglePause();

    ImGui::SeparatorText("Hotkeys");
    ImGui::TextDisabled("Keyboard:  [ set point   ] reload   P pause   . frame-advance   ' reset   ; split\n"
                        "Gamepad:   hold Select + A reload / B set / X pause / Y advance / D-Up reset / D-Down split");
}

/* Read-only entity viewer (#feature). Snapshots all live entities each frame
 * via the recycled-node-safe walk in port_debug_entities.c and lists them in a
 * scrollable table (clipped, so a full 72-entity room is cheap). */
static void DrawRibbonEntitiesTab(void) {
    const int n = Port_DebugQuery_RefreshEntities();
    ImGui::Text("Live entities: %d", n);
    ImGui::SameLine();
    ImGui::TextDisabled("(snapshot, refreshed each frame)");
    if (ImGui::BeginTable("##entities", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0, 320))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("List");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("id");
        ImGui::TableSetupColumn("type");
        ImGui::TableSetupColumn("x, y");
        ImGui::TableSetupColumn("hp");
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(n);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PortEntityInfo* e = Port_DebugQuery_Entity(i);
                if (!e)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", e->listIndex);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Port_DebugQuery_EntityKindName(e->kind));
                ImGui::TableNextColumn();
                ImGui::Text("0x%02X", (unsigned)e->id);
                ImGui::TableNextColumn();
                ImGui::Text("0x%02X", (unsigned)e->type);
                ImGui::TableNextColumn();
                ImGui::Text("%d, %d", e->x, e->y);
                ImGui::TableNextColumn();
                ImGui::Text("%u", (unsigned)e->health);
            }
        }
        ImGui::EndTable();
    }
}

/* Live memory-watch tab (#feature). Type a GBA address + width, "Add watch",
 * and each entry shows its value live every frame. Reads are fault-safe
 * (Port_DebugQuery_MemRead), so an unmapped address renders "<unmapped>"
 * rather than faulting. Session-only — watches are intentionally not persisted
 * (a stale address from a previous build would be misleading). */
static void DrawRibbonMemoryTab(void) {
    ImGui::TextUnformatted("Watch arbitrary GBA memory live: EWRAM 0x02xxxxxx, IWRAM 0x03xxxxxx, "
                           "I/O 0x04xxxxxx, palette 0x05xxxxxx, VRAM 0x06xxxxxx, OAM 0x07xxxxxx, ROM 0x08xxxxxx.");
    ImGui::TextDisabled("Session-only: watches are not saved to config.");
    ImGui::Separator();

    static char sAddrBuf[16] = "03000000";
    static int sWidth = 0; /* 0=u8 1=u16 2=u32 */
    static const char* const kWidthNames[3] = { "u8", "u16", "u32" };

    ImGui::SetNextItemWidth(110);
    ImGui::InputText("##memaddr", sAddrBuf, sizeof(sAddrBuf),
                     ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    ImGui::Combo("##memwidth", &sWidth, kWidthNames, 3);
    ImGui::SameLine();

    const unsigned int candAddr = (unsigned int)strtoul(sAddrBuf, nullptr, 16);
    unsigned int candVal = 0;
    const int candOk = Port_DebugQuery_MemRead(candAddr, sWidth, &candVal);
    if (ImGui::Button("Add watch")) {
        if (Port_DebugAction_MemWatchAdd(candAddr, sWidth) < 0) {
            Port_DebugMenu_ToastFromExternal("Watch list full (32 max)");
        }
    }
    ImGui::SameLine();
    if (candOk) {
        ImGui::Text("= 0x%0*X  (%u)", (1 << sWidth) * 2, candVal, candVal);
    } else {
        ImGui::TextDisabled("= <unmapped>");
    }

    ImGui::Separator();
    const int n = Port_DebugQuery_MemWatchCount();
    ImGui::Text("Watches: %d / 32", n);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear all"))
        Port_DebugAction_MemWatchClear();

    if (ImGui::BeginTable("##memwatch", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0, 280))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Width");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        int removeIdx = -1;
        for (int i = 0; i < n; ++i) {
            const unsigned int a = Port_DebugQuery_MemWatchAddr(i);
            const int w = Port_DebugQuery_MemWatchWidth(i);
            unsigned int v = 0;
            const int ok = Port_DebugQuery_MemRead(a, w, &v);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", a);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Port_DebugQuery_MemWidthName(w));
            ImGui::TableNextColumn();
            if (ok)
                ImGui::Text("0x%0*X  (%u)", (1 << w) * 2, v, v);
            else
                ImGui::TextDisabled("<unmapped>");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
                removeIdx = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (removeIdx >= 0)
            Port_DebugAction_MemWatchRemove(removeIdx);
    }
}

static void DrawRibbon(void) {
    ImGuiIO& io = ImGui::GetIO();
    const float ribbonW = io.DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ribbonW, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##ribbon", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse)) {
        /* Close button anchored to the top-right of the ribbon. The
         * persistent corner trigger sits behind the ribbon when it's
         * open, so without this button users on mouse-only or who
         * forgot the F8/Select+Start hotkey have no way out. Render
         * it BEFORE the tab bar so it sits at the very top edge. */
        const float closeW = 80.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - closeW - 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.30f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.35f, 0.35f, 1.0f));
        if (ImGui::Button("Close X", ImVec2(closeW, 0))) {
            Port_DebugMenu_Toggle();
        }
        ImGui::PopStyleColor(3);

        if (ImGui::BeginTabBar("##ribbonTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Items")) {
                DrawRibbonItemsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Display")) {
                DrawRibbonDisplayTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Saves")) {
                DrawRibbonSavesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Profiles")) {
                DrawRibbonProfilesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Equip")) {
                DrawRibbonEquipTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Controls")) {
                DrawRibbonControlsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Warp")) {
                DrawRibbonWarpTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Entities")) {
                DrawRibbonEntitiesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Flags")) {
                DrawRibbonFlagsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Memory")) {
                DrawRibbonMemoryTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Randomizer")) {
                DrawRibbonRandomizerTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Audio")) {
                DrawRibbonAudioTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Accessibility")) {
                DrawRibbonAccessibilityTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Reborn")) {
                DrawRibbonRebornTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Practice")) {
                DrawRibbonPracticeTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        /* Footer with the mode toggle + hotkey hint. */
        ImGui::Separator();
        bool useRibbon = sRibbonEnabled;
        if (ImGui::Checkbox("Ribbon mode (uncheck for classic menu)", &useRibbon)) {
            sRibbonEnabled = useRibbon;
            Port_Config_SetRibbonEnabled(useRibbon); /* persist (#146) */
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(F8 or Select+Start also toggles)");
        ImGui::TextDisabled("F5/F6 quicksave/load   F9 bug report   -   see the Controls tab for all hotkeys");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* Layout helpers — keep all the styling decisions in one place so it's
 * easy to tweak the look without hunting through draw code. */
static void DrawToast(const char* text) {
    if (!text || !*text)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 vpSize = io.DisplaySize;
    const float pad = 12.0f;
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.94f, 0.25f, 1.0f));
    ImGui::SetNextWindowPos(ImVec2(vpSize.x * 0.5f, vpSize.y - pad - 24.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (ImGui::Begin("##toast", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoInputs)) {
        ImGui::TextUnformatted(text);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

/* ---- Speedrun practice overlay ---------------------------------------- *
 * Non-interactive (NoInputs) HUD drawn every frame, independent of the F8
 * menu, gated by the practice_* config toggles. Timer top-centre; input
 * display + rolling history bottom-centre. All state from port_practice.c. */

static void Practice_FormatFrames(unsigned long long frames, char* out, size_t cap) {
    unsigned long long totalMs = frames * 1000ull / 60ull; /* 60 fps IGT */
    unsigned ms = (unsigned)(totalMs % 1000);
    unsigned long long totalS = totalMs / 1000;
    unsigned s = (unsigned)(totalS % 60);
    unsigned m = (unsigned)(totalS / 60);
    snprintf(out, cap, "%u:%02u.%03u", m, s, ms);
}

/* Button rows shared by the held-glyph line and the history grid. */
static const struct {
    int bit;
    const char* name;
} kPracticeBtns[] = {
    { PORT_INPUT_A, "A" },      { PORT_INPUT_B, "B" },       { PORT_INPUT_L, "L" },    { PORT_INPUT_R, "R" },
    { PORT_INPUT_UP, "^" },     { PORT_INPUT_DOWN, "v" },    { PORT_INPUT_LEFT, "<" }, { PORT_INPUT_RIGHT, ">" },
    { PORT_INPUT_START, "St" }, { PORT_INPUT_SELECT, "Se" },
};
static const int kPracticeBtnCount = (int)(sizeof(kPracticeBtns) / sizeof(kPracticeBtns[0]));

static void Practice_DrawHeldGlyphs(unsigned short mask) {
    for (int i = 0; i < kPracticeBtnCount; ++i) {
        bool on = (mask & (unsigned short)(1u << kPracticeBtns[i].bit)) != 0;
        ImVec4 col = on ? ImVec4(0.30f, 0.85f, 0.45f, 1.0f) : ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        ImGui::TextColored(col, "%s", kPracticeBtns[i].name);
        if (i != kPracticeBtnCount - 1)
            ImGui::SameLine();
    }
}

static void Practice_DrawHistory(void) {
    const int cols = 60; /* ~1 second of frames */
    const float cw = 4.0f, ch = 9.0f, labelW = 16.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    for (int r = 0; r < kPracticeBtnCount; ++r) {
        float y = origin.y + r * ch;
        dl->AddText(ImVec2(origin.x, y), IM_COL32(180, 180, 180, 255), kPracticeBtns[r].name);
        for (int c = 0; c < cols; ++c) {
            /* Rightmost column (c=cols-1) is the newest sample (history idx 0). */
            unsigned short m = Port_Practice_HistoryAt(cols - 1 - c);
            if (m & (unsigned short)(1u << kPracticeBtns[r].bit)) {
                float x = origin.x + labelW + c * cw;
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw - 1.0f, y + ch - 1.0f), IM_COL32(80, 200, 120, 255));
            }
        }
    }
    ImGui::Dummy(ImVec2(labelW + cols * cw, kPracticeBtnCount * ch));
}

/* ---- FPS counter overlay ----------------------------------------------
 * Top-right HUD gated by show_fps. Under decoupled pacing render rate and
 * game speed are separate numbers, so both are shown: FPS is what the
 * display gets, TPS is how fast the game is actually running (60 = correct
 * speed regardless of the FPS cap). Rates refresh once per second in
 * port_bios.c. */
extern "C" {
extern double gPortPaceFps;
extern double gPortPaceTps;
extern bool gPortPaceDecoupled;
}

static void DrawFpsOverlay(void) {
    if (!Port_Config_GetShowFps())
        return;

    /* Foreground draw list: on top of every ImGui window (incl. the F8
     * menu), MangoHud-style, and costs no window/focus bookkeeping. */
    char fpsTxt[24], tpsTxt[24];
    snprintf(fpsTxt, sizeof(fpsTxt), "%.0f FPS", gPortPaceFps);
    snprintf(tpsTxt, sizeof(tpsTxt), " / %.0f TPS", gPortPaceTps);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float pad = 10.0f;
    const float inset = 5.0f;
    ImVec2 fpsSz = ImGui::CalcTextSize(fpsTxt);
    ImVec2 tpsSz = gPortPaceDecoupled ? ImGui::CalcTextSize(tpsTxt) : ImVec2(0, 0);
    ImVec2 boxMax = ImVec2(io.DisplaySize.x - pad, pad + fpsSz.y + inset * 2);
    ImVec2 boxMin = ImVec2(boxMax.x - (fpsSz.x + tpsSz.x + inset * 2), pad);
    dl->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 150), 4.0f);
    ImVec2 cur = ImVec2(boxMin.x + inset, boxMin.y + inset);
    dl->AddText(cur, IM_COL32(90, 230, 115, 255), fpsTxt);
    if (gPortPaceDecoupled) {
        cur.x += fpsSz.x;
        /* Game speed: yellow at the correct rate (60, or 59.73 parity),
         * red when it deviates (overloaded machine or fast-forward). */
        bool nominal = gPortPaceTps > 58.0 && gPortPaceTps < 62.0;
        ImU32 col = nominal ? IM_COL32(255, 240, 76, 255) : IM_COL32(255, 115, 90, 255);
        dl->AddText(cur, col, tpsTxt);
    }
}

static void DrawPracticeOverlay(void) {
    const bool showTimer = Port_Config_GetPracticeShowTimer();
    const bool showInputs = Port_Config_GetPracticeShowInputs();
    const bool showHistory = Port_Config_GetPracticeShowHistory();
    if (!showTimer && !showInputs && !showHistory)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 vp = io.DisplaySize;
    const float pad = 10.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

    if (showTimer) {
        char buf[32];
        Practice_FormatFrames(Port_Practice_ElapsedFrames(), buf, sizeof(buf));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, pad), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        if (ImGui::Begin("##practice_timer", nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.94f, 0.30f, 1.0f));
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(%llu)", (unsigned long long)Port_Practice_ElapsedFrames());
            if (Port_Practice_IsPaused()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "PAUSED");
            } else if (!Port_Practice_TimerRunning()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "STOP");
            }
        }
        ImGui::End();
    }

    if (showInputs || showHistory) {
        ImGui::SetNextWindowBgAlpha(0.70f);
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y - pad), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        if (ImGui::Begin("##practice_inputs", nullptr, flags)) {
            if (showInputs)
                Practice_DrawHeldGlyphs(Port_Practice_CurrentInputMask());
            if (showHistory) {
                if (showInputs)
                    ImGui::Spacing();
                Practice_DrawHistory();
            }
        }
        ImGui::End();
    }
}

static void DrawMenuPage(int depth) {
    const char* title = Port_DebugMenu_PageTitle(depth);
    const int count = Port_DebugMenu_PageItemCount(depth);
    const int cursor = Port_DebugMenu_PageCursor(depth);
    if (!title || count <= 0)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const float panelW = 460.0f;
    const float maxH = io.DisplaySize.y * 0.85f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelW, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(panelW, 0), ImVec2(panelW, maxH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(panelW, 0));
    if (ImGui::Begin(title, nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginChild("##items", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 22.0f), false,
                              ImGuiWindowFlags_None)) {
            for (int i = 0; i < count; ++i) {
                const char* label = Port_DebugMenu_PageItemLabel(depth, i);
                if (!label)
                    continue;
                bool selected = i == cursor;

                /* Render as a Selectable so it gets a hover background.
                 * Spans available width so the hover hit-box reaches the
                 * right edge of the panel. */
                ImGui::PushID(i);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.94f, 0.25f, 1.0f));
                }
                if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    Port_DebugMenu_PageSetCursor(depth, i);
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        Port_DebugMenu_PageActivate(depth, i);
                    }
                }
                /* Right-click → cycle right (shortcut for value items). */
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                    Port_DebugMenu_PageSetCursor(depth, i);
                    Port_DebugMenu_PageCycleRight(depth, i);
                }
                if (selected) {
                    ImGui::PopStyleColor();
                    /* Keep the cursor row visible when keyboard nav
                     * scrolls past the edge of the child window. */
                    if (ImGui::GetScrollMaxY() > 0.0f) {
                        ImGui::SetScrollHereY(0.5f);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextUnformatted("Up/Dn move  Enter activate  L/R cycle  Esc back");
        ImGui::TextUnformatted("Double-click activate  Right-click cycle");
        if (depth == 0)
            ImGui::TextUnformatted("F5/F6 quicksave/load   F9 bug report   (Controls tab: all keys)");
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* Persistent click target so users on mouse/touch can open the menu
 * without the F8 hotkey. When the menu is closed we render the
 * smallest, faintest possible affordance — a single "≡" glyph in the
 * top-right — so gameplay isn't covered. The window auto-opacifies on
 * hover. When the menu IS open, the same widget switches to a clear
 * "CLOSE" label since at that point the menu UI already obscures the
 * background, so visibility is fine. */
static void DrawMenuTrigger(void) {
    ImGuiIO& io = ImGui::GetIO();
    const bool open = Port_DebugMenu_IsOpen();
    /* One-shot discovery hint: mark it seen the moment the menu is first
     * opened by ANY path (F8, gamepad Select+Start, or this button), so it
     * never nags a returning player again. */
    if (open && !Port_Config_GetMenuHintSeen()) {
        Port_Config_SetMenuHintSeen(true);
    }
    const bool showHint = !open && !Port_Config_GetMenuHintSeen();
    const float pad = 6.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, pad), ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    /* Closed: 12% alpha background, ~minimal padding, single-glyph label —
     * so the trigger reads as a faint corner dot rather than an opaque UI
     * element overlapping the player's eye-line. First run (showHint): draw
     * it boldly with a spelled-out label + the F8 key so a new player learns
     * the settings door exists. */
    if (open) {
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
    } else if (showHint) {
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.55f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.45f, 0.65f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
    } else {
        ImGui::SetNextWindowBgAlpha(0.12f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        /* Make the closed-state button itself low-alpha too; ImGui's
         * hover state will bump it on its own when the cursor lands. */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.28f, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.45f, 0.65f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.92f, 0.92f, 0.50f));
    }

    /* NoNavInputs + NoNavFocus keep gamepad/keyboard nav from ever
     * targeting this button, so A on the controller can't accidentally
     * open the menu during gameplay. Mouse/touch click still works. */
    if (ImGui::Begin("##menu_trigger", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavInputs |
                         ImGuiWindowFlags_NoNavFocus)) {
        /* Closed: single triple-bar ASCII '=' stacked into a hamburger
         * shape (the default ImGui font doesn't ship U+2261 ≡). First run:
         * spelled-out "Settings (F8)". Open: clear close label. */
        const char* label = open ? " CLOSE MENU " : (showHint ? " Settings  (F8) " : "[=]");
        if (ImGui::Button(label)) {
            Port_DebugMenu_Toggle();
        }
        if (open) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted("Gamepad: D-pad nav   A activate   B back");
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
    if (open) {
        ImGui::PopStyleVar();
    } else {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }
}

/* Quit-save confirm modal state. The X-button (SDL_EVENT_QUIT) routes
 * through Port_ImGui_RequestQuitModal which arms this flag instead of
 * exiting straight away. The user picks Save & Quit / Quit Without
 * Saving / Cancel. A static "armed" flag survives across frames until
 * the user makes a choice — the modal can't ride a one-shot bool
 * because ImGui::BeginPopupModal needs to be called every frame while
 * it's open. */
static bool sQuitModalArmed = false;
static bool sQuitModalConfirmed = false; /* set to true on "Save & Quit" or "Quit" — main loop polls and exits */
extern "C" bool Port_ImGui_QuitConfirmed(void) {
    return sQuitModalConfirmed;
}
extern "C" void Port_ImGui_RequestQuitModal(void) {
    /* If a previous confirm already fired, honour it and let the host
     * exit. This catches the rare double-click on the X button. */
    if (sQuitModalConfirmed)
        return;
    sQuitModalArmed = true;
}

static void DrawQuitModal(void) {
    if (sQuitModalArmed) {
        ImGui::OpenPopup("Quit?");
        sQuitModalArmed = false;
    }
    /* Centre the popup. */
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted("Save before quitting?");
        ImGui::Separator();
        ImGui::TextWrapped("Save & Quit writes the current game state to "
                           "quicksave slot 0 (F6 to reload). Quit Without "
                           "Saving exits immediately - any progress since "
                           "your last in-game save is lost.");
        ImGui::Spacing();
        if (ImGui::Button("Save & Quit", ImVec2(140, 0))) {
            Port_QuickSave_SaveSlot(0);
            sQuitModalConfirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit Without Saving", ImVec2(180, 0))) {
            sQuitModalConfirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

extern "C" bool Port_ImGui_Render(void) {
    if (!sImGuiInited)
        return false;
    /* SDL_Renderer path needs a renderer; SDL_GPU path runs with
     * sRenderer == nullptr (NewFrame uses ImGui_ImplSDLGPU3 instead
     * and PresentFrame consumes the draw data via the *_Gpu helpers
     * below). */
#ifndef TMC_GPU_RENDERER
    if (!sRenderer)
        return false;
#endif


    static bool sPrevMenuOpen = false;
    const bool menuOpen = Port_DebugMenu_IsOpen();
    const bool navWanted = menuOpen;
    {
        ImGuiIO& io = ImGui::GetIO();
        if (navWanted) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        }
    }

#ifdef TMC_GPU_RENDERER
    const bool gpuBackend = (sRenderer == nullptr);
    if (gpuBackend) {
        ImGui_ImplSDLGPU3_NewFrame();
    } else
#endif
    {
        ImGui_ImplSDLRenderer3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    /* Defensive cleanup on close-transition (open → closed). Without
     * this, ImGui can retain nav focus / active-widget references to
     * ribbon widgets that won't be drawn on the very next frame —
     * causing intermittent crashes when the menu is closed via gamepad
     * (Select+Start) while a widget is focused or being edited. Force-
     * release window focus and any pending popups so the next render
     * starts from a clean state. Safe to call between NewFrame and the
     * first Begin. */
    if (sPrevMenuOpen && !menuOpen) {
        /* Release any window focus so ImGui's nav state doesn't keep a
         * dangling reference to a ribbon widget. Calling with nullptr
         * is the documented "no window focused" path. */
        ImGui::SetWindowFocus(nullptr);
    }
    sPrevMenuOpen = menuOpen;

    /* Soft-slot config overlay — replaces the SDL_Renderer-only popup
     * from port_softslots.c with an ImGui equivalent so it works on
     * both backends (the GPU path has no SDL_Renderer to draw the
     * legacy version into). Centered modal-style window; closes via
     * Enter/Escape, which Port_SoftSlots_HandleConfigKey already
     * handles independently. */
    extern bool Port_SoftSlots_ConfigIsOpen(void);
    extern const char* Port_SoftSlots_GetSlotLabel(int slot);
    extern void Port_SoftSlots_CycleAssignment(int slot, int direction);
    extern void Port_SoftSlots_ConfigClose(void);
    if (Port_SoftSlots_ConfigIsOpen()) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(380, 0));
        if (ImGui::Begin("##softslot_config", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextColored(ImVec4(0.78f, 0.86f, 1.0f, 1.0f), "EXTRA EQUIP SLOTS");
            ImGui::Separator();
            for (int s = 0; s < 4; ++s) {
                ImGui::PushID(s);
                ImGui::Text("%s", Port_SoftSlots_GetSlotLabel(s));
                ImGui::SameLine(260.0f);
                DrawSoftSlotCycleButtons(s);
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::TextDisabled("Up/Down pick   Left/Right cycle   Enter/Esc done");
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                Port_SoftSlots_ConfigClose();
            }
        }
        ImGui::End();
    }

    /* Toast survives the menu being closed (e.g. after a warp). */
    DrawToast(Port_DebugMenu_Toast());

    /* The click-to-open trigger, so mouse/touch users have a way in without
     * the F8 hotkey. It rides the touch-overlay switch: it exists for people
     * with no keyboard, and someone who has turned the on-screen controls
     * off wants the game screen clear of overlay furniture, this corner
     * included. F8 and the gamepad chord still open the menu, and on a
     * handheld the second screen's settings tab switches it back on. */
    if (Port_Config_GetTouchControls() && Port_Config_PortSettingsMenuEnabled()) {
        DrawMenuTrigger();
    }

    /* Quit-save confirm modal — only renders when armed by
     * Port_ImGui_RequestQuitModal (called from port_bios.c when SDL
     * reports SDL_EVENT_QUIT). Independent of the F8 ribbon state so
     * the user gets a chance to save even with the menu closed. */
    DrawQuitModal();

    if (Port_DebugMenu_IsOpen()) {
        if (sRibbonEnabled) {
            DrawRibbon();
        } else {
            /* Render the deepest page only (legacy behaviour: submenu
             * hides its parent). */
            int depth = Port_DebugMenu_PageDepth() - 1;
            if (depth >= 0) {
                DrawMenuPage(depth);
            }
            /* Classic mode has no ribbon footer, so without this it would be a
             * one-way trap. Offer an explicit way back to ribbon mode. */
            ImGui::SetNextWindowBgAlpha(0.85f);
            if (ImGui::Begin("##classic_to_ribbon", nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings)) {
                if (ImGui::SmallButton("Switch to ribbon mode")) {
                    sRibbonEnabled = true;
                    Port_Config_SetRibbonEnabled(true); /* persist (#146) */
                }
            }
            ImGui::End();
        }
    }

    /* Future-friendly: per-frame focus reader hook. ImGui doesn't
     * expose a label-string from the focus ID (labels are hashed
     * into IDs at widget time) so the per-tab handlers call
     * Port_TTS_OnFocusChanged manually for each row when they want
     * announcements. This block is intentionally left empty for
     * now — keep the slot reserved next to Render() so future
     * work that DOES carry labels through DataID has an obvious
     * place to plug in. */

    DrawPracticeOverlay();
    DrawFpsOverlay();
    ImGui::Render();
#ifdef TMC_GPU_RENDERER
    if (gpuBackend) {
        /* GPU path: draw_data lives in ImGui's per-frame state until the
         * GPU PresentFrame consumes it via Port_ImGui_RenderDrawDataGpu.
         * We don't call PrepareDrawData here — that needs the cmd buffer
         * from the GPU side. Return true so the caller knows a frame's
         * worth of ImGui work is queued. */
        return true;
    }
#endif
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sRenderer);
    return true;
}

#ifdef TMC_GPU_RENDERER
/* Stage 2: called from Port_GPU_PresentFrame to inject the F8 menu into
 * the same render pass that draws the game framebuffer. Splits the
 * usual one-call render into two halves — PrepareDrawData uploads
 * vertex/index buffers (must happen before BeginGPURenderPass), and
 * RenderDrawData issues the actual draw commands inside the pass. */
extern "C" void Port_ImGui_PrepareDrawDataGpu(SDL_GPUCommandBuffer* cmd) {
    if (!sImGuiInited)
        return;
    if (sRenderer != nullptr)
        return; /* SDL_Renderer path doesn't use this */
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd)
        return;
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
}

extern "C" void Port_ImGui_RenderDrawDataGpu(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* rp) {
    if (!sImGuiInited)
        return;
    if (sRenderer != nullptr)
        return;
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd)
        return;
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp, /*pipeline=*/nullptr);
}
#endif

/* Project Picori prelaunch — builds and presents a centred ImGui
 * card with embedded logo, title / subtitle, version, ROM filename,
 * and Play / Change-ROM buttons. Returns false if ImGui isn't ready
 * (caller falls back to the plain boot splash).
 *
 * Button presses are reported through the out_play / out_change_rom
 * pointers (caller may pass NULL to ignore). On the SDL_Renderer
 * backend, presents the frame inline. On the SDL_GPU backend, builds
 * + Render()s the draw data and returns true — the caller must follow
 * up with Port_GPU_PresentPrelaunchFrame() to present it. */
/* First-launch asset-extraction progress screen for the SDL_GPU backend.
 * The SDL_Renderer path draws DrawProgressScreen (port_asset_bootstrap.cpp);
 * GPU builds have no SDL_Renderer, so they previously extracted with no UI
 * and the window looked hung. This builds + renders one ImGui frame (same
 * NewFrame structure as the prelaunch card) so it can be presented on the GPU
 * swapchain via Port_GPU_PresentPrelaunchFrame. Returns true when draw data
 * is ready to present. `fraction` is 0..1; `phase` is the current phase name. */
extern "C" bool Port_ImGui_RenderExtractProgress(const char* phase, float fraction, int phase_index, int phase_total) {
    if (!sImGuiInited)
        return false;

#ifdef TMC_GPU_RENDERER
    const bool gpuBackend = (sRenderer == nullptr);
    if (gpuBackend) {
        ImGui_ImplSDLGPU3_NewFrame();
    } else
#endif
    {
        ImGui_ImplSDLRenderer3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 center(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    if (ImGui::Begin("##extract_progress", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.72f, 0.46f, 1.00f));
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted("Extracting game assets");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 6));
        float frac = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
        ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.78f, 0.70f, 1.00f));
        ImGui::Text("loading %s   (phase %d/%d)", (phase && phase[0]) ? phase : "preparing", phase_index, phase_total);
        ImGui::PopStyleColor();
#ifdef __ANDROID__
        ImGui::TextDisabled("One-time setup - this can take a minute on first launch.");
#else
        ImGui::TextDisabled("One-time first-launch extraction. See terminal for detail.");
#endif
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();
    return true;
}

/* Horizontally center a single line of text in the current window. */
static void CenteredText(const char* t) {
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(t).x) * 0.5f);
    ImGui::TextUnformatted(t);
}

extern "C" bool Port_ImGui_RenderPrelaunch(bool rom_present, const char* version, const char* rom_name, bool* out_play,
                                           bool* out_change_rom) {
    if (out_play)
        *out_play = false;
    if (out_change_rom)
        *out_change_rom = false;
    if (!sImGuiInited)
        return false;

#ifdef TMC_GPU_RENDERER
    const bool gpuBackend = (sRenderer == nullptr);
    if (gpuBackend) {
        ImGui_ImplSDLGPU3_NewFrame();
    } else
#endif
    {
        ImGui_ImplSDLRenderer3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 viewport_center(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(viewport_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);
    /* Cap the card's auto-height to the visible work area so a small or
     * default-sized window never pushes the Select ROM / Play buttons
     * off-screen; with the scrollbar enabled (below) they stay reachable
     * without having to resize the window first (v0.6 oversight). */
    ImGui::SetNextWindowSizeConstraints(ImVec2(620, 0.0f), ImVec2(620, vp->WorkSize.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36, 32));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    if (ImGui::Begin("##prelaunch", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        const float win_w = ImGui::GetWindowSize().x;
        const ImVec4 accent(0.40f, 0.72f, 0.46f, 1.00f);
        const ImVec4 subtxt(0.70f, 0.78f, 0.70f, 1.00f);

        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::SetWindowFontScale(2.4f);
        CenteredText("PROJECT PICORI");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
        CenteredText("Minish Cap PC Port");
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 16));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 14));

        if (rom_present) {
            ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
            ImGui::TextUnformatted("Version");
            ImGui::PopStyleColor();
            ImGui::SameLine(170.0f);
            ImGui::TextUnformatted(version ? version : "?");

            ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
            ImGui::TextUnformatted("ROM");
            ImGui::PopStyleColor();
            ImGui::SameLine(170.0f);
            ImGui::TextUnformatted(rom_name ? rom_name : "?");
            ImGui::SameLine();
            /* Right-align the Change-ROM button to the edge of the card. */
            {
                const char* lbl = "Change ROM...";
                float bw = ImGui::CalcTextSize(lbl).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                float pad = ImGui::GetStyle().WindowPadding.x;
                ImGui::SameLine(win_w - pad - bw);
                if (ImGui::Button(lbl)) {
                    if (out_change_rom)
                        *out_change_rom = true;
                }
            }
        } else {
            /* First-launch / missing-ROM state: dominate the card with a
             * "Select your Minish Cap ROM" prompt + big button. No Play
             * yet — there's nothing to play. */
            ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
            CenteredText("No ROM found.");
            CenteredText("Project Picori needs your own Minish Cap dump (.gba).");
            CenteredText("We identify it by SHA-1 - filename is irrelevant.");
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, 14));
        (void)DrawRegionLanguageControls(true);

        ImGui::Dummy(ImVec2(0, 22));

        /* Big centred action button: Play when a ROM is loaded, Select
         * ROM when none. Enter / Space activates whichever is shown. */
        {
            const bool is_select = !rom_present;
            const char* lbl = is_select ? "Select ROM..." : "Play";
            const ImVec2 sz(is_select ? 260.0f : 220.0f, 48.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.34f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.72f, 0.46f, 1.0f));
            ImGui::SetCursorPosX((win_w - sz.x) * 0.5f);
            ImGui::SetWindowFontScale(1.4f);
            const bool clicked = ImGui::Button(lbl, sz) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                                 ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Space);
            if (clicked) {
                if (is_select) {
                    if (out_change_rom)
                        *out_change_rom = true;
                } else {
                    if (out_play)
                        *out_play = true;
                }
            }
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        }

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
        CenteredText(rom_present ? "Press Enter or click Play to start"
                                 : "Press Enter or click to pick your .gba file");
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();

#ifdef TMC_GPU_RENDERER
    if (gpuBackend) {
        return true;
    }
#endif
    if (sRenderer) {
        SDL_SetRenderDrawColor(sRenderer, 15, 18, 18, 255);
        SDL_RenderClear(sRenderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sRenderer);
        SDL_RenderPresent(sRenderer);
    }
    return true;
}
