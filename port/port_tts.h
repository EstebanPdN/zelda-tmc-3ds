/*
 * port_tts.h — Text-to-speech accessibility service for Project Picori.
 *
 * Reads UI text aloud for visually impaired users. Calls hand off to a
 * platform-native TTS backend (speech-dispatcher / espeak / say / SAPI)
 * via subprocess; the backend is wrapped behind a thin abstraction so
 * an in-process synthesizer can be slotted in later without touching
 * call sites.
 *
 *   Per-frame UI flow:
 *     Port_TTS_OnFocusChanged(label)  — announce widget under focus
 *     Port_TTS_AnnounceMessage(text)  — toasts, status changes
 *     Port_TTS_AnnounceError(text)    — high-priority, jumps queue
 *
 *   Direct API (synchronous-looking, actually async):
 *     Port_TTS_Speak(text, opts)      — enqueue
 *     Port_TTS_Stop()                 — drop the queue, cut the current
 *     Port_TTS_Pause() / _Resume()    — pause / resume current utterance
 *     Port_TTS_IsSpeaking()           — true between Speak start and
 *                                       backend completion
 *
 *   Settings (persisted via port_runtime_config):
 *     enabled       — master gate, default OFF (privacy + voice
 *                     synthesis isn't everyone's preference)
 *     rate, pitch, volume — 0.0..1.0 each
 *     voice         — backend-specific voice id (NULL = system default)
 *     language      — BCP-47 lang tag, NULL = system default
 *
 * Threading: every public function is callable from the main UI
 * thread. The backend runs in a worker thread; nothing blocks the
 * frame loop. Calling Port_TTS_Stop() during shutdown is safe and
 * idempotent.
 */
#ifndef PORT_TTS_H
#define PORT_TTS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Urgency hints — `Normal` queues, `Urgent` interrupts whatever's
 * speaking and jumps to the front. Keep small so the enum survives a
 * round-trip through config without translation. */
typedef enum {
    PORT_TTS_PRIO_NORMAL = 0,
    PORT_TTS_PRIO_URGENT = 1,
} PortTtsPriority;

/* Per-utterance overrides. NULL or NaN fields fall back to the
 * persisted setting. */
typedef struct {
    PortTtsPriority priority;
    float           rate;        /* 0.0..1.0, NaN = use setting */
    float           pitch;       /* 0.0..1.0, NaN = use setting */
    float           volume;      /* 0.0..1.0, NaN = use setting */
    const char*     voice;       /* nullable, backend-specific */
    const char*     language;    /* nullable, BCP-47 */
    /* If true the call returns early when this exact text was spoken
     * within the last few seconds — suppresses chatty focus-tracking
     * announcements. Default true; pass `Speak` directly with false
     * for messages that ARE meant to repeat (countdowns, etc). */
    bool            dedupe;
} PortTtsOptions;

/* Lifecycle. Init reads persisted config and spins up the worker.
 * Idempotent. Returns true on success, false if no backend is
 * available (in which case the public API is a no-op). */
bool Port_TTS_Init(void);
void Port_TTS_Shutdown(void);

/* Core. */
void Port_TTS_Speak(const char* text, const PortTtsOptions* opts);
void Port_TTS_Stop(void);
void Port_TTS_Pause(void);
void Port_TTS_Resume(void);
bool Port_TTS_IsSpeaking(void);

/* High-level helpers — the common case is "say this short label."
 * They build the options struct for you and route to the right
 * priority + dedupe defaults. */
void Port_TTS_OnFocusChanged(const char* label);  /* dedupe=true, normal */
void Port_TTS_AnnounceMessage(const char* text);  /* dedupe=true, normal */
void Port_TTS_AnnounceError(const char* text);    /* dedupe=false, urgent */

/* Settings — getters and setters. Setters persist to config.json on
 * the next save tick (mirrors how other Port_Config setters work). */
bool        Port_TTS_GetEnabled(void);
void        Port_TTS_SetEnabled(bool on);
float       Port_TTS_GetRate(void);
void        Port_TTS_SetRate(float v);
float       Port_TTS_GetPitch(void);
void        Port_TTS_SetPitch(float v);
float       Port_TTS_GetVolume(void);
void        Port_TTS_SetVolume(float v);
const char* Port_TTS_GetVoice(void);
void        Port_TTS_SetVoice(const char* v);
const char* Port_TTS_GetLanguage(void);
void        Port_TTS_SetLanguage(const char* v);

/* Backend introspection. Returns "spd-say", "espeak-ng", "espeak",
 * "say", "sapi", or NULL if no backend is usable. Lets the F8 menu
 * tell the user what's wired up. */
const char* Port_TTS_GetBackendName(void);

/* Walk a TMC dialog text index through the same decoder the renderer
 * uses and speak the result. Used for UI focus text (pause menu item
 * names, descriptions, popup labels) that's drawn via ShowTextBox
 * directly — bypassing the MessageMain dialog pipeline that already
 * has its own per-page TTS hook. URGENT + dedupe under the hood.
 * Implementation lives in src/message.c (needs Token / GetCharacter). */
void Port_TTS_SpeakTextIndex(unsigned int textIndex);

#ifdef __cplusplus
}
#endif

#endif /* PORT_TTS_H */
