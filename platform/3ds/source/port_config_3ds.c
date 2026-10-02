#include "port_runtime_config.h"
#include "platform_3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* BottomWorkerMain renders the settings panel while the main thread handles
 * taps and persists changes. Keep every value shared by those two threads
 * atomic; SaveConfig still performs its file I/O on the main thread and never
 * holds a compositor lock while touching the SD card. */
static _Atomic bool sShowFps;
static _Atomic bool sFollow = true;
static _Atomic bool sCrests = true;
static _Atomic bool sFloorReturn = true;
static _Atomic bool sHideHud;
static _Atomic bool sHoldText;
static _Atomic bool sColorCorrection;
static _Atomic bool sAutosave;
static bool sConsoleParity;
static _Atomic Port3DSAspectRatio sAspectRatio = PORT_3DS_ASPECT_STRETCH;
static _Atomic Port3DSDisplayStyle sDisplayStyle = PORT_3DS_DISPLAY_BLUR;
/* The desktop file-select overlay is rendered on the gameplay screen and
 * has no useful 3DS interaction path. Keep the native second-screen UI
 * separate and leave this desktop-only overlay disabled. */
static bool sPortSettings = false;
static bool sVsync = true;
static _Atomic float sVolume = 1.0f;
static _Atomic int sBackdrop;
static _Atomic unsigned sTurboMultiplier = 5;
static bool sConfigLoaded;
static char sConfigPath[256] = "tmc3ds.ini";

static bool ParseBool(const char* value) {
    return value != NULL && (value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
                             value[0] == 'y' || value[0] == 'Y');
}

static const char* AspectRatioConfigName(Port3DSAspectRatio mode) {
    static const char* const names[PORT_3DS_ASPECT_COUNT] = { "wide", "original", "stretch" };
    return mode >= 0 && mode < PORT_3DS_ASPECT_COUNT ? names[mode] : names[PORT_3DS_ASPECT_STRETCH];
}

static const char* DisplayStyleConfigName(Port3DSDisplayStyle style) {
    static const char* const names[PORT_3DS_DISPLAY_COUNT] = { "pixel-perfect", "scaled", "blur" };
    return style >= 0 && style < PORT_3DS_DISPLAY_COUNT ? names[style]
                                                        : names[PORT_3DS_DISPLAY_BLUR];
}

static Port3DSAspectRatio ParseAspectRatio(const char* value) {
    for (int i = 0; i < PORT_3DS_ASPECT_COUNT; ++i) {
        if (strcmp(value, AspectRatioConfigName((Port3DSAspectRatio)i)) == 0) {
            return (Port3DSAspectRatio)i;
        }
    }
    return PORT_3DS_ASPECT_STRETCH;
}

static Port3DSDisplayStyle ParseDisplayStyle(const char* value) {
    for (int i = 0; i < PORT_3DS_DISPLAY_COUNT; ++i) {
        if (strcmp(value, DisplayStyleConfigName((Port3DSDisplayStyle)i)) == 0) {
            return (Port3DSDisplayStyle)i;
        }
    }
    return PORT_3DS_DISPLAY_BLUR;
}

static void SaveConfig(void) {
    if (!sConfigLoaded) return;

    char temp[sizeof(sConfigPath) + 8];
    char backup[sizeof(sConfigPath) + 8];
    snprintf(temp, sizeof(temp), "%s.tmp", sConfigPath);
    snprintf(backup, sizeof(backup), "%s.bak", sConfigPath);

    FILE* file = fopen(temp, "wb");
    if (!file) return;
    fprintf(file, "# The Minish Cap 3DS runtime settings\n");
    fprintf(file, "show_fps=%u\n", sShowFps ? 1u : 0u);
    fprintf(file, "follow_cam=%u\n", sFollow ? 1u : 0u);
    fprintf(file, "windcrest_pins=%u\n", sCrests ? 1u : 0u);
    fprintf(file, "floor_auto_return=%u\n", sFloorReturn ? 1u : 0u);
    fprintf(file, "hide_top_hud=%u\n", sHideHud ? 1u : 0u);
    fprintf(file, "hold_to_advance_text=%u\n", sHoldText ? 1u : 0u);
    fprintf(file, "color_correction=%u\n", sColorCorrection ? 1u : 0u);
    fprintf(file, "autosave=%u\n", sAutosave ? 1u : 0u);
    fprintf(file, "widescreen=%u\n", sAspectRatio == PORT_3DS_ASPECT_WIDE ? 1u : 0u);
    fprintf(file, "screen_aspect=%s\n", AspectRatioConfigName(sAspectRatio));
    fprintf(file, "display_style=%s\n", DisplayStyleConfigName(sDisplayStyle));
    fprintf(file, "master_volume=%.2f\n", (double)sVolume);
    fprintf(file, "panel_backdrop=%d\n", sBackdrop);
    fprintf(file, "turbo_multiplier=%u\n", sTurboMultiplier);

    /* A synchronous SD flush can stall the 3DS main thread for seconds on
     * every Settings change. The temporary file plus close/rename/backup
     * sequence still protects this recoverable preferences file. */
    bool ok = fflush(file) == 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        remove(temp);
        return;
    }

    remove(backup);
    bool hadCurrent = access(sConfigPath, F_OK) == 0;
    if (hadCurrent && rename(sConfigPath, backup) != 0) {
        remove(temp);
        return;
    }
    if (rename(temp, sConfigPath) != 0) {
        if (hadCurrent) rename(backup, sConfigPath);
        remove(temp);
        return;
    }
    remove(backup);
}

void Port_Config_Load(const char* path) {
    if (path != NULL && path[0] != '\0') snprintf(sConfigPath, sizeof(sConfigPath), "%s", path);

    bool hasScreenAspect = false;
    FILE* file = fopen(sConfigPath, "rb");
    if (file) {
        char line[160];
        while (fgets(line, sizeof(line), file) != NULL) {
            char key[64];
            char value[64];
            if (line[0] == '#' || sscanf(line, " %63[^=]=%63s", key, value) != 2) continue;
            if (strcmp(key, "show_fps") == 0) sShowFps = ParseBool(value);
            else if (strcmp(key, "follow_cam") == 0) sFollow = ParseBool(value);
            else if (strcmp(key, "windcrest_pins") == 0) sCrests = ParseBool(value);
            else if (strcmp(key, "floor_auto_return") == 0) sFloorReturn = ParseBool(value);
            else if (strcmp(key, "hide_top_hud") == 0) sHideHud = ParseBool(value);
            else if (strcmp(key, "hold_to_advance_text") == 0) sHoldText = ParseBool(value);
            else if (strcmp(key, "color_correction") == 0) sColorCorrection = ParseBool(value);
            else if (strcmp(key, "autosave") == 0) sAutosave = ParseBool(value);
            else if (strcmp(key, "widescreen") == 0 && !hasScreenAspect)
                sAspectRatio = ParseBool(value) ? PORT_3DS_ASPECT_WIDE : PORT_3DS_ASPECT_ORIGINAL;
            else if (strcmp(key, "screen_aspect") == 0) {
                sAspectRatio = ParseAspectRatio(value);
                hasScreenAspect = true;
            } else if (strcmp(key, "display_style") == 0) {
                sDisplayStyle = ParseDisplayStyle(value);
            }
            else if (strcmp(key, "master_volume") == 0) sVolume = strtof(value, NULL);
            else if (strcmp(key, "panel_backdrop") == 0) sBackdrop = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "turbo_multiplier") == 0) sTurboMultiplier = (unsigned)strtoul(value, NULL, 10);
        }
        fclose(file);
    }

    if (sVolume < 0.0f) sVolume = 0.0f;
    if (sVolume > 1.0f) sVolume = 1.0f;
    if (sBackdrop < 0 || sBackdrop > 6) sBackdrop = 0;
    if (sTurboMultiplier < 2 || sTurboMultiplier > 5) sTurboMultiplier = 5;
    Platform3DS_SetTurboMultiplier(sTurboMultiplier);
    sConfigLoaded = true;
}
void Port_Config_SetActiveSaveProfile(const char* path) { (void)path; }
u8 Port_Config_WindowScale(void) { return 1; }
const char* Port_Config_UpscaleMethod(void) { return "nearest"; }
u64 Port_Config_FrameTimeNs(void) { return 16666667ULL; }
u32 Port_Config_TargetFps(void) { return 60; }
u64 Port_Config_TickTimeNs(void) { return 16666667ULL; }
bool Port_Config_GetDecoupleRender(void) { return false; }
void Port_Config_SetDecoupleRender(bool on) { (void)on; }
bool Port_Config_GetShowFps(void) { return sShowFps; }
void Port_Config_SetShowFps(bool on) { sShowFps = on; SaveConfig(); }
bool Port_Config_GetTouchControls(void) { return false; }
void Port_Config_SetTouchControls(bool on) { (void)on; }
bool Port_Config_PortSettingsMenuEnabled(void) { return sPortSettings; }
void Port_Config_SetWindowScale(u8 scale) { (void)scale; }
void Port_Config_SetUpscaleMethod(const char* method) { (void)method; }
void Port_Config_SetTargetFps(u32 fps) { (void)fps; }
void Port_Config_CycleTargetFps(int direction) { (void)direction; }
u8 Port_Config_InternalScale(void) { return 1; }
void Port_Config_SetInternalScale(u8 scale) { (void)scale; }
void Port_Config_CycleInternalScale(int direction) { (void)direction; }
PortTouchScheme Port_Config_TouchScheme(void) { return PORT_TOUCH_SCHEME_DPAD; }
void Port_Config_SetTouchScheme(PortTouchScheme scheme) { (void)scheme; }
void Port_Config_CycleTouchScheme(int direction) { (void)direction; }
float Port_Config_TouchScale(void) { return 1.0f; }
void Port_Config_SetTouchScale(float scale) { (void)scale; }
float Port_Config_TouchOpacity(void) { return 1.0f; }
void Port_Config_SetTouchOpacity(float opacity) { (void)opacity; }
bool Port_Config_WidescreenEnabled(void) { return sAspectRatio == PORT_3DS_ASPECT_WIDE; }
void Port_Config_SetWidescreenEnabled(bool enabled) {
    sAspectRatio = enabled ? PORT_3DS_ASPECT_WIDE : PORT_3DS_ASPECT_ORIGINAL;
    SaveConfig();
}
void Port_Config_ToggleWidescreen(void) { Port_Config_SetWidescreenEnabled(!Port_Config_WidescreenEnabled()); }
bool Port_Config_GetConsoleParity(void) { return sConsoleParity; }
void Port_Config_SetConsoleParity(bool on) { sConsoleParity = on; }
void Port_Config_ToggleConsoleParity(void) { sConsoleParity = !sConsoleParity; }
PortAspectMode Port_Config_AspectMode(void) { return PORT_ASPECT_NATIVE_3_2; }
const char* Port_Config_AspectModeName(PortAspectMode mode) { (void)mode; return "Native"; }
void Port_Config_SetAspectMode(PortAspectMode mode) { (void)mode; }
void Port_Config_CycleAspectMode(int direction) { (void)direction; }
PortBgFill Port_Config_BgFill(void) { return PORT_BG_FILL_BLACK; }
const char* Port_Config_BgFillName(PortBgFill fill) { (void)fill; return "Black"; }
void Port_Config_SetBgFill(PortBgFill fill) { (void)fill; }
void Port_Config_CycleBgFill(int direction) { (void)direction; }
void Port_Config_BgFillColor(u8* r, u8* g, u8* b) { *r = *g = *b = 0; }
void Port_Config_SetBgFillColor(u8 r, u8 g, u8 b) { (void)r; (void)g; (void)b; }
PortRenderBackend Port_Config_RenderBackend(void) { return PORT_RENDER_BACKEND_SOFTWARE; }
const char* Port_Config_RenderBackendName(PortRenderBackend b) { (void)b; return "3DS CPU"; }
void Port_Config_SetRenderBackend(PortRenderBackend b) { (void)b; }
void Port_Config_CycleRenderBackend(int direction) { (void)direction; }

#define BOOL_CONFIG(name, initial) \
    bool Port_Config_Get##name(void) { return initial; } \
    void Port_Config_Set##name(bool on) { (void)on; }
BOOL_CONFIG(TtsEnabled, false)
BOOL_CONFIG(A11yCues, false)
BOOL_CONFIG(A11yFootsteps, false)
BOOL_CONFIG(A11yHazards, false)
BOOL_CONFIG(A11yRadar, false)
BOOL_CONFIG(A11yWalls, false)
BOOL_CONFIG(PracticeShowTimer, false)
BOOL_CONFIG(PracticeShowInputs, false)
BOOL_CONFIG(PracticeShowHistory, false)
BOOL_CONFIG(DiscordRpc, false)
BOOL_CONFIG(GpuRaster, false)
BOOL_CONFIG(GpuRasterGles, false)
BOOL_CONFIG(LcdPersistence, false)
BOOL_CONFIG(RibbonEnabled, false)
BOOL_CONFIG(MenuHintSeen, true)
BOOL_CONFIG(RollAttackMacroEnabled, false)
BOOL_CONFIG(Fullscreen, true)
BOOL_CONFIG(FullscreenHideCursor, false)

float Port_Config_GetTtsRate(void) { return 1.0f; }
void Port_Config_SetTtsRate(float v) { (void)v; }
float Port_Config_GetTtsPitch(void) { return 1.0f; }
void Port_Config_SetTtsPitch(float v) { (void)v; }
float Port_Config_GetTtsVolume(void) { return 1.0f; }
void Port_Config_SetTtsVolume(float v) { (void)v; }
const char* Port_Config_GetTtsVoice(void) { return ""; }
void Port_Config_SetTtsVoice(const char* v) { (void)v; }
const char* Port_Config_GetTtsLanguage(void) { return "en"; }
void Port_Config_SetTtsLanguage(const char* v) { (void)v; }
bool Port_Config_GetSecondScreenFollowCam(void) { return sFollow; }
void Port_Config_SetSecondScreenFollowCam(bool on) { sFollow = on; SaveConfig(); }
bool Port_Config_GetSecondScreenCrestPins(void) { return sCrests; }
void Port_Config_SetSecondScreenCrestPins(bool on) { sCrests = on; SaveConfig(); }
bool Port_Config_GetSecondScreenFloorReturn(void) { return sFloorReturn; }
void Port_Config_SetSecondScreenFloorReturn(bool on) { sFloorReturn = on; SaveConfig(); }
int Port_Config_GetSecondScreenBackdrop(void) { return sBackdrop; }
void Port_Config_SetSecondScreenBackdrop(int style) { sBackdrop = style; SaveConfig(); }
bool Port_Config_GetSecondScreenSwap(void) { return false; }
void Port_Config_SetSecondScreenSwap(bool on) { (void)on; }
bool Port_Config_GetHideTopHud(void) { return sHideHud; }
void Port_Config_SetHideTopHud(bool on) { sHideHud = on; SaveConfig(); }
float Port_Config_GetPracticeSlowmo(void) { return 1.0f; }
void Port_Config_SetPracticeSlowmo(float v) { (void)v; }
bool Port_Config_GetVSync(void) { return sVsync; }
void Port_Config_SetVSync(bool on) { sVsync = on; }
bool Port_Config_GetColorCorrection(void) { return sColorCorrection; }
void Port_Config_SetColorCorrection(bool on) { sColorCorrection = on; SaveConfig(); }
float Port_Config_GetLcdPersistenceRho(void) { return 0.0f; }
void Port_Config_SetLcdPersistenceRho(float v) { (void)v; }
bool Port_Config_GetHoldToAdvanceText(void) { return sHoldText; }
void Port_Config_SetHoldToAdvanceText(bool on) { sHoldText = on; SaveConfig(); }
float Port_Config_GetMasterVolume(void) { return sVolume; }
void Port_Config_SetMasterVolume(float v) {
    sVolume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    SaveConfig();
}
const char* Port_Config_GetShaderPreset(void) { return ""; }
void Port_Config_SetShaderPreset(const char* path) { (void)path; }
int Port_Config_HasRebornMask(void) { return 0; }
unsigned Port_Config_GetRebornMask(void) { return 0; }
void Port_Config_SetRebornMask(unsigned mask) { (void)mask; }
void Port_Config_OpenGamepads(void) {}
void Port_Config_CloseGamepads(void) {}

bool Port_Config_InputPressed(PortInput input) {
    const u16 keys = (u16)(~Platform3DS_ReadKeyInput()) & 0x03ff;
    static const u16 masks[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    return input < 10 && (keys & masks[input]) != 0;
}
bool Port_Config_InputEdgePressed(PortInput input) {
    const u16 keys = (u16)(~Platform3DS_ReadKeyDownInput()) & 0x03ff;
    static const u16 masks[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    return input < 10 && (keys & masks[input]) != 0;
}
bool Port_Config_SoftSlotPressed(int slot) {
    const u32 held = (u32)Platform3DS_KeysHeld();
    static const u32 masks[4] = { 1u << 10, 1u << 11, 1u << 14, 1u << 15 };
    return slot >= 0 && slot < 4 && (held & masks[slot]) != 0;
}
bool Port_Config_GetLeftStick(float* outX, float* outY) {
    float x = 0.0f;
    float y = 0.0f;
    Platform3DS_ReadCircle(&x, &y);
    if (outX) *outX = x;
    if (outY) *outY = y;
    return x != 0.0f || y != 0.0f;
}
float Port_Config_GetAnalogDeadzone(void) { return 0.3f; }
void Port_Config_SetAnalogDeadzone(float v) { (void)v; }
void Port_Config_ClearInputEdges(void) {}
void Port_Config_TestForceEdge(PortInput input) { (void)input; }
void Port_Config_StampInputEdge(PortInput input) { (void)input; }
int Port_Config_PreferredRegion(void) { return -1; }
void Port_Config_SetPreferredRegion(int region) { (void)region; }
int Port_Config_PreferredLanguage(void) { return -1; }
void Port_Config_SetPreferredLanguage(int lang) { (void)lang; }
void Port_Config_SetPortSettingsMenuEnabled(bool enabled) { sPortSettings = enabled; }

bool Port_Config_AutosaveEnabled(void) { return sAutosave; }
void Port_Config_SetAutosaveEnabled(bool enabled) { sAutosave = enabled; SaveConfig(); }
u32 Port_Config_AutosaveIntervalMs(void) { return 300000; }
void Port_Config_SetAutosaveIntervalMs(u32 ms) { (void)ms; }

unsigned Port_Config_GetTurboMultiplier(void) { return sTurboMultiplier; }
void Port_Config_SetTurboMultiplier(unsigned multiplier) {
    if (multiplier < 2) multiplier = 2;
    if (multiplier > 5) multiplier = 5;
    sTurboMultiplier = multiplier;
    Platform3DS_SetTurboMultiplier(multiplier);
    SaveConfig();
}

int Port_Config_Get3DSAspectRatio(void) { return (int)sAspectRatio; }
const char* Port_Config_Get3DSAspectRatioName(void) {
    static const char* const names[PORT_3DS_ASPECT_COUNT] = { "WIDE", "ORIGINAL", "STRETCH" };
    return names[sAspectRatio];
}
void Port_Config_Cycle3DSAspectRatio(void) {
    sAspectRatio = (Port3DSAspectRatio)((sAspectRatio + 1) % PORT_3DS_ASPECT_COUNT);
    SaveConfig();
}
int Port_Config_Get3DSDisplayStyle(void) { return (int)sDisplayStyle; }
const char* Port_Config_Get3DSDisplayStyleName(void) {
    static const char* const names[PORT_3DS_DISPLAY_COUNT] = { "PIXEL PERFECT", "SCALED", "BLUR" };
    return names[sDisplayStyle];
}
void Port_Config_Cycle3DSDisplayStyle(void) {
    sDisplayStyle = (Port3DSDisplayStyle)((sDisplayStyle + 1) % PORT_3DS_DISPLAY_COUNT);
    SaveConfig();
}
