#pragma once

#include "port_types.h"
#if !defined(TMC_N64) && !defined(TMC_3DS)
#include <SDL3/SDL.h>
#endif
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_INPUT_A,
    PORT_INPUT_B,
    PORT_INPUT_SELECT,
    PORT_INPUT_START,
    PORT_INPUT_RIGHT,
    PORT_INPUT_LEFT,
    PORT_INPUT_UP,
    PORT_INPUT_DOWN,
    PORT_INPUT_R,
    PORT_INPUT_L,
    /* Extra equip buttons (port_softslots.c). Each maps to a configurable
     * key/pad/trigger and is read every frame to decide which item — if any —
     * fires through the B-dispatch path. */
    PORT_INPUT_SOFT_X,
    PORT_INPUT_SOFT_Y,
    PORT_INPUT_SOFT_L2,
    PORT_INPUT_SOFT_R2,
    /* One-button roll attack (port_roll_attack_macro.c). Default keyboard D. */
    PORT_INPUT_ROLL_ATTACK,
    PORT_INPUT_COUNT,
} PortInput;

void Port_Config_Load(const char* path);
u8 Port_Config_WindowScale(void);
const char* Port_Config_UpscaleMethod(void);
u64 Port_Config_FrameTimeNs(void);
u32 Port_Config_TargetFps(void);
u64 Port_Config_TickTimeNs(void);
bool Port_Config_GetDecoupleRender(void);
void Port_Config_SetDecoupleRender(bool on);
bool Port_Config_GetShowFps(void);
void Port_Config_SetShowFps(bool on);
/* Android touch overlay master switch. Off both hides it and stops it
 * claiming touches, so nothing sits between the player and the game. */
bool Port_Config_GetTouchControls(void);
void Port_Config_SetTouchControls(bool on);
bool Port_Config_PortSettingsMenuEnabled(void);
void Port_Config_SetWindowScale(u8 scale);
void Port_Config_SetUpscaleMethod(const char* method);
void Port_Config_SetTargetFps(u32 fps);
void Port_Config_CycleTargetFps(int direction);

/* Internal render-resolution multiplier. The PPU normally renders at the
 * GBA-native 240x160; with scale=N>1 it produces a 240*N by 160*N
 * framebuffer, with sub-pixel sampling on affine paths (OAM affine,
 * mode2 BG2, mode7) so rotated layers and scaled sprites stop staircase-
 * aliasing. Text BGs and non-affine sprites are simply S*S nearest-
 * replicated — pixel-art has no information to recover at higher density.
 * Range 1..10; port_ppu.cpp backs the scaled framebuffer with a fixed
 * BSS scratch pool so scale changes do not allocate in the frame loop. */
u8 Port_Config_InternalScale(void);
void Port_Config_SetInternalScale(u8 scale);
void Port_Config_CycleInternalScale(int direction);

typedef enum {
    PORT_TOUCH_SCHEME_JOYSTICK = 0,
    PORT_TOUCH_SCHEME_DPAD = 1,
} PortTouchScheme;
PortTouchScheme Port_Config_TouchScheme(void);
void Port_Config_SetTouchScheme(PortTouchScheme scheme);
void Port_Config_CycleTouchScheme(int direction);

/* Touch-overlay tuning (Android). Scale multiplies every control's
 * layout unit (0.60..1.60); opacity multiplies every control's alpha
 * (0.30..1.50, >1 = more opaque than the stock look). Persisted as
 * touch_scale / touch_opacity in config.json. */
float Port_Config_TouchScale(void);
void Port_Config_SetTouchScale(float scale);
float Port_Config_TouchOpacity(void);
void Port_Config_SetTouchOpacity(float opacity);

/* True widescreen reveal is still WIP. The build-time
 * --widescreen_width=N only reserves the larger framebuffer; this runtime
 * switch decides whether gameplay uses the wider camera/reveal or falls
 * back to a native 240x160 frame. No effect in native-width builds. */
bool Port_Config_WidescreenEnabled(void);
/* 3DS: `gpu_renderer=0` in tmc3ds.ini forces the software rasterizer, so a
 * console session can compare the two renderers without a rebuild. */
bool Port_Config_GpuRenderer(void);
/* 3DS: `gpu_frame_sync=0` stops the GPU frame waiting for the previous one. */
bool Port_Config_GpuFrameSync(void);
/* 3DS: `gpu_viewport_offset=0` draws the frame at the bottom of the target. */
bool Port_Config_GpuViewportOffset(void);
/* 3DS: 0 = no vertical scissor, 1 = bottom-up (default), 2 = top-down. */
int Port_Config_GpuScissorMode(void);
/* 3DS: `gpu_stencil=0` draws every batch, ignoring window/blend masking. */
bool Port_Config_GpuStencil(void);
int Port_Config_AudioCore(void);
int Port_Config_BottomCore(void);
/* 3DS: `bottom_map_skip=0` restores an unconditional MAP-tab repaint. */
bool Port_Config_BottomMapSkip(void);
/* 3DS: `vblank_phase_lock=1` waits for the next VBlank instead of accepting an
 * already-pending one. Experiment; see Platform3DS_WaitForVBlank. */
bool Port_Config_VblankPhaseLock(void);
/* 3DS: interpolation for offloaded NDSP voices. Default 1 (linear) matches the
 * software mix on channel 0; 0 selects NDSP_INTERP_NONE. */
bool Port_Config_AudioDspInterpLinear(void);
/* 3DS: `frame_log=1` re-enables the per-120-frame SD diagnostic line. Off by
 * default; it costs main-thread SD I/O inside the presentation span. */
bool Port_Config_FrameLog(void);
bool Port_Config_GpuStaticQuad(void);
bool Port_Config_BottomRgb565(void);
bool Port_Config_GpuShortVertices(void);
bool Port_Config_AudioDsp(void);
bool Port_Config_AudioDspPcm(void);
void Port_Config_SetWidescreenEnabled(bool enabled);
void Port_Config_ToggleWidescreen(void);

/* Console-Parity mode. When ON the port is held provably equivalent to real
 * GBA hardware for speedrun integrity: the sub-frame input edge cache is
 * disabled (hardware 1-frame input granularity), save-states (F1-F6) are
 * inert, widescreen is forced off (no early off-screen-AI RNG advance), and
 * frame pacing locks to the authentic 59.7275 Hz. Default OFF. Persisted to
 * config.json as "console_parity"; --console-parity forces it on at launch. */
bool Port_Config_GetConsoleParity(void);
void Port_Config_SetConsoleParity(bool on);
void Port_Config_ToggleConsoleParity(void);

/* Aspect-ratio mode for the on-screen viewport. The game frame is always
 * rendered at its own aspect (3:2 native, wider in true widescreen) — this
 * knob picks the *stage* it sits inside. The area between frame and stage
 * is filled per PortBgFill. Native means "no constraint": the stage spans
 * the whole window, so the frame fills as much of it as its aspect allows
 * and the fill covers the rest — the default. Fixed-ratio modes constrain
 * the stage (e.g. pin a 16:9 play area on an ultrawide monitor); outside
 * the stage is always black. */
typedef enum {
    PORT_ASPECT_NATIVE_3_2 = 0,
    PORT_ASPECT_WIDESCREEN_16_9 = 1,
    PORT_ASPECT_ULTRAWIDE_21_9 = 2,
    PORT_ASPECT_SUPER_ULTRAWIDE_32_9 = 3,
    PORT_ASPECT_COUNT,
} PortAspectMode;
PortAspectMode Port_Config_AspectMode(void);
const char* Port_Config_AspectModeName(PortAspectMode mode);
void Port_Config_SetAspectMode(PortAspectMode mode);
void Port_Config_CycleAspectMode(int direction);

/* Fill style for the area around the GBA frame (the "pillar bars" or
 * "stage background"). Blurred is the default: it stretches a
 * linearly-filtered copy of the current frame across the stage for a soft
 * halo / "ambient mode" effect, so fixed-canvas scenes (title, one-screen
 * rooms) fill the monitor without distorting the game. Black restores the
 * historical plain bars. Solid uses the persisted RGB triple from
 * Port_Config_BgFillColor*. */
typedef enum {
    PORT_BG_FILL_BLACK = 0,
    PORT_BG_FILL_SOLID_COLOR = 1,
    PORT_BG_FILL_BLURRED_FRAME = 2,
    PORT_BG_FILL_COUNT,
} PortBgFill;
PortBgFill Port_Config_BgFill(void);
const char* Port_Config_BgFillName(PortBgFill fill);
void Port_Config_SetBgFill(PortBgFill fill);
void Port_Config_CycleBgFill(int direction);
/* RGB triple, 0..255, used when BgFill == SOLID_COLOR. */
void Port_Config_BgFillColor(u8* r, u8* g, u8* b);
void Port_Config_SetBgFillColor(u8 r, u8 g, u8 b);

/* Renderer backend selection. The port has two presentation paths:
 * SDL_GPU (shader pipeline, required for CRT/LCD filters and the
 * .glslp preset runtime) and SDL_Renderer (software composite via
 * SDL's 2D API). AUTO is the historical default — try GPU first,
 * fall back to SDL_Renderer if GPU init fails. Changing this needs
 * a restart to take effect; the window's swapchain owner is set
 * once at Port_PPU_Init time. */
typedef enum {
    PORT_RENDER_BACKEND_AUTO = 0,
    PORT_RENDER_BACKEND_SOFTWARE = 1,
    PORT_RENDER_BACKEND_GPU = 2,
    PORT_RENDER_BACKEND_COUNT,
} PortRenderBackend;
PortRenderBackend Port_Config_RenderBackend(void);
const char* Port_Config_RenderBackendName(PortRenderBackend b);
void Port_Config_SetRenderBackend(PortRenderBackend b);
void Port_Config_CycleRenderBackend(int direction);

/* Text-to-speech accessibility settings — read by port_tts at startup
 * and re-written on every setter. All persisted to config.json. */
bool Port_Config_GetTtsEnabled(void);
void Port_Config_SetTtsEnabled(bool on);
float Port_Config_GetTtsRate(void);
void Port_Config_SetTtsRate(float v);
float Port_Config_GetTtsPitch(void);
void Port_Config_SetTtsPitch(float v);
float Port_Config_GetTtsVolume(void);
void Port_Config_SetTtsVolume(float v);
const char* Port_Config_GetTtsVoice(void);
void Port_Config_SetTtsVoice(const char* v);
const char* Port_Config_GetTtsLanguage(void);
void Port_Config_SetTtsLanguage(const char* v);

/* Accessibility passive-cue toggles (Phase 2) — read at Port_A11y_Init,
 * re-written on every setter. All persisted to config.json. */
bool Port_Config_GetA11yCues(void);
void Port_Config_SetA11yCues(bool on);
bool Port_Config_GetA11yFootsteps(void);
void Port_Config_SetA11yFootsteps(bool on);
bool Port_Config_GetA11yHazards(void);
void Port_Config_SetA11yHazards(bool on);
bool Port_Config_GetA11yRadar(void);
void Port_Config_SetA11yRadar(bool on);
bool Port_Config_GetA11yWalls(void);
void Port_Config_SetA11yWalls(bool on);

/* Second-screen (AYN Thor bottom panel) map-behavior toggles — the rows on
 * that screen's own settings tab. Read by port_second_screen.c each frame,
 * written from its tap handler. All persisted to config.json, default on. */
bool Port_Config_GetSecondScreenFollowCam(void);
void Port_Config_SetSecondScreenFollowCam(bool on);
bool Port_Config_GetSecondScreenCrestPins(void);
void Port_Config_SetSecondScreenCrestPins(bool on);
bool Port_Config_GetSecondScreenFloorReturn(void);
void Port_Config_SetSecondScreenFloorReturn(bool on);
/* The panel's backdrop dressing, as an SS_BACKDROP_* style id (see
 * port_second_screen_theme.h). Stored as a plain int rather than an enum
 * so this header stays independent of the second screen's; the panel is
 * what sanitises the stored value. Default 0 = the menu's parchment. */
int Port_Config_GetSecondScreenBackdrop(void);
void Port_Config_SetSecondScreenBackdrop(int style);
/* Swap the game and the panel between the two screens. Applied by the
 * Android shell at launch (it reads config.json before starting the game
 * activity, and targets the display this flag asks for), never live — so
 * the panel row that writes it shows RESTART until it actually took. */
bool Port_Config_GetSecondScreenSwap(void);
void Port_Config_SetSecondScreenSwap(bool on);
#ifdef TMC_3DS
typedef enum {
    PORT_3DS_ASPECT_WIDE = 0,
    PORT_3DS_ASPECT_ORIGINAL,
    PORT_3DS_ASPECT_STRETCH,
    PORT_3DS_ASPECT_COUNT,
} Port3DSAspectRatio;

typedef enum {
    PORT_3DS_DISPLAY_PIXEL_PERFECT = 0,
    PORT_3DS_DISPLAY_SCALED,
    PORT_3DS_DISPLAY_BLUR,
    PORT_3DS_DISPLAY_COUNT,
} Port3DSDisplayStyle;

int Port_Config_Get3DSAspectRatio(void);
const char* Port_Config_Get3DSAspectRatioName(void);
void Port_Config_Cycle3DSAspectRatio(void);
int Port_Config_Get3DSDisplayStyle(void);
const char* Port_Config_Get3DSDisplayStyleName(void);
void Port_Config_Cycle3DSDisplayStyle(void);

/* New 3DS C-stick turbo multiplier. The device supports 2x through 5x and
 * applies changes immediately to the frame-boundary scheduler. */
unsigned Port_Config_GetTurboMultiplier(void);
void Port_Config_SetTurboMultiplier(unsigned multiplier);
#endif
/* Hide the game's own top-screen HUD (hearts/rupees) while the second
 * screen carries vitals. Engine gate sits with DrawUIElements; gameplay
 * only — menus keep their UI. Default off. */
bool Port_Config_GetHideTopHud(void);
void Port_Config_SetHideTopHud(bool on);

/* Speedrun practice mode (port_practice.c). Overlay toggles + slow-mo factor
 * (0.05..1.0; 1.0 = normal speed). All persisted to config.json. */
bool Port_Config_GetPracticeShowTimer(void);
void Port_Config_SetPracticeShowTimer(bool on);
bool Port_Config_GetPracticeShowInputs(void);
void Port_Config_SetPracticeShowInputs(bool on);
bool Port_Config_GetPracticeShowHistory(void);
void Port_Config_SetPracticeShowHistory(bool on);
float Port_Config_GetPracticeSlowmo(void);
void Port_Config_SetPracticeSlowmo(float v);

/* Persisted runtime toggles (issue #146). Applied at startup; setters write
 * config.json immediately. reborn_features is a bitmask over RebornFeature;
 * HasRebornMask reports whether the user ever overrode the compile defaults. */
bool Port_Config_GetDiscordRpc(void);
void Port_Config_SetDiscordRpc(bool on);
bool Port_Config_GetVSync(void);
void Port_Config_SetVSync(bool on);
/* GPU PPU rasterizer toggle (docs/gpu-rasterizer-design.md). Default on; the
 * CPU rasterizer is the automatic fallback. No-op on non-GPU builds. */
bool Port_Config_GetGpuRaster(void);
void Port_Config_SetGpuRaster(bool on);
/* GLES compute rasterizer — OPT-IN (default off); ~5x slower than CPU on the
 * Adreno 405. Only consulted when the Vulkan raster is unavailable. */
bool Port_Config_GetGpuRasterGles(void);
void Port_Config_SetGpuRasterGles(bool on);

/* GBA-LCD display transforms (F8 Display menu). */
bool Port_Config_GetColorCorrection(void);
void Port_Config_SetColorCorrection(bool on);
bool Port_Config_GetLcdPersistence(void);
void Port_Config_SetLcdPersistence(bool on);
float Port_Config_GetLcdPersistenceRho(void);
void Port_Config_SetLcdPersistenceRho(float v);
bool Port_Config_GetRibbonEnabled(void);
void Port_Config_SetRibbonEnabled(bool on);
/* One-shot "Press F8 for settings" discovery hint: false until the settings
 * menu is first opened, then persisted true so it never nags again. */
bool Port_Config_GetMenuHintSeen(void);
void Port_Config_SetMenuHintSeen(bool seen);
bool Port_Config_GetHoldToAdvanceText(void);
void Port_Config_SetHoldToAdvanceText(bool on);
bool Port_Config_GetRollAttackMacroEnabled(void);
void Port_Config_SetRollAttackMacroEnabled(bool on);
float Port_Config_GetMasterVolume(void);
void Port_Config_SetMasterVolume(float v);
bool Port_Config_GetFullscreen(void);
void Port_Config_SetFullscreen(bool on);
/* Hide the OS mouse cursor while the window is fullscreen (default on). When
 * off, the cursor stays visible in fullscreen. Read by Port_PPU's cursor-
 * visibility policy on every fullscreen transition. */
bool Port_Config_GetFullscreenHideCursor(void);
void Port_Config_SetFullscreenHideCursor(bool on);
const char* Port_Config_GetShaderPreset(void);
void Port_Config_SetShaderPreset(const char* path);
int Port_Config_HasRebornMask(void);
unsigned Port_Config_GetRebornMask(void);
void Port_Config_SetRebornMask(unsigned mask);

/* Randomizer persistence (issue #155): the file-select "Enable Randomizer
 * Mode" toggle, the built-in graph settings, and .logic define overrides
 * survive restarts. They reset to vanilla defaults only when the rando
 * toggle is switched off (rando_file_menu.c owns that policy). */
bool Port_Config_GetRandoEnabled(void);
void Port_Config_SetRandoEnabled(bool on);
bool Port_Config_GetRandoGlitchless(void);
bool Port_Config_GetRandoObscure(void);
bool Port_Config_GetRandoKinstones(void);
bool Port_Config_GetRandoEntrances(void);
bool Port_Config_GetRandoDojos(void);
bool Port_Config_GetRandoOpenWorld(void);
int Port_Config_GetRandoItemPool(void);
bool Port_Config_GetRandoHomewarp(void);
bool Port_Config_GetRandoStartSword(void);
bool Port_Config_GetRandoEarlyCrests(void);
bool Port_Config_GetRandoInstantText(void);
int Port_Config_GetRandoTunicColor(void);
int Port_Config_GetRandoHeartColor(void);
void Port_Config_SetRandoSettings(bool glitchless, bool obscure, bool kinstones, bool entrances, bool dojos,
                                  bool open_world, int item_pool, bool homewarp, bool start_sword, bool early_crests,
                                  bool instant_text, int tunic_color, int heart_color);
/* Glitch-logic trick bitmask (RANDO_TRICK_*) and verifier accessibility level
 * (RandoAccessibility). Persisted independently of the positional setter above
 * so adding logic tiers never reshuffles that argument list. */
int Port_Config_GetRandoTricks(void);
void Port_Config_SetRandoTricks(int tricks);
int Port_Config_GetRandoAccessibility(void);
void Port_Config_SetRandoAccessibility(int accessibility);
/* Whether big keys / maps / compasses join the shuffle (default off = pinned
 * vanilla). Persisted independently of the positional setter above. */
bool Port_Config_GetRandoDungeonItems(void);
void Port_Config_SetRandoDungeonItems(bool dungeon_items);

void Port_Config_OpenGamepads(void);
#if !defined(TMC_N64) && !defined(TMC_3DS)
void Port_Config_HandleEvent(const SDL_Event* e);
/* True when an SDL event is a fresh press bound to `input` (see .cpp). */
bool Port_Config_EventIsInputDown(const SDL_Event* e, PortInput input);
#endif
bool Port_Config_InputPressed(PortInput input);
void Port_Config_CloseGamepads(void);

bool Port_Config_InputEdgePressed(PortInput input);
/* Soft-slot input poll, indexed 0..3 (X, Y, L2, R2). */
bool Port_Config_SoftSlotPressed(int slot);

/* Raw left-analog-stick reading from the first attached gamepad, in
 * the range [-1, 1] per axis (positive Y = downward on screen, matching
 * TMC's top-left coord system). Returns false with outputs untouched
 * when no pad is attached. Used by the 360° analog movement toggle in
 * src/code_0805EC04.c::UpdatePlayerInput. */
bool Port_Config_GetLeftStick(float* outX, float* outY);

/* 360° analog-movement deadzone magnitude, range [0..0.95]. Left-stick
 * displacements below this keep the D-pad authoritative (no phantom walking
 * with a thumb resting on the stick). Read by port_analog_movement.c; only
 * meaningful when REBORN_FEAT_ANALOG_360_MOVEMENT is enabled. Default 0.30. */
float Port_Config_GetAnalogDeadzone(void);
void Port_Config_SetAnalogDeadzone(float v);

/* Clear the per-input "pressed this frame" edge cache. Call after the
 * port has committed KEYINPUT and the engine has read it, so the next
 * frame's polled state isn't stuck reporting the previous tap. */
void Port_Config_ClearInputEdges(void);

/* Test-only: stamp the per-frame edge cache for `input` (see
 * Port_Config_TestForceEdge in the .cpp). Used exclusively by env-gated
 * repro harnesses; the bit is wiped by Port_Config_ClearInputEdges() each
 * frame, so it never persists into normal play. */
void Port_Config_TestForceEdge(PortInput input);

/* Production edge-stamp used by the touch overlay: latch a button press onto
 * the per-frame edge cache so a sub-frame tap (pointer DOWN+UP in one poll
 * batch) still registers for one game frame. Same cache/wipe as above. */
void Port_Config_StampInputEdge(PortInput input);

int Port_Config_PreferredRegion(void);
void Port_Config_SetPreferredRegion(int region);
int Port_Config_PreferredLanguage(void);
void Port_Config_SetPreferredLanguage(int lang);
#ifdef launcher
void Port_Config_SetPortSettingsMenuEnabled(bool enabled);
const char* Port_Config_InputUiLabel(PortInput input);
void Port_Config_FormatBindingsLine(PortInput input, char* out, size_t outCap);
void Port_Config_SetKeyboardBindExclusive(PortInput input, int sdl_keycode);
void Port_Config_FormatGamepadBindingsLine(PortInput input, char* out, size_t outCap);
void Port_Config_SetGamepadBindExclusive(PortInput input, int sdl_gamepad_button);
#endif

#ifdef __cplusplus
}
#endif
