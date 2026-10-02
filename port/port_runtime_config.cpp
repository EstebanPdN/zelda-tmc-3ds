#include "port_runtime_config.h"
#include "port_touch_controls.h"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

struct Bind {
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_GamepadButton pad = SDL_GAMEPAD_BUTTON_INVALID;
    /* Triggers (L2/R2 on Xbox/PS pads) are reported by SDL as analog axes,
     * not buttons, so the bind table allows binding to an axis. A value
     * past kAxisThreshold counts as "pressed". */
    SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;
};

constexpr Sint16 kAxisThreshold = 16384;

struct Def {
    PortInput input;
    const char* name;
    std::initializer_list<const char*> binds;
};

const std::array<Def, PORT_INPUT_COUNT> kDefaults = { {
    { PORT_INPUT_A, "a", { "SDLK:0x00000078", "SDL_GAMEPAD:0x00000000" } },
    { PORT_INPUT_B, "b", { "SDLK:0x0000007a", "SDL_GAMEPAD:0x00000001" } },
    { PORT_INPUT_SELECT, "select", { "SDLK:0x00000008", "SDL_GAMEPAD:0x00000004" } },
    { PORT_INPUT_START, "start", { "SDLK:0x0000000d", "SDL_GAMEPAD:0x00000006" } },
    { PORT_INPUT_RIGHT, "right", { "SDLK:0x4000004f", "SDL_GAMEPAD:0x0000000e" } },
    { PORT_INPUT_LEFT, "left", { "SDLK:0x40000050", "SDL_GAMEPAD:0x0000000d" } },
    { PORT_INPUT_UP, "up", { "SDLK:0x40000052", "SDL_GAMEPAD:0x0000000b" } },
    { PORT_INPUT_DOWN, "down", { "SDLK:0x40000051", "SDL_GAMEPAD:0x0000000c" } },
    { PORT_INPUT_R, "r", { "SDLK:0x00000073", "SDL_GAMEPAD:0x0000000a" } },
    { PORT_INPUT_L, "l", { "SDLK:0x00000061", "SDL_GAMEPAD:0x00000009" } },
    /* Soft-slots: keyboard CV/QE + face-buttons WEST/NORTH + triggers L2/R2.
     * Numeric values: SDL_GAMEPAD_BUTTON_WEST=2, NORTH=3,
     * SDL_GAMEPAD_AXIS_LEFT_TRIGGER=4, RIGHT_TRIGGER=5. */
    { PORT_INPUT_SOFT_X, "soft_x", { "SDLK:0x00000063", "SDL_GAMEPAD:0x00000002" } },
    { PORT_INPUT_SOFT_Y, "soft_y", { "SDLK:0x00000076", "SDL_GAMEPAD:0x00000003" } },
    { PORT_INPUT_SOFT_L2, "soft_l2", { "SDLK:0x00000071", "SDL_AXIS:0x00000004" } },
    { PORT_INPUT_SOFT_R2, "soft_r2", { "SDLK:0x00000065", "SDL_AXIS:0x00000005" } },
    /* Roll attack: keyboard D + R3 (right stick click). R3 is unused by the
     * base game and sits near the movement stick for a direction+macro press. */
    { PORT_INPUT_ROLL_ATTACK, "roll_attack", { "SDLK:0x00000064", "SDL_GAMEPAD:0x00000008" } },
} };

u8 sScale = 3;
u8 sInternalScale = 1;
std::string sUpscaleMethod = "nearest";
u64 sFrameTimeNs = 1000000000ULL / 60; /* 60 FPS cap by default (VSync-effective); see kDefaultFrameTimeNs */
bool sPortSettingsMenuEnabled = true;
/* Persisted active save-profile filename. Defaults to tmc.sav. The
 * port_save.c EEPROM emulation reads/writes this path; the F8 debug
 * menu's "Save profiles" page switches it. */
std::string sActiveSaveProfile = "tmc.sav";
/* Persisted auto-save settings. On by default — protects new players
 * from losing progress after a crash, since most TMC saves rely on
 * the in-game tetra statue / file-save flow that requires getting
 * back to a save point first. */
bool sAutosaveEnabled = true;
u32 sAutosaveIntervalMs = 60000;
/* Touch input scheme from matheo's launcher integration — kept for
 * Android compatibility. */
PortTouchScheme sTouchScheme = PORT_TOUCH_SCHEME_JOYSTICK;
float sTouchScale = 1.0f;   /* multiplies the touch layout unit  */
float sTouchOpacity = 1.0f; /* multiplies every control's alpha  */
/* Widescreen reveal default-ON everywhere. It draws up to ~60% more
 * scanline pixels (384x160 vs 240x160), which is why this was once off for
 * Android — but the width tracks the window's aspect rather than always
 * running the full 384 (Port_Widescreen_TargetViewWidth), so a 16:9 handheld
 * pays ~18% rather than 60%, and the dual-screen target chip carries it.
 * Anyone it does cost can turn it off in the panel's SETTINGS; the row is
 * hidden on native-240 builds, which ignore this flag entirely.
 * Console-Parity still forces it off. */
bool sWidescreenEnabled = true;
/* Console-Parity mode. When true the port suppresses every feature that
 * gives the player an edge over real GBA hardware, so a run is provably
 * console-equivalent: sub-frame input edge leniency off, save-states inert,
 * widescreen forced off, frame pacing locked to the authentic 59.7275 Hz.
 * Default false. Persisted as "console_parity"; CLI --console-parity forces
 * it on after config load. */
bool sConsoleParity = false;
/* Decoupled pacing (port_bios.c): game logic ticks at a fixed rate (60 Hz, or
 * GBA-exact under console parity) while the "Target FPS" cap paces only how
 * often frames are presented. Off = legacy behavior where the FPS cap paces
 * the engine directly, so a 120 FPS cap runs the game at 2x speed. */
bool sDecoupleRender = true;
/* On-screen FPS/TPS counter overlay (top-right HUD, port_imgui_menu.cpp). */
bool sShowFps = false;
/* The Android touch overlay (port_touch_controls.cpp). Off removes it from
 * the screen AND stops it consuming touches — a handheld with real buttons
 * wants neither the clutter over the game nor the dead zones under it. */
bool sTouchControls = true;
int sPreferredRegion = -1;
int sPreferredLanguage = -1;
/* Widescreen pillarbox config — applied in port_ppu.cpp's present path.
 * The frame fills as much of the window as its aspect allows; whatever the
 * frame can't cover (title screen, one-screen 240px rooms) defaults to the
 * blurred ambient fill so no scene shows dead black bars. "black" restores
 * the historical plain-bars look. */
PortAspectMode sAspectMode = PORT_ASPECT_NATIVE_3_2;
#ifdef __ANDROID__
PortBgFill sBgFill = PORT_BG_FILL_BLACK; /* "blurred" reads as glitchy duplicated tiles on real widescreen panels */
#else
PortBgFill sBgFill = PORT_BG_FILL_BLURRED_FRAME;
#endif
u8 sBgFillR = 0, sBgFillG = 0, sBgFillB = 0;
/* Renderer backend selection — read once at PPU init; menu writes
 * here but a restart is needed to apply because the window's
 * swapchain owner is set once and is not live-switchable. */
#ifdef __ANDROID__
/* "Auto" picks the SDL_GPU/Vulkan backend on capable Adreno GPUs (e.g. AYN
 * Thor), whose ImGui/swapchain path has an unresolved resize bug that leaves
 * everything past the pre-fullscreen window width unpainted. Force the
 * SDL_Renderer path until that's fixed upstream. */
PortRenderBackend sRenderBackend = PORT_RENDER_BACKEND_SOFTWARE;
#else
PortRenderBackend sRenderBackend = PORT_RENDER_BACKEND_AUTO;
#endif
bool sTtsEnabled = true;
float sTtsRate = 0.5f;
float sTtsPitch = 0.5f;
float sTtsVolume = 0.8f;
std::string sTtsVoice;
std::string sTtsLanguage;
bool sA11yCues = true;
bool sA11yFootsteps = true;
bool sA11yHazards = true;
bool sA11yRadar = true;
bool sA11yWalls = true;
/* Second-screen (AYN Thor bottom panel) map-behavior toggles, surfaced as
 * tappable rows on that screen's own settings tab. All default on. */
bool sSecondScreenFollowCam = true;
bool sSecondScreenCrestPins = true;
bool sSecondScreenFloorReturn = true;
/* Which dressing the panel paints behind everything (an SS_BACKDROP_*
 * style id). 0 = the pause menu's parchment, i.e. the original look. */
int sSecondScreenBackdrop = 0;
/* Swap the two screens: with this on, the Android shell launches the game
 * activity onto the secondary display and hosts the panel on the main one.
 * Read by the Java launcher (TMCLauncherActivity) straight out of
 * config.json before the game process gets a window, so it only takes
 * effect at the NEXT launch — SDL's Android backend syncs a window's
 * system-bar/immersive state from the flags of the SDL_CreateWindow call
 * that made it, so moving the game between displays live is not a thing
 * this port can do without recreating that window. Default off. */
bool sSecondScreenSwap = false;
/* Hide the game's own HUD on the top screen while the second screen shows
 * vitals — the engine-side gate lives with DrawUIElements. Default off. */
bool sHideTopHud = false;
/* Speedrun practice mode (port_practice.c). Overlays default off so normal
 * play stays uncluttered; slow-mo defaults to 1.0 (normal speed). */
bool sPracticeShowTimer = false;
bool sPracticeShowInputs = false;
bool sPracticeShowHistory = false;
float sPracticeSlowmo = 1.0f;
/* Runtime toggles that previously lived only in memory (issue #146): they
 * now persist to config.json and are re-applied at startup. */
bool sDiscordRpc = false;
bool sVSyncCfg = true; /* matches Port_PPU's sVSyncEnabled default */
/* GPU PPU rasterizer (docs/gpu-rasterizer-design.md): default on where the
 * SDL_GPU presentation path is active; the CPU rasterizer is the automatic
 * fallback. Ignored on SDL_Renderer / software builds. */
bool sGpuRaster = true;
/* GLES compute rasterizer: OPT-IN (default off). Measured ~5x slower than the
 * 3-thread CPU rasterizer on the Adreno 405 (Moto G4) — the archetypal
 * no-Vulkan device — so it must never auto-enable. Vulkan raster (gpu_raster)
 * stays default-on since it's a real win. Flip on to try GLES on a stronger
 * GLES-only GPU. */
bool sGpuRasterGles = false;
#ifdef __ANDROID__
bool sColorCorrect = false; /* Too heavy for low-end ARM by default */
#else
bool sColorCorrect = true; /* GBA-LCD colour correction (default on) */
#endif
bool sLcdPersist = false;      /* LCD temporal persistence (default off) */
float sLcdPersistRho = 0.35f;  /* persistence: fraction of prev frame kept */
bool sRibbonCfg = true;        /* F8 menu style: ribbon (true) vs classic */
bool sMenuHintSeen = false;    /* set once the F8/settings menu is first opened */
float sMasterVolume = 1.0f;    /* game master volume [0,1]; 1.0 = unchanged */
bool sHoldAdvanceText = false; /* hold an advance key to keep paging text */
bool sRollAttackMacroEnabled = true;
#ifdef __ANDROID__
bool sFullscreen = true; /* no windowed mode on a handheld; avoid a visible nav bar */
#else
bool sFullscreen = false;
#endif
bool sFullscreenHideCursor = true; /* hide the OS cursor while fullscreen */
float sAnalogDeadzone = 0.30f;     /* 360° stick deadzone magnitude [0..0.95] */
std::string sShaderPreset;         /* path to active .glslp, empty = none */
unsigned sRebornFeatures = 0;      /* bitmask of enabled Reborn features */
bool sHasRebornFeatures = false;   /* was the key present in config.json? */
std::array<std::vector<Bind>, PORT_INPUT_COUNT> sBinds;
/* Rebind capture state. -1 = not capturing; otherwise the PortInput
 * whose next key/button/axis press becomes a new binding. The ImGui
 * "Controls" tab toggles this and shows a modal "Press a key..." popup
 * while it's non-negative. */
int sCapturingInput = -1;
/* While capturing: true = append the new bind to this action's existing list
 * (multiple bindings per action); false = replace. Set by BeginAddBinding /
 * BeginCaptureBinding respectively. */
bool sCaptureAppend = false;
/* Edge-detection cache. Set when the corresponding SDL key/button event
 * arrives during the frame; cleared by Port_Config_ClearInputEdges()
 * after KEYINPUT is committed. Catches sub-frame taps (press+release
 * between two polls) that the polled-state path would otherwise miss
 * — useful for frame-perfect rolls / spin-attack inputs. */
std::array<bool, PORT_INPUT_COUNT> sEdgePressed{};
std::vector<SDL_Gamepad*> sPads;
std::filesystem::path sConfigPath = "config.json";
nlohmann::json sConfigJson;
const std::array<u32, 9> kFpsPresets = { 0, 30, 60, 75, 90, 120, 144, 150, 240 };
/* Default cap when config omits frame_time_ns (1e9 ns / 60 Hz). */
constexpr u64 kDefaultFrameTimeNs = 1000000000ULL / 60;
/* Authentic GBA frame period used in Console-Parity mode. The GBA draws
 * 228 scanlines * 1232 cycles = 280896 cycles/frame at a 2^24 Hz CPU clock,
 * i.e. 16777216/280896 = 59.7275 Hz -> 280896/16777216 s = 16742706 ns. */
constexpr u64 kGbaFrameTimeNs = 16742706ULL;

/* ----------------------------------------------------------------------
 * Single source of truth binding each config.json key to its typed static
 * and default. DefaultsJson() writes e.def into the json; apply() loads
 * json→static. Replaces the two formerly hand-synced key lists. Settings
 * with bespoke parse/typing (enum strings, bg_fill_color, frame_time_ns,
 * autosave_interval_ms, reborn_features, bindings) stay inline at their
 * sites — see DefaultsJson() and apply(). */
struct BoolCfg {
    const char* key;
    bool* var;
    bool def;
};
struct IntCfg {
    const char* key;
    int* var;
    int def;
};
struct StrCfg {
    const char* key;
    std::string* var;
    const char* def;
};
struct FloatCfg {
    const char* key;
    float* var;
    double def;
};
struct ScaleCfg {
    const char* key;
    u8* var;
    int def;
    int lo;
    int hi;
};

const BoolCfg kBoolCfg[] = {
    { "port_settings_menu", &sPortSettingsMenuEnabled, true },
    { "autosave_enabled", &sAutosaveEnabled, true },
    { "widescreen_enabled", &sWidescreenEnabled, true },
    { "console_parity", &sConsoleParity, false },
    { "decouple_render", &sDecoupleRender, true },
    { "show_fps", &sShowFps, false },
    { "touch_controls", &sTouchControls, true },
    { "tts_enabled", &sTtsEnabled, true },
    { "a11y_cues", &sA11yCues, false },
    { "a11y_footsteps", &sA11yFootsteps, false },
    { "a11y_hazards", &sA11yHazards, false },
    { "a11y_radar", &sA11yRadar, false },
    { "a11y_walls", &sA11yWalls, false },
    { "second_screen_follow_cam", &sSecondScreenFollowCam, true },
    { "second_screen_windcrest_pins", &sSecondScreenCrestPins, true },
    { "second_screen_floor_auto_return", &sSecondScreenFloorReturn, true },
    { "second_screen_swap", &sSecondScreenSwap, false },
    { "hide_top_hud", &sHideTopHud, false },
    { "practice_show_timer", &sPracticeShowTimer, false },
    { "practice_show_inputs", &sPracticeShowInputs, false },
    { "practice_show_history", &sPracticeShowHistory, false },
    { "discord_rpc", &sDiscordRpc, false },
    { "vsync", &sVSyncCfg, true },
    { "gpu_raster", &sGpuRaster, true },
    { "gpu_raster_gles", &sGpuRasterGles, false },
#ifdef __ANDROID__
    { "color_correction", &sColorCorrect, false },
#else
    { "color_correction", &sColorCorrect, true },
#endif
    { "lcd_persistence", &sLcdPersist, false },
    { "ribbon_mode", &sRibbonCfg, true },
    { "menu_hint_seen", &sMenuHintSeen, false },
    { "hold_advance_text", &sHoldAdvanceText, false },
    { "roll_attack_macro", &sRollAttackMacroEnabled, true },
#ifdef __ANDROID__
    { "fullscreen", &sFullscreen, true },
#else
    { "fullscreen", &sFullscreen, false },
#endif
    { "fullscreen_hide_cursor", &sFullscreenHideCursor, true },
};
const IntCfg kIntCfg[] = {
    { "preferred_region", &sPreferredRegion, -1 },      { "preferred_language", &sPreferredLanguage, -1 },
    { "second_screen_backdrop", &sSecondScreenBackdrop, 0 },
};
const StrCfg kStrCfg[] = {
    { "upscale_method", &sUpscaleMethod, "nearest" },
    { "active_save_profile", &sActiveSaveProfile, "tmc.sav" },
    { "tts_voice", &sTtsVoice, "" },
    { "tts_language", &sTtsLanguage, "" },
    { "shader_preset", &sShaderPreset, "" },
};
const FloatCfg kFloatCfg[] = {
    { "tts_rate", &sTtsRate, 0.5 },
    { "tts_pitch", &sTtsPitch, 0.5 },
    { "tts_volume", &sTtsVolume, 0.8 },
    { "practice_slowmo", &sPracticeSlowmo, 1.0 },
    { "lcd_persistence_rho", &sLcdPersistRho, 0.35 },
    { "master_volume", &sMasterVolume, 1.0 },
    { "analog_deadzone", &sAnalogDeadzone, 0.30 },
};
const ScaleCfg kScaleCfg[] = {
    { "window_scale", &sScale, 3, 1, 10 },
    { "internal_scale", &sInternalScale, 1, 1, 10 },
};

nlohmann::json DefaultsJson(void) {
    nlohmann::json j;
    for (const auto& e : kScaleCfg)
        j[e.key] = e.def;
    for (const auto& e : kIntCfg)
        j[e.key] = e.def;
    for (const auto& e : kBoolCfg)
        j[e.key] = e.def;
    for (const auto& e : kStrCfg)
        j[e.key] = e.def;
    for (const auto& e : kFloatCfg)
        j[e.key] = e.def;
    /* Bespoke-default keys (custom parse/typing in apply()). */
    j["frame_time_ns"] = kDefaultFrameTimeNs; /* 60 FPS cap; uncapped is unstable for frame-tied logic */
    j["autosave_interval_ms"] = 60000;
    j["touch_scheme"] = "joystick";
    j["aspect_mode"] = "native";
#ifdef __ANDROID__
    j["bg_fill"] = "black";
#else
    j["bg_fill"] = "blurred";
#endif
    j["bg_fill_color"] = { 0, 0, 0 };
#ifdef __ANDROID__
    j["render_backend"] = "software";
#else
    j["render_backend"] = "auto";
#endif
    /* reborn_features is intentionally absent — its presence is the signal
     * to override the compile-time feature defaults. */
    j["bindings"] = nlohmann::json::object();
    for (const auto& d : kDefaults) {
        j["bindings"][d.name] = nlohmann::json::array();
        for (const char* bind : d.binds) {
            j["bindings"][d.name].push_back(bind);
        }
    }
    return j;
}

void AddBind(PortInput input, const std::string& name) {
    Bind b;
    if (name.rfind("SDLK:", 0) == 0) {
        b.key = static_cast<SDL_Keycode>(std::strtoul(name.c_str() + 5, nullptr, 0));
        sBinds[input].push_back(b);
    } else if (name.rfind("SDL_GAMEPAD:", 0) == 0) {
        b.pad = static_cast<SDL_GamepadButton>(std::strtoul(name.c_str() + 12, nullptr, 0));
        sBinds[input].push_back(b);
    } else if (name.rfind("SDL_AXIS:", 0) == 0) {
        b.axis = static_cast<SDL_GamepadAxis>(std::strtoul(name.c_str() + 9, nullptr, 0));
        sBinds[input].push_back(b);
    }
}

void LoadBinds(PortInput input, const nlohmann::json& v) {
    if (v.is_string()) {
        AddBind(input, v.get<std::string>());
    } else if (v.is_array()) {
        for (const auto& it : v) {
            if (it.is_string()) {
                AddBind(input, it.get<std::string>());
            }
        }
    }
}

void SaveConfig(void) {
    try {
        std::ofstream(sConfigPath) << sConfigJson.dump(4) << '\n';
    } catch (...) {}
}

u64 FrameTimeForFps(u32 fps) {
    if (fps == 0) {
        return 0;
    }
    if (fps > 1000) {
        fps = 1000;
    }
    return 1000000000ULL / fps;
}

/* Serialize a Bind to the config.json token shape (`SDLK:0x...`,
 * `SDL_GAMEPAD:0x...`, `SDL_AXIS:0x...`). Shared by the capture/persist
 * path and (under launcher) the rebind writers. */
std::string FormatBindForJson(const Bind& b) {
    char buf[40];
    if (b.key != SDLK_UNKNOWN) {
        std::snprintf(buf, sizeof(buf), "SDLK:0x%08x", (unsigned)b.key);
    } else if (b.pad != SDL_GAMEPAD_BUTTON_INVALID) {
        std::snprintf(buf, sizeof(buf), "SDL_GAMEPAD:0x%08x", (unsigned)b.pad);
    } else if (b.axis != SDL_GAMEPAD_AXIS_INVALID) {
        std::snprintf(buf, sizeof(buf), "SDL_AXIS:0x%08x", (unsigned)b.axis);
    } else {
        return std::string();
    }
    return std::string(buf);
}

#ifdef launcher
static void WriteBindingsJsonFromState(void) {
    for (const auto& d : kDefaults) {
        nlohmann::json arr = nlohmann::json::array();
        for (const Bind& b : sBinds[d.input]) {
            std::string tok = FormatBindForJson(b);
            if (!tok.empty()) {
                arr.push_back(std::move(tok));
            }
        }
        sConfigJson["bindings"][d.name] = std::move(arr);
    }
    SaveConfig();
}

extern "C" void Port_Config_SetPortSettingsMenuEnabled(bool enabled) {
    sPortSettingsMenuEnabled = enabled;
    sConfigJson["port_settings_menu"] = enabled;
    SaveConfig();
}

extern "C" const char* Port_Config_InputUiLabel(PortInput input) {
    static const char* const kLabels[PORT_INPUT_COUNT] = {
        "A (jump / talk)",   "B (attack / cancel)", "Select",      "Start", "D-pad Right", "D-pad Left",
        "D-pad Up",          "D-pad Down",          "R",           "L",     "Soft slot X", "Soft slot Y",
        "Soft slot L2 / LT", "Soft slot R2 / RT",   "Roll attack",
    };
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return "?";
    }
    return kLabels[input];
}

/* Shared formatter for the launcher bindings rows. padOnly skips keyboard
 * binds so the gamepad column lists only pad/axis entries; otherwise every
 * bind (keyboard + pad + axis) is shown. Output byte-identical to the two
 * former hand-copied functions. */
static void FormatBindingsLineImpl(PortInput input, char* out, size_t outCap, bool padOnly) {
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return;
    }
    size_t pos = 0;
    bool first = true;
    for (const Bind& b : sBinds[input]) {
        if (padOnly && b.key != SDLK_UNKNOWN)
            continue;
        char piece[112];
        piece[0] = '\0';
        if (b.key != SDLK_UNKNOWN) {
            const char* nm = SDL_GetKeyName(b.key);
            if (nm && nm[0] != '\0') {
                std::snprintf(piece, sizeof(piece), "%s", nm);
            } else {
                std::snprintf(piece, sizeof(piece), "key 0x%x", static_cast<unsigned>(b.key));
            }
        } else if (b.pad != SDL_GAMEPAD_BUTTON_INVALID) {
            const char* nm = SDL_GetGamepadStringForButton(b.pad);
            if (nm && nm[0] != '\0') {
                std::snprintf(piece, sizeof(piece), "%s", nm);
            } else {
                std::snprintf(piece, sizeof(piece), "pad btn %u", static_cast<unsigned>(b.pad));
            }
        } else if (b.axis != SDL_GAMEPAD_AXIS_INVALID) {
            const char* nm = SDL_GetGamepadStringForAxis(b.axis);
            if (nm && nm[0] != '\0') {
                std::snprintf(piece, sizeof(piece), "%s", nm);
            } else {
                std::snprintf(piece, sizeof(piece), "axis %u", static_cast<unsigned>(b.axis));
            }
        }
        if (piece[0] == '\0') {
            continue;
        }
        if (!first) {
            if (pos + 2 < outCap) {
                out[pos++] = ',';
                out[pos++] = ' ';
                out[pos] = '\0';
            }
        }
        first = false;
        const size_t plen = std::strlen(piece);
        if (pos + plen >= outCap) {
            break;
        }
        std::memcpy(out + pos, piece, plen);
        pos += plen;
        out[pos] = '\0';
    }
    if (out[0] == '\0' && outCap > 1) {
        std::snprintf(out, outCap, "(none)");
    }
}

extern "C" void Port_Config_FormatBindingsLine(PortInput input, char* out, size_t outCap) {
    FormatBindingsLineImpl(input, out, outCap, false);
}

extern "C" void Port_Config_SetKeyboardBindExclusive(PortInput input, int sdl_keycode) {
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return;
    }
    const SDL_Keycode nk = static_cast<SDL_Keycode>(sdl_keycode);
    std::vector<Bind> kept;
    kept.reserve(sBinds[input].size());
    for (const Bind& b : sBinds[input]) {
        if (b.pad != SDL_GAMEPAD_BUTTON_INVALID || b.axis != SDL_GAMEPAD_AXIS_INVALID) {
            kept.push_back(b);
        }
    }
    sBinds[input] = std::move(kept);
    if (nk != SDLK_UNKNOWN) {
        Bind kb;
        kb.key = nk;
        sBinds[input].insert(sBinds[input].begin(), kb);
    }
    WriteBindingsJsonFromState();
}

extern "C" void Port_Config_FormatGamepadBindingsLine(PortInput input, char* out, size_t outCap) {
    FormatBindingsLineImpl(input, out, outCap, true);
}

extern "C" void Port_Config_SetGamepadBindExclusive(PortInput input, int sdl_gamepad_button) {
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return;
    }
    const SDL_GamepadButton nb = static_cast<SDL_GamepadButton>(sdl_gamepad_button);
    std::vector<Bind> kept;
    kept.reserve(sBinds[input].size());
    for (const Bind& b : sBinds[input]) {
        if (b.pad == SDL_GAMEPAD_BUTTON_INVALID) {
            kept.push_back(b);
        }
    }
    sBinds[input] = std::move(kept);
    if (nb != SDL_GAMEPAD_BUTTON_INVALID && nb >= 0 && nb < SDL_GAMEPAD_BUTTON_COUNT) {
        Bind pb;
        pb.pad = nb;
        sBinds[input].insert(sBinds[input].begin(), pb);
    }
    WriteBindingsJsonFromState();
}
#endif

} // namespace

static SDL_Gamepad* OpenGamepad(SDL_JoystickID id) {
    for (SDL_Gamepad* pad : sPads) {
        if (SDL_GetGamepadID(pad) == id) {
            return pad;
        }
    }
    if (!SDL_IsGamepad(id)) {
        SDL_Log("SDL device is not a gamepad: %s", SDL_GetGamepadNameForID(id));
        return nullptr;
    }
    SDL_Gamepad* pad = SDL_OpenGamepad(id);
    SDL_Log("Gamepad %s: %s", pad ? "connected" : "open failed", SDL_GetGamepadNameForID(id));
    if (pad) {
        sPads.push_back(pad);
    }
    return pad;
}

static void CloseGamepad(SDL_JoystickID id) {
    for (auto it = sPads.begin(); it != sPads.end(); ++it) {
        if (SDL_GetGamepadID(*it) == id) {
            SDL_Log("Gamepad disconnected: %s", SDL_GetGamepadName(*it));
            SDL_CloseGamepad(*it);
            sPads.erase(it);
            return;
        }
    }
}

extern "C" void Port_Config_Load(const char* path) {
    nlohmann::json j = DefaultsJson();
    const std::filesystem::path p = path ? path : "config.json";
    sConfigPath = p;

    if (std::filesystem::exists(p)) {
        try {
            std::ifstream(p) >> j;
        } catch (...) { j = DefaultsJson(); }
    } else {
        std::ofstream(p) << j.dump(4) << '\n';
    }

    sConfigJson = j;

    /* Apply parsed JSON into the runtime statics. j.value(key, default)
     * throws json::type_error when a key is present with the wrong type
     * (e.g. a hand-edited "window_scale": "big"). The parse above is
     * guarded; guard the reads too so a malformed-but-valid-JSON config
     * degrades to defaults instead of terminating the process. The lambda
     * param shadows the outer j so it can be re-run against defaults. */
    auto apply = [](const nlohmann::json& j) {
        for (const auto& e : kBoolCfg)
            *e.var = j.value(e.key, e.def);
        for (const auto& e : kIntCfg)
            *e.var = j.value(e.key, e.def);
        for (const auto& e : kStrCfg)
            *e.var = j.value(e.key, std::string(e.def));
        for (const auto& e : kFloatCfg)
            *e.var = (float)j.value(e.key, e.def);
        for (const auto& e : kScaleCfg) {
            int v = j.value(e.key, e.def);
            *e.var = (v >= e.lo && v <= e.hi) ? (u8)v : (u8)e.def;
        }
        /* frame_time_ns: a MISSING key now defaults to the 60 FPS cap (same as
         * DefaultsJson) so VSync is enforced by default — an uncapped default
         * makes Port_Config_TargetFps()==0, which port_bios.c treats as "must
         * disable VSync" (can't sync above the panel), silently defeating it.
         * An explicit frame_time_ns:0 in the config is still honoured as a
         * deliberate uncapped/VSync-off opt-out (the key is present). */
        sFrameTimeNs = j.value("frame_time_ns", kDefaultFrameTimeNs);
        sAutosaveIntervalMs = j.value("autosave_interval_ms", 60000u); // ponytail: lone u32, not worth a table
        {
            std::string ts = j.value("touch_scheme", std::string("joystick"));
            for (char& c : ts) {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            sTouchScheme = (ts == "dpad") ? PORT_TOUCH_SCHEME_DPAD : PORT_TOUCH_SCHEME_JOYSTICK;
        }
        sTouchScale = std::min(1.6f, std::max(0.6f, j.value("touch_scale", 1.0f)));
        sTouchOpacity = std::min(1.5f, std::max(0.3f, j.value("touch_opacity", 1.0f)));
        {
            std::string am = j.value("aspect_mode", std::string("native"));
            for (char& c : am) {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            if (am == "16:9" || am == "widescreen")
                sAspectMode = PORT_ASPECT_WIDESCREEN_16_9;
            else if (am == "21:9" || am == "ultrawide")
                sAspectMode = PORT_ASPECT_ULTRAWIDE_21_9;
            else if (am == "32:9" || am == "super_ultrawide")
                sAspectMode = PORT_ASPECT_SUPER_ULTRAWIDE_32_9;
            else
                sAspectMode = PORT_ASPECT_NATIVE_3_2;

            std::string bf = j.value("bg_fill", std::string("blurred"));
            for (char& c : bf) {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            if (bf == "solid" || bf == "solid_color")
                sBgFill = PORT_BG_FILL_SOLID_COLOR;
            else if (bf == "blurred" || bf == "blurred_frame")
                sBgFill = PORT_BG_FILL_BLURRED_FRAME;
            else
                sBgFill = PORT_BG_FILL_BLACK;

            const auto& col = j.contains("bg_fill_color") ? j["bg_fill_color"] : nlohmann::json::array();
            if (col.is_array() && col.size() >= 3) {
                auto clamp_u8 = [](int v) -> u8 {
                    if (v < 0)
                        return 0;
                    if (v > 255)
                        return 255;
                    return (u8)v;
                };
                sBgFillR = clamp_u8(col[0].is_number() ? col[0].get<int>() : 0);
                sBgFillG = clamp_u8(col[1].is_number() ? col[1].get<int>() : 0);
                sBgFillB = clamp_u8(col[2].is_number() ? col[2].get<int>() : 0);
            }
        }
        {
            std::string rb = j.value("render_backend", std::string("auto"));
            for (char& c : rb) {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            if (rb == "software" || rb == "sw")
                sRenderBackend = PORT_RENDER_BACKEND_SOFTWARE;
            else if (rb == "gpu")
                sRenderBackend = PORT_RENDER_BACKEND_GPU;
            else
                sRenderBackend = PORT_RENDER_BACKEND_AUTO;
        }
        /* reborn_features: absent from DefaultsJson on purpose — presence is
         * the signal that the user overrode the compile-time feature mask. */
        sHasRebornFeatures = j.contains("reborn_features");
        sRebornFeatures = sHasRebornFeatures ? j.value("reborn_features", 0u) : 0u;

        for (auto& v : sBinds) {
            v.clear();
        }
        nlohmann::json empty = nlohmann::json::object();
        const auto& b = j.contains("bindings") ? j["bindings"] : empty;
        for (const auto& d : kDefaults) {
            LoadBinds(d.input, b.contains(d.name) ? b[d.name] : DefaultsJson()["bindings"][d.name]);
        }
    };

    try {
        apply(j);
    } catch (const std::exception& e) {
        fprintf(stderr, "[CONFIG] Malformed config.json (%s); falling back to defaults.\n", e.what());
        const nlohmann::json def = DefaultsJson();
        sConfigJson = def;
        apply(def); /* defaults are well-typed and cannot throw */
    }
}

extern "C" u8 Port_Config_WindowScale(void) {
    return sScale;
}

extern "C" const char* Port_Config_UpscaleMethod(void) {
    return sUpscaleMethod.c_str();
}

extern "C" u64 Port_Config_FrameTimeNs(void) {
    /* Parity mode pins pacing to authentic GBA refresh, ignoring any user
     * fps cap (including uncapped), so segment/long-run timing matches
     * console instead of the round 60 Hz default. */
    if (sConsoleParity) {
        return kGbaFrameTimeNs;
    }
    return sFrameTimeNs;
}

extern "C" u32 Port_Config_TargetFps(void) {
    const u64 frameNs = Port_Config_FrameTimeNs();
    if (frameNs == 0) {
        return 0;
    }
    return (u32)((1000000000ULL + (frameNs / 2)) / frameNs);
}

extern "C" u64 Port_Config_TickTimeNs(void) {
    /* Game-logic tick period under decoupled pacing. Fixed: the user's FPS
     * cap must not change game speed. Parity pins to the authentic GBA
     * refresh; otherwise the port's long-standing round 60 Hz. */
    if (sConsoleParity) {
        return kGbaFrameTimeNs;
    }
    return kDefaultFrameTimeNs;
}

extern "C" bool Port_Config_GetDecoupleRender(void) {
    return sDecoupleRender;
}
extern "C" void Port_Config_SetDecoupleRender(bool on) {
    sDecoupleRender = on;
    sConfigJson["decouple_render"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetShowFps(void) {
    return sShowFps;
}
extern "C" void Port_Config_SetShowFps(bool on) {
    sShowFps = on;
    sConfigJson["show_fps"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetTouchControls(void) {
    return sTouchControls;
}
extern "C" void Port_Config_SetTouchControls(bool on) {
    sTouchControls = on;
    sConfigJson["touch_controls"] = on;
    SaveConfig();
}

extern "C" bool Port_Config_PortSettingsMenuEnabled(void) {
    return sPortSettingsMenuEnabled;
}

extern "C" const char* Port_Config_ActiveSaveProfile(void) {
    return sActiveSaveProfile.c_str();
}

extern "C" void Port_Config_SetActiveSaveProfile(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        path = "tmc.sav";
    }
    sActiveSaveProfile = path;
    sConfigJson["active_save_profile"] = sActiveSaveProfile;
    SaveConfig();
}

extern "C" bool Port_Config_AutosaveEnabled(void) {
    return sAutosaveEnabled;
}

extern "C" void Port_Config_SetAutosaveEnabled(bool enabled) {
    sAutosaveEnabled = enabled;
    sConfigJson["autosave_enabled"] = enabled;
    SaveConfig();
}

extern "C" u32 Port_Config_AutosaveIntervalMs(void) {
    return sAutosaveIntervalMs;
}

extern "C" void Port_Config_SetAutosaveIntervalMs(u32 ms) {
    if (ms < 5000)
        ms = 5000;
    if (ms > 600000)
        ms = 600000;
    sAutosaveIntervalMs = ms;
    sConfigJson["autosave_interval_ms"] = ms;
    SaveConfig();
}

extern "C" void Port_Config_SetWindowScale(u8 scale) {
    if (scale < 1) {
        scale = 1;
    } else if (scale > 10) {
        scale = 10;
    }
    sScale = scale;
    sConfigJson["window_scale"] = static_cast<int>(scale);
    SaveConfig();
}

extern "C" u8 Port_Config_InternalScale(void) {
    return sInternalScale;
}

extern "C" void Port_Config_SetInternalScale(u8 scale) {
    /* Cap at 10×. port_ppu.cpp backs the scaled framebuffer with a fixed
       64-byte-aligned BSS pool, so scale changes do not allocate in the
       frame loop and are not bounded by the static virtuappu_frame_buffer.
       At 10×, native output is 2400×1600 = 15 MB and ~3.8 Mpx copied per
       frame, so the practical limit is SDL_Texture max / fill-rate. */
    if (scale < 1)
        scale = 1;
    if (scale > 10)
        scale = 10;
    sInternalScale = scale;
    sConfigJson["internal_scale"] = static_cast<int>(scale);
    SaveConfig();
}

extern "C" void Port_Config_CycleInternalScale(int direction) {
    int next = (int)sInternalScale + (direction < 0 ? -1 : 1);
    if (next < 1)
        next = 10;
    if (next > 10)
        next = 1;
    Port_Config_SetInternalScale((u8)next);
}

extern "C" void Port_Config_SetUpscaleMethod(const char* method) {
    if (method == nullptr || method[0] == '\0') {
        method = "nearest";
    }
    sUpscaleMethod = method;
    sConfigJson["upscale_method"] = sUpscaleMethod;
    SaveConfig();
}

extern "C" void Port_Config_SetTargetFps(u32 fps) {
    sFrameTimeNs = FrameTimeForFps(fps);
    sConfigJson["frame_time_ns"] = sFrameTimeNs;
    SaveConfig();
}

extern "C" PortTouchScheme Port_Config_TouchScheme(void) {
    return sTouchScheme;
}

extern "C" void Port_Config_SetTouchScheme(PortTouchScheme scheme) {
    sTouchScheme = (scheme == PORT_TOUCH_SCHEME_DPAD) ? PORT_TOUCH_SCHEME_DPAD : PORT_TOUCH_SCHEME_JOYSTICK;
    sConfigJson["touch_scheme"] = (sTouchScheme == PORT_TOUCH_SCHEME_DPAD) ? "dpad" : "joystick";
    SaveConfig();
}

extern "C" void Port_Config_CycleTouchScheme(int /*direction*/) {
    Port_Config_SetTouchScheme(sTouchScheme == PORT_TOUCH_SCHEME_DPAD ? PORT_TOUCH_SCHEME_JOYSTICK
                                                                      : PORT_TOUCH_SCHEME_DPAD);
}

extern "C" float Port_Config_TouchScale(void) {
    return sTouchScale;
}

extern "C" void Port_Config_SetTouchScale(float scale) {
    sTouchScale = std::min(1.6f, std::max(0.6f, scale));
    sConfigJson["touch_scale"] = sTouchScale;
    SaveConfig();
}

extern "C" float Port_Config_TouchOpacity(void) {
    return sTouchOpacity;
}

extern "C" void Port_Config_SetTouchOpacity(float opacity) {
    sTouchOpacity = std::min(1.5f, std::max(0.3f, opacity));
    sConfigJson["touch_opacity"] = sTouchOpacity;
    SaveConfig();
}

extern "C" bool Port_Config_WidescreenEnabled(void) {
    /* Parity mode forces the effective answer to false regardless of the
     * stored preference: widening the framebuffer also widens off-screen
     * AI culling, which ticks enemies (and advances RNG) earlier than
     * hardware. The saved pref is preserved so toggling parity back off
     * restores the user's choice. */
    if (sConsoleParity) {
        return false;
    }
    return sWidescreenEnabled;
}

extern "C" void Port_Config_SetWidescreenEnabled(bool enabled) {
    sWidescreenEnabled = enabled;
    sConfigJson["widescreen_enabled"] = enabled;
    SaveConfig();
}

extern "C" void Port_Config_ToggleWidescreen(void) {
    Port_Config_SetWidescreenEnabled(!sWidescreenEnabled);
}

extern "C" bool Port_Config_GetConsoleParity(void) {
    return sConsoleParity;
}

extern "C" void Port_Config_SetConsoleParity(bool on) {
    sConsoleParity = on;
    sConfigJson["console_parity"] = on;
    SaveConfig();
}

extern "C" void Port_Config_ToggleConsoleParity(void) {
    Port_Config_SetConsoleParity(!sConsoleParity);
}

extern "C" int Port_Config_PreferredRegion(void) {
    return sPreferredRegion;
}

extern "C" void Port_Config_SetPreferredRegion(int region) {
    sPreferredRegion = region;
    sConfigJson["preferred_region"] = region;
    SaveConfig();
}

extern "C" int Port_Config_PreferredLanguage(void) {
    return sPreferredLanguage;
}

extern "C" void Port_Config_SetPreferredLanguage(int lang) {
    sPreferredLanguage = lang;
    sConfigJson["preferred_language"] = lang;
    SaveConfig();
}

extern "C" PortAspectMode Port_Config_AspectMode(void) {
    return sAspectMode;
}

extern "C" const char* Port_Config_AspectModeName(PortAspectMode mode) {
    switch (mode) {
        case PORT_ASPECT_WIDESCREEN_16_9:
            return "Widescreen 16:9";
        case PORT_ASPECT_ULTRAWIDE_21_9:
            return "Ultrawide 21:9";
        case PORT_ASPECT_SUPER_ULTRAWIDE_32_9:
            return "Super Ultrawide 32:9";
        case PORT_ASPECT_NATIVE_3_2:
        default:
            return "Native 3:2 (GBA)";
    }
}

extern "C" void Port_Config_SetAspectMode(PortAspectMode mode) {
    if (mode < 0 || mode >= PORT_ASPECT_COUNT)
        mode = PORT_ASPECT_NATIVE_3_2;
    sAspectMode = mode;
    const char* name = "native";
    switch (mode) {
        case PORT_ASPECT_WIDESCREEN_16_9:
            name = "16:9";
            break;
        case PORT_ASPECT_ULTRAWIDE_21_9:
            name = "21:9";
            break;
        case PORT_ASPECT_SUPER_ULTRAWIDE_32_9:
            name = "32:9";
            break;
        case PORT_ASPECT_NATIVE_3_2:
        default:
            name = "native";
            break;
    }
    sConfigJson["aspect_mode"] = name;
    SaveConfig();
}

extern "C" void Port_Config_CycleAspectMode(int direction) {
    int next = (int)sAspectMode + (direction < 0 ? -1 : 1);
    if (next < 0)
        next = PORT_ASPECT_COUNT - 1;
    if (next >= PORT_ASPECT_COUNT)
        next = 0;
    Port_Config_SetAspectMode((PortAspectMode)next);
}

extern "C" PortBgFill Port_Config_BgFill(void) {
    return sBgFill;
}

extern "C" const char* Port_Config_BgFillName(PortBgFill fill) {
    switch (fill) {
        case PORT_BG_FILL_SOLID_COLOR:
            return "Solid color";
        case PORT_BG_FILL_BLURRED_FRAME:
            return "Blurred frame";
        case PORT_BG_FILL_BLACK:
        default:
            return "Black";
    }
}

extern "C" void Port_Config_SetBgFill(PortBgFill fill) {
    if (fill < 0 || fill >= PORT_BG_FILL_COUNT)
        fill = PORT_BG_FILL_BLACK;
    sBgFill = fill;
    const char* name = "black";
    switch (fill) {
        case PORT_BG_FILL_SOLID_COLOR:
            name = "solid";
            break;
        case PORT_BG_FILL_BLURRED_FRAME:
            name = "blurred";
            break;
        case PORT_BG_FILL_BLACK:
        default:
            name = "black";
            break;
    }
    sConfigJson["bg_fill"] = name;
    SaveConfig();
}

extern "C" void Port_Config_CycleBgFill(int direction) {
    int next = (int)sBgFill + (direction < 0 ? -1 : 1);
    if (next < 0)
        next = PORT_BG_FILL_COUNT - 1;
    if (next >= PORT_BG_FILL_COUNT)
        next = 0;
    Port_Config_SetBgFill((PortBgFill)next);
}

extern "C" void Port_Config_BgFillColor(u8* r, u8* g, u8* b) {
    if (r)
        *r = sBgFillR;
    if (g)
        *g = sBgFillG;
    if (b)
        *b = sBgFillB;
}

extern "C" void Port_Config_SetBgFillColor(u8 r, u8 g, u8 b) {
    sBgFillR = r;
    sBgFillG = g;
    sBgFillB = b;
    sConfigJson["bg_fill_color"] = nlohmann::json::array({ (int)r, (int)g, (int)b });
    SaveConfig();
}

extern "C" PortRenderBackend Port_Config_RenderBackend(void) {
    return sRenderBackend;
}

extern "C" const char* Port_Config_RenderBackendName(PortRenderBackend b) {
    switch (b) {
        case PORT_RENDER_BACKEND_SOFTWARE:
            return "Software";
        case PORT_RENDER_BACKEND_GPU:
            return "GPU";
        case PORT_RENDER_BACKEND_AUTO:
        default:
            return "Auto";
    }
}

extern "C" void Port_Config_SetRenderBackend(PortRenderBackend b) {
    if (b < 0 || b >= PORT_RENDER_BACKEND_COUNT)
        b = PORT_RENDER_BACKEND_AUTO;
    sRenderBackend = b;
    const char* name = "auto";
    switch (b) {
        case PORT_RENDER_BACKEND_SOFTWARE:
            name = "software";
            break;
        case PORT_RENDER_BACKEND_GPU:
            name = "gpu";
            break;
        case PORT_RENDER_BACKEND_AUTO:
        default:
            name = "auto";
            break;
    }
    sConfigJson["render_backend"] = name;
    SaveConfig();
}

extern "C" void Port_Config_CycleRenderBackend(int direction) {
    int next = (int)sRenderBackend + (direction < 0 ? -1 : 1);
    if (next < 0)
        next = PORT_RENDER_BACKEND_COUNT - 1;
    if (next >= PORT_RENDER_BACKEND_COUNT)
        next = 0;
    Port_Config_SetRenderBackend((PortRenderBackend)next);
}

extern "C" void Port_Config_CycleTargetFps(int direction) {
    const u32 current = Port_Config_TargetFps();
    size_t index = 0;
    u32 bestDistance = UINT32_MAX;

    for (size_t i = 0; i < kFpsPresets.size(); i++) {
        const u32 preset = kFpsPresets[i];
        const u32 distance = current > preset ? current - preset : preset - current;
        if (distance < bestDistance) {
            bestDistance = distance;
            index = i;
        }
    }

    if (direction < 0) {
        index = index == 0 ? kFpsPresets.size() - 1 : index - 1;
    } else {
        index = index + 1 >= kFpsPresets.size() ? 0 : index + 1;
    }

    Port_Config_SetTargetFps(kFpsPresets[index]);
}

/* Make sure every connected gamepad has an open SDL_Gamepad handle.
 * Called from both Port_Config_OpenGamepads() at startup and re-tried
 * lazily in Port_Config_InputPressed() so a controller that's plugged
 * in (or recognised by SDL) AFTER startup still gets picked up without
 * needing the GAMEPAD_ADDED event to flow through the poll loop. */
static void Port_Config_RescanGamepads(bool verbose) {
    const bool hadPads = !sPads.empty();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (verbose) {
        SDL_Log("SDL gamepads found: %d", count);
    }
    for (int i = 0; i < count; i++) {
        OpenGamepad(ids[i]);
    }
    if (!hadPads && !sPads.empty()) {
        Port_TouchControls_SetGamepadAvailable(true);
    } else if (hadPads && sPads.empty()) {
        Port_TouchControls_SetGamepadAvailable(false);
    }
    SDL_free(ids);
}

extern "C" void Port_Config_OpenGamepads(void) {
    /* Hint nudges to make SDL3 see more devices on Linux/wine where the
     * default backend selection sometimes misses Xinput-shaped pads. */
    SDL_SetHint("SDL_JOYSTICK_HIDAPI", "1");
    SDL_SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "1");

    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
        SDL_Log("SDL joystick init failed: %s", SDL_GetError());
    }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL gamepad init failed: %s", SDL_GetError());
        return;
    }
    Port_Config_RescanGamepads(true);
}

/* Re-serialize sBinds[input] back into sConfigJson["bindings"][name]
 * and persist. Keeps the JSON in sync with runtime mutations so the
 * next launch picks up the user's rebinds. */
static void PersistBinds(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT)
        return;
    const char* name = nullptr;
    for (const auto& d : kDefaults) {
        if (d.input == input) {
            name = d.name;
            break;
        }
    }
    if (!name)
        return;
    nlohmann::json arr = nlohmann::json::array();
    for (const Bind& b : sBinds[input]) {
        std::string s = FormatBindForJson(b);
        if (!s.empty())
            arr.push_back(s);
    }
    if (!sConfigJson.contains("bindings") || !sConfigJson["bindings"].is_object()) {
        sConfigJson["bindings"] = nlohmann::json::object();
    }
    sConfigJson["bindings"][name] = arr;
    SaveConfig();
}

extern "C" void Port_Config_HandleEvent(const SDL_Event* e) {
    /* Touch-controls dispatch first so finger / virtual-pad events on
     * Android route to the touch handler before the keyboard / pad
     * paths below see them. From matheo's launcher integration. */
    Port_TouchControls_HandleEvent(e);

    /* Rebind capture: when the user's pressed an action-edit button in
     * the ImGui Controls tab, the next press of any key / gamepad
     * button / axis triggers becomes that action's new binding. Esc /
     * Right-click cancels without binding. Consume the event so the
     * captured press doesn't also propagate to its old action this
     * frame. */
    if (sCapturingInput >= 0) {
        const PortInput target = (PortInput)sCapturingInput;
        bool captured = false;
        Bind newBind;
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
            if (e->key.key == SDLK_ESCAPE) {
                /* Cancel without binding. */
                sCapturingInput = -1;
                sCaptureAppend = false;
                return;
            }
            newBind.key = e->key.key;
            captured = true;
        } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            newBind.pad = (SDL_GamepadButton)e->gbutton.button;
            captured = true;
        } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e->gaxis.value > kAxisThreshold) {
            newBind.axis = (SDL_GamepadAxis)e->gaxis.axis;
            captured = true;
        }
        if (captured) {
            /* Speedrun integrity (Console-Parity): a physical key/button/axis
             * must map to at most one action, so strip the captured input from
             * every OTHER action before assigning it here. */
            if (sConsoleParity) {
                for (size_t j = 0; j < PORT_INPUT_COUNT; j++) {
                    if ((int)j == (int)target)
                        continue;
                    std::vector<Bind>& v = sBinds[j];
                    bool changed = false;
                    for (size_t k = 0; k < v.size();) {
                        const Bind& b = v[k];
                        const bool same = (newBind.key != SDLK_UNKNOWN && b.key == newBind.key) ||
                                          (newBind.pad != SDL_GAMEPAD_BUTTON_INVALID && b.pad == newBind.pad) ||
                                          (newBind.axis != SDL_GAMEPAD_AXIS_INVALID && b.axis == newBind.axis);
                        if (same) {
                            v.erase(v.begin() + (long)k);
                            changed = true;
                        } else {
                            ++k;
                        }
                    }
                    if (changed)
                        PersistBinds((PortInput)j);
                }
            }
            if (!sCaptureAppend)
                sBinds[target].clear();
            sBinds[target].push_back(newBind);
            PersistBinds(target);
            sCapturingInput = -1;
            sCaptureAppend = false;
            return;
        }
    }

    if (e->type == SDL_EVENT_GAMEPAD_ADDED || e->type == SDL_EVENT_JOYSTICK_ADDED) {
        OpenGamepad(e->gdevice.which);
    } else if (e->type == SDL_EVENT_GAMEPAD_REMOVED || e->type == SDL_EVENT_JOYSTICK_REMOVED) {
        CloseGamepad(e->gdevice.which);
        Port_TouchControls_SetGamepadAvailable(!sPads.empty());
    } else if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        /* Stamp every PortInput whose binding includes this key as
         * "pressed this frame" so the engine sees the press even if
         * the matching release arrives before the next poll. */
        for (size_t i = 0; i < PORT_INPUT_COUNT; i++) {
            for (const Bind& b : sBinds[i]) {
                if (b.key != SDLK_UNKNOWN && b.key == e->key.key) {
                    sEdgePressed[i] = true;
                    break;
                }
            }
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        Port_TouchControls_NotifyGamepadUsed();
        for (size_t i = 0; i < PORT_INPUT_COUNT; i++) {
            for (const Bind& b : sBinds[i]) {
                if (b.pad >= 0 && b.pad < SDL_GAMEPAD_BUTTON_COUNT && b.pad == (SDL_GamepadButton)e->gbutton.button) {
                    sEdgePressed[i] = true;
                    break;
                }
            }
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e->gaxis.value > kAxisThreshold) {
        Port_TouchControls_NotifyGamepadUsed();
        for (size_t i = 0; i < PORT_INPUT_COUNT; i++) {
            for (const Bind& b : sBinds[i]) {
                if (b.axis >= 0 && b.axis < SDL_GAMEPAD_AXIS_COUNT && b.axis == (SDL_GamepadAxis)e->gaxis.axis) {
                    sEdgePressed[i] = true;
                    break;
                }
            }
        }
    }
}

extern "C" void Port_Config_ClearInputEdges(void) {
    sEdgePressed.fill(false);
}

/* Test-only input seam. Stamps the per-frame edge cache for `input` exactly
 * as an SDL KEY_DOWN would, so headless repro harnesses (see
 * port_repro_roll_macro.c) drive edge/held reads through the real
 * Port_Config_InputPressed / Port_Config_InputEdgePressed path. Wiped every
 * frame by Port_Config_ClearInputEdges() after KEYINPUT is committed, so a
 * forced bit never survives past the frame it is set on. */
extern "C" void Port_Config_TestForceEdge(PortInput input) {
    if (input >= 0 && input < PORT_INPUT_COUNT) {
        sEdgePressed[input] = true;
    }
}

/* Production edge-stamp: the touch overlay calls this on a pointer DOWN that
 * lands on a button so a sub-frame tap (DOWN+UP delivered in one poll batch —
 * common with synthesized `adb input` events and fast real taps) still
 * registers for one game frame, exactly as the keyboard KEY_DOWN path does.
 * Same cache, same per-frame Port_Config_ClearInputEdges() wipe. */
extern "C" void Port_Config_StampInputEdge(PortInput input) {
    if (input >= 0 && input < PORT_INPUT_COUNT) {
        sEdgePressed[input] = true;
    }
}

extern "C" bool Port_Config_InputEdgePressed(PortInput input) {
    /* Parity mode disables the sub-frame edge cache: a press is only seen
     * on the frame the engine actually polls it, matching hardware 1-frame
     * input granularity instead of the more-lenient port default. */
    if (sConsoleParity) {
        return false;
    }
    if (input >= 0 && input < PORT_INPUT_COUNT) {
        return sEdgePressed[input];
    }
    return false;
}
/* True when the SDL event is a fresh "down" (key press, gamepad button
 * press, or axis crossing the trigger threshold) bound to `input`. Mirrors
 * the matching in Port_Config_HandleEvent but reports a single input rather
 * than stamping the edge cache — used by callers that consume an input
 * while game KEYINPUT is masked (e.g. closing the file-select setup
 * sidebar with its open button). Key repeats are ignored so a held key
 * fires once. */
extern "C" bool Port_Config_EventIsInputDown(const SDL_Event* e, PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return false;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        for (const Bind& b : sBinds[input]) {
            if (b.key != SDLK_UNKNOWN && b.key == e->key.key) {
                return true;
            }
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        for (const Bind& b : sBinds[input]) {
            if (b.pad >= 0 && b.pad < SDL_GAMEPAD_BUTTON_COUNT && b.pad == (SDL_GamepadButton)e->gbutton.button) {
                return true;
            }
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e->gaxis.value > kAxisThreshold) {
        for (const Bind& b : sBinds[input]) {
            if (b.axis >= 0 && b.axis < SDL_GAMEPAD_AXIS_COUNT && b.axis == (SDL_GamepadAxis)e->gaxis.axis) {
                return true;
            }
        }
    }
    return false;
}

extern "C" bool Port_Config_InputPressed(PortInput input) {
    if (Port_TouchControls_InputPressed(input)) {
        return true;
    }

    /* Edge cache — set by Port_Config_HandleEvent on KEY_DOWN /
     * GAMEPAD_BUTTON_DOWN events. Lets a sub-frame tap (press+release
     * entirely between two polls) still register as held for one game
     * frame, which the polled-state path below cannot do on its own. */
    if (!sConsoleParity && input >= 0 && input < PORT_INPUT_COUNT && sEdgePressed[input]) {
        return true;
    }

    SDL_UpdateGamepads();
    /* Re-scan every ~1s so a hot-plugged pad starts working even if its
     * GAMEPAD_ADDED event somehow didn't reach the poll loop. */
    static uint32_t sNextRescanAt = 0;
    uint32_t now = SDL_GetTicks();
    if (now >= sNextRescanAt) {
        Port_Config_RescanGamepads(false);
        sNextRescanAt = now + 1000;
    }

    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (const Bind& b : sBinds[input]) {
        SDL_Scancode scan = b.key == SDLK_UNKNOWN ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromKey(b.key, nullptr);
        if (scan != SDL_SCANCODE_UNKNOWN && (int)scan < count && keys[scan]) {
            return true;
        }
        for (SDL_Gamepad* pad : sPads) {
            if (b.pad >= 0 && b.pad < SDL_GAMEPAD_BUTTON_COUNT && SDL_GetGamepadButton(pad, b.pad)) {
                Port_TouchControls_NotifyGamepadUsed();
                return true;
            }
            if (b.axis >= 0 && b.axis < SDL_GAMEPAD_AXIS_COUNT && SDL_GetGamepadAxis(pad, b.axis) > kAxisThreshold) {
                Port_TouchControls_NotifyGamepadUsed();
                return true;
            }
        }
    }
    return false;
}

extern "C" bool Port_Config_SoftSlotPressed(int slot) {
    static const PortInput kMap[4] = {
        PORT_INPUT_SOFT_X,
        PORT_INPUT_SOFT_Y,
        PORT_INPUT_SOFT_L2,
        PORT_INPUT_SOFT_R2,
    };
    if (slot < 0 || slot >= 4)
        return false;
    return Port_Config_InputPressed(kMap[slot]);
}

/* Returns the left-stick reading from the first attached gamepad, in
 * the range [-1, 1] per axis, with Y inverted to match TMC's screen
 * convention (positive Y = down on screen, negative Y = up). Returns
 * false (and leaves outputs untouched) when no gamepad is attached or
 * when SDL hasn't reported any stick activity yet. */
extern "C" bool Port_Config_GetLeftStick(float* outX, float* outY) {
    if (sPads.empty()) {
        return false;
    }
    SDL_Gamepad* pad = sPads.front();
    /* SDL axis values are int16 -32768..32767. Normalise to [-1, 1].
     * +y on SDL = stick down = positive screen-y in TMC (top-left
     * origin), so no sign flip is required despite intuition. */
    const int16_t rawX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    const int16_t rawY = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    if (outX)
        *outX = (float)rawX / 32767.0f;
    if (outY)
        *outY = (float)rawY / 32767.0f;
    return true;
}

extern "C" void Port_Config_CloseGamepads(void) {
    for (SDL_Gamepad* pad : sPads) {
        SDL_CloseGamepad(pad);
    }
    sPads.clear();
}

/* ============================================================ */
/*                     Rebind API for the UI                    */
/* ============================================================ */

extern "C" const char* Port_Config_InputName(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT)
        return "?";
    for (const auto& d : kDefaults) {
        if (d.input == input)
            return d.name;
    }
    return "?";
}

extern "C" int Port_Config_BindingCount(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT)
        return 0;
    return (int)sBinds[input].size();
}

/* Human-readable label for a single binding. Composite three rows:
 * gamepad buttons get the SDL_GetGamepadStringForButton name where
 * available (Xbox-style "A", "Y", "DPad Up" etc.); axes get the
 * trigger / stick name; keyboard keys use SDL_GetKeyName. Falls back
 * to the raw numeric code if any of those return null. */
extern "C" void Port_Config_BindingLabel(PortInput input, int idx, char* out, int cap) {
    if (!out || cap <= 0)
        return;
    out[0] = '\0';
    if (input < 0 || input >= PORT_INPUT_COUNT)
        return;
    if (idx < 0 || idx >= (int)sBinds[input].size())
        return;
    const Bind& b = sBinds[input][idx];
    if (b.key != SDLK_UNKNOWN) {
        const char* name = SDL_GetKeyName(b.key);
        std::snprintf(out, cap, "%s", (name && *name) ? name : "Key");
    } else if (b.pad != SDL_GAMEPAD_BUTTON_INVALID) {
        const char* name = SDL_GetGamepadStringForButton(b.pad);
        if (name && *name)
            std::snprintf(out, cap, "Pad: %s", name);
        else
            std::snprintf(out, cap, "Pad button %d", (int)b.pad);
    } else if (b.axis != SDL_GAMEPAD_AXIS_INVALID) {
        const char* name = SDL_GetGamepadStringForAxis(b.axis);
        if (name && *name)
            std::snprintf(out, cap, "Axis: %s", name);
        else
            std::snprintf(out, cap, "Pad axis %d", (int)b.axis);
    }
}

extern "C" void Port_Config_ClearBindings(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT)
        return;
    sBinds[input].clear();
    PersistBinds(input);
}

extern "C" void Port_Config_BeginCaptureBinding(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT)
        return;
    sCapturingInput = (int)input;
    sCaptureAppend = false; /* replace this action's bindings */
}

extern "C" void Port_Config_BeginAddBinding(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT)
        return;
    sCapturingInput = (int)input;
    sCaptureAppend = true; /* append a binding, keep existing ones */
}

extern "C" int Port_Config_IsCapturingBinding(void) {
    return sCapturingInput >= 0 ? 1 : 0;
}

extern "C" PortInput Port_Config_CapturingBindingInput(void) {
    return (PortInput)sCapturingInput;
}

extern "C" void Port_Config_CancelCaptureBinding(void) {
    sCapturingInput = -1;
    sCaptureAppend = false;
}

extern "C" void Port_Config_ResetAllBindings(void) {
    /* Re-populate sBinds from the kDefaults table and persist the
     * resulting JSON. Mirrors what Port_Config_Load does on a missing
     * config — same code path. */
    for (auto& v : sBinds)
        v.clear();
    for (const auto& d : kDefaults) {
        for (const char* bind : d.binds) {
            AddBind(d.input, bind);
        }
    }
    /* Rewrite the bindings object in JSON wholesale. */
    nlohmann::json b = nlohmann::json::object();
    for (const auto& d : kDefaults) {
        nlohmann::json arr = nlohmann::json::array();
        for (const Bind& bind : sBinds[d.input]) {
            std::string s = FormatBindForJson(bind);
            if (!s.empty())
                arr.push_back(s);
        }
        b[d.name] = arr;
    }
    sConfigJson["bindings"] = b;
    SaveConfig();
}

/* ------------------------------------------------------------------ */
/*  TTS settings                                                      */
/*                                                                    */
/*  Persisted to config.json so the user's voice / rate / pitch /     */
/*  volume / language survives restarts. The actual TTS service       */
/*  caches these in port_tts.cpp and re-reads on init.                */
/* ------------------------------------------------------------------ */

extern "C" bool Port_Config_GetTtsEnabled(void) {
    return sTtsEnabled;
}
extern "C" void Port_Config_SetTtsEnabled(bool on) {
    sTtsEnabled = on;
    sConfigJson["tts_enabled"] = on;
    SaveConfig();
}

extern "C" float Port_Config_GetTtsRate(void) {
    return sTtsRate;
}
extern "C" void Port_Config_SetTtsRate(float v) {
    sTtsRate = v;
    sConfigJson["tts_rate"] = (double)v;
    SaveConfig();
}

extern "C" float Port_Config_GetTtsPitch(void) {
    return sTtsPitch;
}
extern "C" void Port_Config_SetTtsPitch(float v) {
    sTtsPitch = v;
    sConfigJson["tts_pitch"] = (double)v;
    SaveConfig();
}

extern "C" float Port_Config_GetTtsVolume(void) {
    return sTtsVolume;
}
extern "C" void Port_Config_SetTtsVolume(float v) {
    sTtsVolume = v;
    sConfigJson["tts_volume"] = (double)v;
    SaveConfig();
}

extern "C" const char* Port_Config_GetTtsVoice(void) {
    return sTtsVoice.c_str();
}
extern "C" void Port_Config_SetTtsVoice(const char* v) {
    sTtsVoice = v ? v : "";
    sConfigJson["tts_voice"] = sTtsVoice;
    SaveConfig();
}

extern "C" const char* Port_Config_GetTtsLanguage(void) {
    return sTtsLanguage.c_str();
}
extern "C" void Port_Config_SetTtsLanguage(const char* v) {
    sTtsLanguage = v ? v : "";
    sConfigJson["tts_language"] = sTtsLanguage;
    SaveConfig();
}

extern "C" bool Port_Config_GetA11yCues(void) {
    return sA11yCues;
}
extern "C" void Port_Config_SetA11yCues(bool on) {
    sA11yCues = on;
    sConfigJson["a11y_cues"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetA11yFootsteps(void) {
    return sA11yFootsteps;
}
extern "C" void Port_Config_SetA11yFootsteps(bool on) {
    sA11yFootsteps = on;
    sConfigJson["a11y_footsteps"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetA11yHazards(void) {
    return sA11yHazards;
}
extern "C" void Port_Config_SetA11yHazards(bool on) {
    sA11yHazards = on;
    sConfigJson["a11y_hazards"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetA11yRadar(void) {
    return sA11yRadar;
}
extern "C" void Port_Config_SetA11yRadar(bool on) {
    sA11yRadar = on;
    sConfigJson["a11y_radar"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetA11yWalls(void) {
    return sA11yWalls;
}
extern "C" void Port_Config_SetA11yWalls(bool on) {
    sA11yWalls = on;
    sConfigJson["a11y_walls"] = on;
    SaveConfig();
}

/* ---- Second-screen map behavior --------------------------------------- */
extern "C" bool Port_Config_GetSecondScreenFollowCam(void) {
    return sSecondScreenFollowCam;
}
extern "C" void Port_Config_SetSecondScreenFollowCam(bool on) {
    sSecondScreenFollowCam = on;
    sConfigJson["second_screen_follow_cam"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetSecondScreenCrestPins(void) {
    return sSecondScreenCrestPins;
}
extern "C" void Port_Config_SetSecondScreenCrestPins(bool on) {
    sSecondScreenCrestPins = on;
    sConfigJson["second_screen_windcrest_pins"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetSecondScreenFloorReturn(void) {
    return sSecondScreenFloorReturn;
}
extern "C" void Port_Config_SetSecondScreenFloorReturn(bool on) {
    sSecondScreenFloorReturn = on;
    sConfigJson["second_screen_floor_auto_return"] = on;
    SaveConfig();
}
extern "C" int Port_Config_GetSecondScreenBackdrop(void) {
    return sSecondScreenBackdrop;
}
extern "C" void Port_Config_SetSecondScreenBackdrop(int style) {
    sSecondScreenBackdrop = style;
    sConfigJson["second_screen_backdrop"] = style;
    SaveConfig();
}
extern "C" bool Port_Config_GetSecondScreenSwap(void) {
    return sSecondScreenSwap;
}
extern "C" void Port_Config_SetSecondScreenSwap(bool on) {
    sSecondScreenSwap = on;
    sConfigJson["second_screen_swap"] = on;
    /* Written straight through: the Java launcher re-reads config.json on
     * the next cold start, so an unsaved toggle would silently do nothing. */
    SaveConfig();
}
extern "C" bool Port_Config_GetHideTopHud(void) {
    return sHideTopHud;
}
extern "C" void Port_Config_SetHideTopHud(bool on) {
    sHideTopHud = on;
    sConfigJson["hide_top_hud"] = on;
    SaveConfig();
}

/* ---- Speedrun practice mode ------------------------------------------- */
extern "C" bool Port_Config_GetPracticeShowTimer(void) {
    return sPracticeShowTimer;
}
extern "C" void Port_Config_SetPracticeShowTimer(bool on) {
    sPracticeShowTimer = on;
    sConfigJson["practice_show_timer"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetPracticeShowInputs(void) {
    return sPracticeShowInputs;
}
extern "C" void Port_Config_SetPracticeShowInputs(bool on) {
    sPracticeShowInputs = on;
    sConfigJson["practice_show_inputs"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetPracticeShowHistory(void) {
    return sPracticeShowHistory;
}
extern "C" void Port_Config_SetPracticeShowHistory(bool on) {
    sPracticeShowHistory = on;
    sConfigJson["practice_show_history"] = on;
    SaveConfig();
}
extern "C" float Port_Config_GetPracticeSlowmo(void) {
    return sPracticeSlowmo;
}
extern "C" void Port_Config_SetPracticeSlowmo(float v) {
    if (v < 0.05f)
        v = 0.05f;
    if (v > 1.0f)
        v = 1.0f;
    sPracticeSlowmo = v;
    sConfigJson["practice_slowmo"] = v;
    SaveConfig();
}

/* ---- Persisted runtime toggles (issue #146) --------------------------- */
extern "C" bool Port_Config_GetDiscordRpc(void) {
    return sDiscordRpc;
}
extern "C" void Port_Config_SetDiscordRpc(bool on) {
    sDiscordRpc = on;
    sConfigJson["discord_rpc"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetVSync(void) {
    return sVSyncCfg;
}
extern "C" void Port_Config_SetVSync(bool on) {
    sVSyncCfg = on;
    sConfigJson["vsync"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetGpuRaster(void) {
    return sGpuRaster;
}
extern "C" void Port_Config_SetGpuRaster(bool on) {
    sGpuRaster = on;
    sConfigJson["gpu_raster"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetGpuRasterGles(void) {
    return sGpuRasterGles;
}
extern "C" void Port_Config_SetGpuRasterGles(bool on) {
    sGpuRasterGles = on;
    sConfigJson["gpu_raster_gles"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetColorCorrection(void) {
    return sColorCorrect;
}
extern "C" void Port_Config_SetColorCorrection(bool on) {
    sColorCorrect = on;
    sConfigJson["color_correction"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetLcdPersistence(void) {
    return sLcdPersist;
}
extern "C" void Port_Config_SetLcdPersistence(bool on) {
    sLcdPersist = on;
    sConfigJson["lcd_persistence"] = on;
    SaveConfig();
}
extern "C" float Port_Config_GetLcdPersistenceRho(void) {
    return sLcdPersistRho;
}
extern "C" void Port_Config_SetLcdPersistenceRho(float v) {
    sLcdPersistRho = v;
    sConfigJson["lcd_persistence_rho"] = v;
    SaveConfig();
}
extern "C" bool Port_Config_GetRibbonEnabled(void) {
    return sRibbonCfg;
}
extern "C" void Port_Config_SetRibbonEnabled(bool on) {
    sRibbonCfg = on;
    sConfigJson["ribbon_mode"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetMenuHintSeen(void) {
    return sMenuHintSeen;
}
extern "C" void Port_Config_SetMenuHintSeen(bool seen) {
    if (sMenuHintSeen == seen)
        return;
    sMenuHintSeen = seen;
    sConfigJson["menu_hint_seen"] = seen;
    SaveConfig();
}
extern "C" bool Port_Config_GetHoldToAdvanceText(void) {
    return sHoldAdvanceText;
}
extern "C" void Port_Config_SetHoldToAdvanceText(bool on) {
    sHoldAdvanceText = on;
    sConfigJson["hold_advance_text"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetRollAttackMacroEnabled(void) {
    return sRollAttackMacroEnabled;
}
extern "C" void Port_Config_SetRollAttackMacroEnabled(bool on) {
    sRollAttackMacroEnabled = on;
    sConfigJson["roll_attack_macro"] = on;
    SaveConfig();
}
extern "C" float Port_Config_GetMasterVolume(void) {
    return sMasterVolume;
}
extern "C" void Port_Config_SetMasterVolume(float v) {
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    sMasterVolume = v;
    sConfigJson["master_volume"] = (double)v;
    SaveConfig();
}
extern "C" bool Port_Config_GetFullscreen(void) {
    return sFullscreen;
}
extern "C" void Port_Config_SetFullscreen(bool on) {
    sFullscreen = on;
    sConfigJson["fullscreen"] = on;
    SaveConfig();
}
extern "C" bool Port_Config_GetFullscreenHideCursor(void) {
    return sFullscreenHideCursor;
}
extern "C" void Port_Config_SetFullscreenHideCursor(bool on) {
    sFullscreenHideCursor = on;
    sConfigJson["fullscreen_hide_cursor"] = on;
    SaveConfig();
}
extern "C" float Port_Config_GetAnalogDeadzone(void) {
    return sAnalogDeadzone;
}
extern "C" void Port_Config_SetAnalogDeadzone(float v) {
    if (v < 0.0f)
        v = 0.0f;
    if (v > 0.95f)
        v = 0.95f;
    sAnalogDeadzone = v;
    sConfigJson["analog_deadzone"] = (double)v;
    SaveConfig();
}
extern "C" const char* Port_Config_GetShaderPreset(void) {
    return sShaderPreset.c_str();
}
extern "C" void Port_Config_SetShaderPreset(const char* path) {
    sShaderPreset = (path && path[0]) ? path : "";
    sConfigJson["shader_preset"] = sShaderPreset;
    SaveConfig();
}
extern "C" int Port_Config_HasRebornMask(void) {
    return sHasRebornFeatures ? 1 : 0;
}
extern "C" unsigned Port_Config_GetRebornMask(void) {
    return sRebornFeatures;
}
extern "C" void Port_Config_SetRebornMask(unsigned mask) {
    sRebornFeatures = mask;
    sHasRebornFeatures = true;
    sConfigJson["reborn_features"] = mask;
    SaveConfig();
}
