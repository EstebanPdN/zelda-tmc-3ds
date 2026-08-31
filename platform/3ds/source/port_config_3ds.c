#include "port_runtime_config.h"
#include "platform_3ds.h"
#include "config_values_3ds.h"
#include "rando/rando.h"

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
static _Atomic Port3DSAspectRatio sAspectRatio = CONFIG_3DS_DEFAULT_ASPECT;
static _Atomic Port3DSDisplayStyle sDisplayStyle = CONFIG_3DS_DEFAULT_DISPLAY;
/* The desktop file-select overlay is rendered on the gameplay screen and
 * has no useful 3DS interaction path. Keep the native second-screen UI
 * separate and leave this desktop-only overlay disabled. */
static bool sPortSettings = false;
static bool sVsync = true;
static _Atomic float sVolume = 1.0f;
static _Atomic int sBackdrop;
static _Atomic unsigned sTurboMultiplier = 5;
static _Atomic bool sRandoEnabled;
static bool sRandoGlitchless = true;
static bool sRandoObscure;
static bool sRandoKinstones = true;
static bool sRandoEntrances;
static bool sRandoDojos = true;
static bool sRandoOpenWorld;
static bool sRandoHomewarp = true;
static bool sRandoStartSword = true;
static bool sRandoEarlyCrests = true;
static bool sRandoInstantText = true;
static bool sRandoDungeonItems;
static int sRandoItemPool;
static int sRandoTunicColor;
static int sRandoHeartColor;
static int sRandoTricks;
static int sRandoAccessibility;
static bool sConfigLoaded;
static char sConfigPath[256] = "tmc3ds.ini";
/* Lets a console session A/B the PICA200 renderer against the software
 * rasterizer without a rebuild. */
static bool sGpuRenderer = true;
/* Whether the GPU frame waits for the previous one to retire before the
 * builder rewrites the command buffers.
 *
 * Waiting is the correct thing in principle -- the GPU reads those buffers
 * asynchronously -- but it is off by default because it coincided with a
 * console showing a black top screen, and a renderer that draws is worth more
 * than one that is theoretically race-free. `gpu_frame_sync=1` turns it back
 * on for testing. The proper fix is double-buffered geometry, which needs the
 * presentation measurement first; see docs/old3ds-pica200-parity-notes.md. */
static bool sGpuFrameSync = false;
/* Where the rendered frame sits in the 512x256 target. The viewport and the
 * scissor are offset so scanline 0 lands in target row 0, which is where the
 * presenter samples -- but hardware parity fails from exactly row 96 down
 * (256 - 160, the offset itself), while an emulator passes, so the two
 * disagree about this axis. `gpu_viewport_offset=0` draws at the bottom of
 * the target instead, which is the other reading of the same convention. */
static bool sGpuViewportOffset = true;
/* How a batch's scanline range becomes a scissor rectangle.
 *   0 = none      : no vertical clipping at all (tile rows overhang)
 *   1 = flipped   : y measured from the bottom of the target (default)
 *   2 = direct    : y measured from the top
 * Hardware draws only the first band with the default while an emulator draws
 * both, so the two disagree about this axis; the switch settles it. */
static int sGpuScissorMode = 1;
/* Window/blend masking uses the stencil buffer. Turning it off draws every
 * batch unconditionally: windows and blending come out wrong, but if the
 * missing band appears then the stencil is what was rejecting it. */
static bool sGpuStencil = true;
/* -1 keeps the built-in choice; 0 or 1 pins the audio worker for an A/B. */
static int sAudioCore = -1;
/* -1 keeps core 0; 1 moves the bottom-screen painter off the main thread's core. */
static int sBottomCore = -1;
/* Skip a MAP-tab bottom-screen repaint when the quantised animation signature
 * says the next tick draws the same picture. Defaults on: a dump measured 2230
 * paints at 11.679 ms with 0 skips, ~1.95 ms per presented frame against a
 * 1.55 ms/frame deficit. Turn it off to A/B, or if the bottom screen ever
 * freezes -- that is the failure mode if the signature misses an input. */
static bool sBottomMapSkip = true;
/* Phase-lock the presentation wait to the next VBlank instead of accepting an
 * already-pending one. Off by default: unproven, and the failure mode is a
 * heavy scene pinned to 30 FPS. See Platform3DS_WaitForVBlank. */
static bool sVblankPhaseLock = false;
/* Interpolation for the offloaded NDSP voices.
 *
 * The software mix leaves channel 0 at 16364 Hz and NDSP upsamples it to its
 * 32728 Hz output with NDSP_INTERP_LINEAR -- exactly 2x, so linear inserts the
 * arithmetic midpoint and gently low-passes the GBA's aliasing. Before the DSP
 * offload every voice went through that one channel, so the whole mix shared
 * that character. The offload gave its own channels NDSP_INTERP_NONE, which
 * keeps the aliasing, so the mix has run two different resampling characters
 * at once ever since -- offloaded voices brighter than the software ones.
 *
 * Default matches channel 0, which is the pre-offload sound. Set to 0 for
 * NDSP_INTERP_NONE, which is closer to MP2K's NEAREST pitching in isolation
 * but inconsistent with the rest of the mix. */
static bool sAudioDspInterpLinear = true;
/* Speaker shaping. Off by default: it deliberately departs from GBA output, so
 * it is the player's call, not the port's. The cutoff is a knob because the
 * right value depends on the actual driver and on how NDSP's biquad interprets
 * f0 relative to its output rate -- tune it by ear, not from a datasheet. */
static bool sSpeakerEq = false;
static float sSpeakerEqHz = 280.0f;
/* Frame-path diagnostics. Off by default: Platform3DS_Debug writes each line
 * to the SD card with fopen/fwrite/fclose on the main thread inside the
 * presentation span. The quick dump reports the same counters without adding
 * intermittent gameplay stalls. */
static bool sFrameLog = false;
/* Compact Old 3DS upload surfaces (272x160 top, 320x240 bottom RGBA8 instead of
 * 512x256). Cuts the bottom clean-and-transfer from 491520 to 307200 bytes, and
 * that transfer is synchronous, so it shortens a recurring main-thread block on
 * GSP rather than just saving memory. Off by default: the painter stride and the
 * display-transfer dimensions must agree, and if they do not the bottom screen
 * is visibly garbage. Flip it, look at the screen, then keep or drop it. */
static bool sCompactUpload = false;
/* Cap on the app's core-1 share. 0 keeps the built-in preference order (80 first).
 * Lower values hand core 1 back to the sysmodules -- GSP above all, which retires
 * the queue C3D_FrameBegin waits on. See Platform3DS_Init. */
static int sAppCpuLimit = 0;
/* Report and research items that measurement says are neutral or harmful by
 * default. Present, switchable, and off unless asked for. */
static bool sGpuStaticQuad = false;
static bool sBottomRgb565 = false;
static bool sGpuShortVertices = false;
/* Stored audio choices. The versioned migration below enables both for the
 * Old 3DS profile; the effective accessors keep New 3DS on the reference mix. */
static bool sAudioDsp = false;
static bool sAudioDspPcm = false;
/* E8 shipped the Old 3DS performance work with both audio offloads stored as
 * disabled.  The hardware run that validated the PR used both of them and the
 * new dumps show why they are part of the Old 3DS profile rather than merely
 * research switches: fanfares miss the 15.6 ms audio-block deadline by as much
 * as 64 ms.  Version the private performance profile so an existing E8 ini is
 * migrated once, while a deliberate later audio_dsp=0 A/B remains possible. */
static int sPerformanceProfileVersion;

enum { PERFORMANCE_PROFILE_VERSION = 1 };

static bool ParseBool(const char* value) {
    return value != NULL && (value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
                             value[0] == 'y' || value[0] == 'Y');
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
    fprintf(file, "performance_profile_version=%d\n", PERFORMANCE_PROFILE_VERSION);
    fprintf(file, "show_fps=%u\n", sShowFps ? 1u : 0u);
    fprintf(file, "follow_cam=%u\n", sFollow ? 1u : 0u);
    fprintf(file, "windcrest_pins=%u\n", sCrests ? 1u : 0u);
    fprintf(file, "floor_auto_return=%u\n", sFloorReturn ? 1u : 0u);
    fprintf(file, "hide_top_hud=%u\n", sHideHud ? 1u : 0u);
    fprintf(file, "hold_to_advance_text=%u\n", sHoldText ? 1u : 0u);
    fprintf(file, "color_correction=%u\n", sColorCorrection ? 1u : 0u);
    fprintf(file, "autosave=%u\n", sAutosave ? 1u : 0u);
    fprintf(file, "widescreen=%u\n", sAspectRatio == PORT_3DS_ASPECT_WIDE ? 1u : 0u);
    fprintf(file, "screen_aspect=%s\n", ConfigValues3DS_AspectName(sAspectRatio));
    fprintf(file, "display_style=%s\n", ConfigValues3DS_DisplayName(sDisplayStyle));
    fprintf(file, "master_volume=%.2f\n", (double)sVolume);
    fprintf(file, "panel_backdrop=%d\n", sBackdrop);
    fprintf(file, "turbo_multiplier=%u\n", sTurboMultiplier);
    fprintf(file, "randomizer=%u\n", sRandoEnabled ? 1u : 0u);
    fprintf(file, "rando_glitchless=%u\n", sRandoGlitchless ? 1u : 0u);
    fprintf(file, "rando_obscure=%u\n", sRandoObscure ? 1u : 0u);
    fprintf(file, "rando_kinstones=%u\n", sRandoKinstones ? 1u : 0u);
    fprintf(file, "rando_entrances=%u\n", sRandoEntrances ? 1u : 0u);
    fprintf(file, "rando_dojos=%u\n", sRandoDojos ? 1u : 0u);
    fprintf(file, "rando_open_world=%u\n", sRandoOpenWorld ? 1u : 0u);
    fprintf(file, "rando_homewarp=%u\n", sRandoHomewarp ? 1u : 0u);
    fprintf(file, "rando_start_sword=%u\n", sRandoStartSword ? 1u : 0u);
    fprintf(file, "rando_early_crests=%u\n", sRandoEarlyCrests ? 1u : 0u);
    fprintf(file, "rando_instant_text=%u\n", sRandoInstantText ? 1u : 0u);
    fprintf(file, "rando_dungeon_items=%u\n", sRandoDungeonItems ? 1u : 0u);
    fprintf(file, "rando_item_pool=%d\n", sRandoItemPool);
    fprintf(file, "rando_tunic_color=%d\n", sRandoTunicColor);
    fprintf(file, "rando_heart_color=%d\n", sRandoHeartColor);
    fprintf(file, "rando_tricks=%d\n", sRandoTricks);
    fprintf(file, "rando_accessibility=%d\n", sRandoAccessibility);

    /* Round-trip the performance/research switches. SaveConfig parses these
     * but used to omit them, so any Settings change -- or any exit that saves
     * -- silently erased every hand-set flag from tmc3ds.ini and the next run
     * quietly reverted to compiled defaults. That destroyed the config of the
     * very run being measured; `dump-20260820-205902` was diagnosed only by
     * reading the offload counters back out of info.txt. Whatever is parsed
     * must be written. */
    fprintf(file, "gpu_renderer=%u\n", sGpuRenderer ? 1u : 0u);
    fprintf(file, "gpu_frame_sync=%u\n", sGpuFrameSync ? 1u : 0u);
    fprintf(file, "gpu_viewport_offset=%u\n", sGpuViewportOffset ? 1u : 0u);
    fprintf(file, "gpu_scissor_mode=%d\n", sGpuScissorMode);
    fprintf(file, "gpu_stencil=%u\n", sGpuStencil ? 1u : 0u);
    fprintf(file, "audio_core=%d\n", sAudioCore);
    fprintf(file, "bottom_core=%d\n", sBottomCore);
    fprintf(file, "bottom_map_skip=%u\n", sBottomMapSkip ? 1u : 0u);
    fprintf(file, "vblank_phase_lock=%u\n", sVblankPhaseLock ? 1u : 0u);
    fprintf(file, "audio_dsp_interp_linear=%u\n", sAudioDspInterpLinear ? 1u : 0u);
    fprintf(file, "speaker_eq=%u\n", sSpeakerEq ? 1u : 0u);
    fprintf(file, "speaker_eq_hz=%.1f\n", (double)sSpeakerEqHz);
    fprintf(file, "frame_log=%u\n", sFrameLog ? 1u : 0u);
    fprintf(file, "compact_upload=%u\n", sCompactUpload ? 1u : 0u);
    fprintf(file, "app_cpu_limit=%d\n", sAppCpuLimit);
    fprintf(file, "gpu_static_quad=%u\n", sGpuStaticQuad ? 1u : 0u);
    fprintf(file, "bottom_rgb565=%u\n", sBottomRgb565 ? 1u : 0u);
    fprintf(file, "gpu_short_vertices=%u\n", sGpuShortVertices ? 1u : 0u);
    fprintf(file, "audio_dsp=%u\n", sAudioDsp ? 1u : 0u);
    fprintf(file, "audio_dsp_pcm=%u\n", sAudioDspPcm ? 1u : 0u);

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
    bool migratePerformanceProfile = false;
    FILE* file = fopen(sConfigPath, "rb");
    if (file) {
        char line[160];
        while (fgets(line, sizeof(line), file) != NULL) {
            char key[64];
            char value[64];
            if (line[0] == '#' || sscanf(line, " %63[^=]=%63s", key, value) != 2) continue;
            if (strcmp(key, "performance_profile_version") == 0)
                sPerformanceProfileVersion = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "show_fps") == 0) sShowFps = ParseBool(value);
            else if (strcmp(key, "gpu_renderer") == 0) sGpuRenderer = ParseBool(value);
            else if (strcmp(key, "gpu_frame_sync") == 0) sGpuFrameSync = ParseBool(value);
            else if (strcmp(key, "gpu_viewport_offset") == 0) sGpuViewportOffset = ParseBool(value);
            else if (strcmp(key, "gpu_scissor_mode") == 0) sGpuScissorMode = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "gpu_stencil") == 0) sGpuStencil = ParseBool(value);
            else if (strcmp(key, "audio_core") == 0) sAudioCore = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "bottom_core") == 0) sBottomCore = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "bottom_map_skip") == 0) sBottomMapSkip = ParseBool(value);
            else if (strcmp(key, "vblank_phase_lock") == 0) sVblankPhaseLock = ParseBool(value);
            else if (strcmp(key, "audio_dsp_interp_linear") == 0)
                sAudioDspInterpLinear = ParseBool(value);
            else if (strcmp(key, "speaker_eq") == 0) sSpeakerEq = ParseBool(value);
            else if (strcmp(key, "speaker_eq_hz") == 0) sSpeakerEqHz = strtof(value, NULL);
            else if (strcmp(key, "frame_log") == 0) sFrameLog = ParseBool(value);
            else if (strcmp(key, "compact_upload") == 0) sCompactUpload = ParseBool(value);
            else if (strcmp(key, "app_cpu_limit") == 0) sAppCpuLimit = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "gpu_static_quad") == 0) sGpuStaticQuad = ParseBool(value);
            else if (strcmp(key, "bottom_rgb565") == 0) sBottomRgb565 = ParseBool(value);
            else if (strcmp(key, "gpu_short_vertices") == 0) sGpuShortVertices = ParseBool(value);
            else if (strcmp(key, "audio_dsp") == 0) sAudioDsp = ParseBool(value);
            else if (strcmp(key, "audio_dsp_pcm") == 0) sAudioDspPcm = ParseBool(value);
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
                sAspectRatio = ConfigValues3DS_ParseAspect(value);
                hasScreenAspect = true;
            } else if (strcmp(key, "display_style") == 0) {
                sDisplayStyle = ConfigValues3DS_ParseDisplay(value);
            }
            else if (strcmp(key, "master_volume") == 0) sVolume = strtof(value, NULL);
            else if (strcmp(key, "panel_backdrop") == 0) sBackdrop = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "turbo_multiplier") == 0) sTurboMultiplier = (unsigned)strtoul(value, NULL, 10);
            else if (strcmp(key, "randomizer") == 0) sRandoEnabled = ParseBool(value);
            else if (strcmp(key, "rando_glitchless") == 0) sRandoGlitchless = ParseBool(value);
            else if (strcmp(key, "rando_obscure") == 0) sRandoObscure = ParseBool(value);
            else if (strcmp(key, "rando_kinstones") == 0) sRandoKinstones = ParseBool(value);
            else if (strcmp(key, "rando_entrances") == 0) sRandoEntrances = ParseBool(value);
            else if (strcmp(key, "rando_dojos") == 0) sRandoDojos = ParseBool(value);
            else if (strcmp(key, "rando_open_world") == 0) sRandoOpenWorld = ParseBool(value);
            else if (strcmp(key, "rando_homewarp") == 0) sRandoHomewarp = ParseBool(value);
            else if (strcmp(key, "rando_start_sword") == 0) sRandoStartSword = ParseBool(value);
            else if (strcmp(key, "rando_early_crests") == 0) sRandoEarlyCrests = ParseBool(value);
            else if (strcmp(key, "rando_instant_text") == 0) sRandoInstantText = ParseBool(value);
            else if (strcmp(key, "rando_dungeon_items") == 0) sRandoDungeonItems = ParseBool(value);
            else if (strcmp(key, "rando_item_pool") == 0) sRandoItemPool = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "rando_tunic_color") == 0) sRandoTunicColor = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "rando_heart_color") == 0) sRandoHeartColor = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "rando_tricks") == 0) sRandoTricks = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "rando_accessibility") == 0) sRandoAccessibility = (int)strtol(value, NULL, 10);
        }
        fclose(file);
    }

    /* Keep New 3DS on the established software-mix reference.  Old 3DS needs
     * the PR's proven DSP profile, including when an E8 settings save already
     * wrote the old research defaults as explicit zeroes. */
    if (!Platform3DS_IsNew3DS() &&
        sPerformanceProfileVersion < PERFORMANCE_PROFILE_VERSION) {
        sAudioDsp = true;
        sAudioDspPcm = true;
        sPerformanceProfileVersion = PERFORMANCE_PROFILE_VERSION;
        migratePerformanceProfile = true;
    }

    if (sVolume < 0.0f) sVolume = 0.0f;
    if (sVolume > 1.0f) sVolume = 1.0f;
    if (sBackdrop < 0 || sBackdrop > 6) sBackdrop = 0;
    if (sTurboMultiplier < 2 || sTurboMultiplier > 5) sTurboMultiplier = 5;
    if (sRandoItemPool < 0 || sRandoItemPool >= RANDO_ITEM_POOL_COUNT) sRandoItemPool = RANDO_ITEM_POOL_NORMAL;
    if (sRandoTunicColor < 0 || sRandoTunicColor > 6) sRandoTunicColor = 0;
    if (sRandoHeartColor < 0 || sRandoHeartColor > 6) sRandoHeartColor = 0;
    sRandoTricks &= RANDO_TRICK_ALL;
    if (sRandoAccessibility < 0 || sRandoAccessibility >= RANDO_ACCESS_COUNT)
        sRandoAccessibility = RANDO_ACCESS_GOAL;
    Platform3DS_SetTurboMultiplier(sTurboMultiplier);
    sConfigLoaded = true;
    if (migratePerformanceProfile) SaveConfig();
}
void Port_Config_SetActiveSaveProfile(const char* path) { (void)path; }
bool Port_Config_GpuRenderer(void) { return sGpuRenderer; }
bool Port_Config_GpuFrameSync(void) { return sGpuFrameSync; }
bool Port_Config_GpuViewportOffset(void) { return sGpuViewportOffset; }
int Port_Config_GpuScissorMode(void) { return sGpuScissorMode; }
bool Port_Config_GpuStencil(void) { return sGpuStencil; }
int Port_Config_AudioCore(void) { return sAudioCore; }
int Port_Config_BottomCore(void) { return sBottomCore; }
bool Port_Config_BottomMapSkip(void) { return sBottomMapSkip; }
bool Port_Config_VblankPhaseLock(void) { return sVblankPhaseLock; }
bool Port_Config_AudioDspInterpLinear(void) { return sAudioDspInterpLinear; }
bool Port_Config_SpeakerEq(void) { return sSpeakerEq; }
float Port_Config_SpeakerEqHz(void) { return sSpeakerEqHz; }
bool Port_Config_FrameLog(void) { return sFrameLog; }
bool Port_Config_CompactUpload(void) { return sCompactUpload; }
int Port_Config_AppCpuLimit(void) { return sAppCpuLimit; }
bool Port_Config_GpuStaticQuad(void) { return sGpuStaticQuad; }
bool Port_Config_BottomRgb565(void) { return sBottomRgb565; }
bool Port_Config_GpuShortVertices(void) { return sGpuShortVertices; }
bool Port_Config_AudioDsp(void) {
    return !Platform3DS_IsNew3DS() && sAudioDsp;
}
bool Port_Config_AudioDspPcm(void) {
    return !Platform3DS_IsNew3DS() && sAudioDspPcm;
}
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
bool Port_Config_GetRandoEnabled(void) { return sRandoEnabled; }
void Port_Config_SetRandoEnabled(bool on) { sRandoEnabled = on; SaveConfig(); }
bool Port_Config_GetRandoGlitchless(void) { return sRandoGlitchless; }
bool Port_Config_GetRandoObscure(void) { return sRandoObscure; }
bool Port_Config_GetRandoKinstones(void) { return sRandoKinstones; }
bool Port_Config_GetRandoEntrances(void) { return sRandoEntrances; }
bool Port_Config_GetRandoDojos(void) { return sRandoDojos; }
bool Port_Config_GetRandoOpenWorld(void) { return sRandoOpenWorld; }
bool Port_Config_GetRandoHomewarp(void) { return sRandoHomewarp; }
bool Port_Config_GetRandoStartSword(void) { return sRandoStartSword; }
bool Port_Config_GetRandoEarlyCrests(void) { return sRandoEarlyCrests; }
bool Port_Config_GetRandoInstantText(void) { return sRandoInstantText; }
bool Port_Config_GetRandoDungeonItems(void) { return sRandoDungeonItems; }
void Port_Config_SetRandoDungeonItems(bool on) { sRandoDungeonItems = on; SaveConfig(); }
int Port_Config_GetRandoItemPool(void) { return sRandoItemPool; }
int Port_Config_GetRandoTunicColor(void) { return sRandoTunicColor; }
int Port_Config_GetRandoHeartColor(void) { return sRandoHeartColor; }
void Port_Config_SetRandoSettings(bool glitchless, bool obscure, bool kinstones, bool entrances, bool dojos,
                                  bool openWorld, int itemPool, bool homewarp, bool startSword,
                                  bool earlyCrests, bool instantText, int tunicColor, int heartColor) {
    sRandoGlitchless = glitchless;
    sRandoObscure = obscure;
    sRandoKinstones = kinstones;
    sRandoEntrances = entrances;
    sRandoDojos = dojos;
    sRandoOpenWorld = openWorld;
    sRandoItemPool = itemPool;
    sRandoHomewarp = homewarp;
    sRandoStartSword = startSword;
    sRandoEarlyCrests = earlyCrests;
    sRandoInstantText = instantText;
    sRandoTunicColor = tunicColor;
    sRandoHeartColor = heartColor;
    SaveConfig();
}
int Port_Config_GetRandoTricks(void) { return sRandoTricks; }
void Port_Config_SetRandoTricks(int tricks) { sRandoTricks = tricks; SaveConfig(); }
int Port_Config_GetRandoAccessibility(void) { return sRandoAccessibility; }
void Port_Config_SetRandoAccessibility(int accessibility) { sRandoAccessibility = accessibility; SaveConfig(); }
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
    return ConfigValues3DS_DisplayLabel(sDisplayStyle);
}
void Port_Config_Cycle3DSDisplayStyle(void) {
    sDisplayStyle = (Port3DSDisplayStyle)((sDisplayStyle + 1) % PORT_3DS_DISPLAY_COUNT);
    SaveConfig();
}
bool Port_Config_3DSFullViewComboEnabled(void) {
    /* Load each atomic once so a bottom-screen option tap can only select one
     * complete policy on this call. A mixed/transient value fails closed on
     * the following scene latch before any 400x240 geometry is published. */
    const Port3DSAspectRatio aspect = sAspectRatio;
    const Port3DSDisplayStyle style = sDisplayStyle;
    return Platform3DS_IsNew3DS() && aspect == PORT_3DS_ASPECT_WIDE &&
           style == PORT_3DS_DISPLAY_PIXEL_PERFECT;
}
