/*
 * port_tts.cpp — Project Picori TTS service implementation.
 *
 * Subprocess-per-utterance design. Each Speak() pushes a job onto a
 * worker queue; the worker picks the next job, spawns the platform
 * backend with the text on stdin (or as an arg), waits for it, and
 * loops. Urgent jobs kill any in-flight subprocess and move to the
 * front of the queue. Drop-deduplicate suppresses identical text
 * within a short window so chatty focus-tracking doesn't spam.
 *
 *   Linux: prefer `spd-say` (speech-dispatcher) for queuing/voice
 *          selection; fall back to `espeak-ng` / `espeak`.
 *   macOS: `say` (always present on the OS).
 *   Windows: PowerShell + System.Speech.Synthesis.SpeechSynthesizer.
 *
 * If no backend is usable Port_TTS_Init returns false and every
 * subsequent call is a no-op. The F8 menu shows the resolved
 * backend name (or "(unavailable)") so the user sees what's wired.
 */

#include "port_tts.h"
#include "port_runtime_config.h"

#ifdef __ANDROID__
/* No subprocess TTS backends exist on Android (spd-say/say/SAPI are desktop).
 * Native TTS via android.speech.tts + JNI is a planned later milestone; until
 * then the whole a11y-speech surface is a well-behaved no-op so the rest of
 * the port (menus, config plumbing) links and runs unchanged. */
extern "C" {
bool Port_TTS_Init(void) {
    return false;
}
void Port_TTS_Shutdown(void) {
}
void Port_TTS_Speak(const char*, const PortTtsOptions*) {
}
void Port_TTS_Stop(void) {
}
void Port_TTS_Pause(void) {
}
void Port_TTS_Resume(void) {
}
bool Port_TTS_IsSpeaking(void) {
    return false;
}
void Port_TTS_OnFocusChanged(const char*) {
}
void Port_TTS_AnnounceMessage(const char*) {
}
void Port_TTS_AnnounceError(const char*) {
}
bool Port_TTS_GetEnabled(void) {
    return false;
}
void Port_TTS_SetEnabled(bool) {
}
float Port_TTS_GetRate(void) {
    return 0.5f;
}
void Port_TTS_SetRate(float) {
}
float Port_TTS_GetPitch(void) {
    return 0.5f;
}
void Port_TTS_SetPitch(float) {
}
float Port_TTS_GetVolume(void) {
    return 1.0f;
}
void Port_TTS_SetVolume(float) {
}
const char* Port_TTS_GetVoice(void) {
    return "";
}
void Port_TTS_SetVoice(const char*) {
}
const char* Port_TTS_GetLanguage(void) {
    return "";
}
void Port_TTS_SetLanguage(const char*) {
}
const char* Port_TTS_GetBackendName(void) {
    return "(unavailable on Android)";
}
/* Port_TTS_SpeakTextIndex is implemented in src/message.c (needs Token /
 * GetCharacter) and internally calls Port_TTS_Speak — no stub here. */
}
#else /* !__ANDROID__ */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

/* ------------------------------------------------------------------ */
/*  Backend abstraction                                               */
/* ------------------------------------------------------------------ */

namespace {

enum class Backend {
    None,
    SpdSay,   /* Linux: speech-dispatcher's spd-say */
    EspeakNg, /* Linux: espeak-ng (more verbose, no queue) */
    Espeak,   /* Linux: original espeak */
    Say,      /* macOS: AppKit-backed CLI */
    Sapi,     /* Windows: PowerShell + System.Speech.Synthesis */
    Nvda,     /* Windows: nvdaControllerClient — preferred when NVDA
               *          is the user's screen reader. NVDA owns the
               *          rate/pitch/voice settings (configured inside
               *          NVDA itself), so our sliders are ignored on
               *          this path. */
};

const char* BackendName(Backend b) {
    switch (b) {
        case Backend::SpdSay:
            return "spd-say";
        case Backend::EspeakNg:
            return "espeak-ng";
        case Backend::Espeak:
            return "espeak";
        case Backend::Say:
            return "say";
        case Backend::Sapi:
            return "sapi";
        case Backend::Nvda:
            return "NVDA";
        default:
            return nullptr;
    }
}

#ifdef _WIN32
/* ============================================================
 * NVDA Controller Client integration.
 *
 * NVDA (NonVisual Desktop Access) is the de-facto open-source
 * screen reader on Windows. Apps that want to be screen-reader-
 * friendly call into nvdaControllerClient.dll instead of doing
 * their own synthesis — NVDA then queues the speech through the
 * user's configured voice / rate / pitch (which the user has
 * already tuned in NVDA's own settings).
 *
 * We load the DLL lazily so this binary still runs on Windows
 * boxes that don't have NVDA installed (we just fall back to
 * SAPI). Our 64-bit binary needs nvdaControllerClient64.dll;
 * we also try the plain name in case the user dropped a copy
 * next to tmc_pc.exe themselves.
 * ============================================================ */
typedef int(__cdecl* PFN_nvdaController_testIfRunning)(void);
typedef int(__cdecl* PFN_nvdaController_speakText)(const wchar_t*);
typedef int(__cdecl* PFN_nvdaController_cancelSpeech)(void);

HMODULE g_nvda_dll = nullptr;
PFN_nvdaController_testIfRunning g_nvda_testIfRunning = nullptr;
PFN_nvdaController_speakText g_nvda_speakText = nullptr;
PFN_nvdaController_cancelSpeech g_nvda_cancelSpeech = nullptr;

bool TryLoadNvda() {
    if (g_nvda_dll)
        return g_nvda_speakText != nullptr;
    /* DLL search order matters — prefer the 64-bit name (the PC
     * port is 64-bit only), then the unversioned name (NVDA's
     * installer drops it in NVDA's program-files dir but a
     * developer might place a copy next to the binary). */
    g_nvda_dll = LoadLibraryA("nvdaControllerClient64.dll");
    if (!g_nvda_dll)
        g_nvda_dll = LoadLibraryA("nvdaControllerClient.dll");
    if (!g_nvda_dll)
        return false;
    g_nvda_testIfRunning = (PFN_nvdaController_testIfRunning)GetProcAddress(g_nvda_dll, "nvdaController_testIfRunning");
    g_nvda_speakText = (PFN_nvdaController_speakText)GetProcAddress(g_nvda_dll, "nvdaController_speakText");
    g_nvda_cancelSpeech = (PFN_nvdaController_cancelSpeech)GetProcAddress(g_nvda_dll, "nvdaController_cancelSpeech");
    if (!g_nvda_testIfRunning || !g_nvda_speakText || !g_nvda_cancelSpeech) {
        FreeLibrary(g_nvda_dll);
        g_nvda_dll = nullptr;
        return false;
    }
    return true;
}

bool NvdaIsRunning() {
    /* nvdaController_testIfRunning returns 0 (S_OK) when NVDA is
     * running, non-zero otherwise — opposite of the conventional
     * "boolean" sense, but matches the NVDA Controller API docs. */
    return TryLoadNvda() && g_nvda_testIfRunning() == 0;
}

bool CommandExists(const char*) {
    /* Windows backend is always available — SAPI ships in every
     * supported Windows version via PowerShell + System.Speech.
     * NVDA path detection happens separately in NvdaIsRunning(). */
    return false;
}
#else
bool CommandExists(const char* name) {
    /* `which` returns 0 on found. system() is fine here — we only
     * call this at startup. */
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}
#endif

Backend DetectBackend() {
#ifdef _WIN32
    /* Prefer NVDA when its DLL is present + the user has NVDA
     * running. Each Speak call re-tests at runtime via
     * NvdaIsRunning() so we transparently fall back to SAPI if
     * NVDA exits mid-session (and vice-versa). The "preferred"
     * backend recorded here is the one the F8 UI labels; the
     * runtime dispatch happens in SpeakOne. */
    if (TryLoadNvda())
        return Backend::Nvda;
    return Backend::Sapi;
#elif defined(__APPLE__)
    return Backend::Say; /* present on every macOS install */
#else
    if (CommandExists("spd-say"))
        return Backend::SpdSay;
    if (CommandExists("espeak-ng"))
        return Backend::EspeakNg;
    if (CommandExists("espeak"))
        return Backend::Espeak;
    return Backend::None;
#endif
}

/* ------------------------------------------------------------------ */
/*  Worker state                                                      */
/* ------------------------------------------------------------------ */

struct Utterance {
    std::string text;
    PortTtsOptions opts;
};

struct State {
    Backend backend = Backend::None;
    std::atomic<bool> initialized{ false };
    std::atomic<bool> quitting{ false };
    std::atomic<bool> paused{ false };
    std::atomic<bool> in_flight{ false };

    /* Settings — cached copies of the persisted config so we don't
     * call into the config service on every Speak(). Refreshed by
     * setters. */
    std::atomic<bool> enabled{ false };
    std::atomic<float> rate{ 0.5f };
    std::atomic<float> pitch{ 0.5f };
    std::atomic<float> volume{ 0.8f };
    std::string voice;
    std::string language;
    std::mutex settings_mu; /* protects voice / language */

    /* Job queue + worker. */
    std::deque<Utterance> queue;
    std::mutex queue_mu;
    std::condition_variable queue_cv;
    std::thread worker;

    /* Dedupe: remember the last few texts spoken so a chatty focus
     * tracker doesn't repeat itself. Tiny ring buffer (8 entries)
     * keyed by string + timestamp. */
    struct DedupeEntry {
        std::string text;
        std::chrono::steady_clock::time_point at;
    };
    static constexpr size_t kDedupeWindow = 8;
    static constexpr int kDedupeMillis = 1500;
    std::deque<DedupeEntry> recent;
    std::mutex recent_mu;

    /* PID of the in-flight backend subprocess (so urgent jobs can
     * kill it). 0 = none. POSIX-only field; Windows uses a HANDLE. */
#ifndef _WIN32
    std::atomic<pid_t> current_pid{ 0 };
#else
    std::atomic<HANDLE> current_proc{ nullptr };
#endif
};

State g_state;

bool IsDuplicate(const std::string& text) {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();
    const auto cutoff = now - std::chrono::milliseconds(State::kDedupeMillis);
    std::lock_guard<std::mutex> lk(g_state.recent_mu);
    /* Prune older entries; small enough that linear is fine. */
    while (!g_state.recent.empty() && g_state.recent.front().at < cutoff) {
        g_state.recent.pop_front();
    }
    for (auto& e : g_state.recent) {
        if (e.text == text)
            return true;
    }
    g_state.recent.push_back({ text, now });
    if (g_state.recent.size() > State::kDedupeWindow)
        g_state.recent.pop_front();
    return false;
}

/* ------------------------------------------------------------------ */
/*  Per-backend Speak                                                 */
/* ------------------------------------------------------------------ */

#ifndef _WIN32
/* Run argv as a subprocess, recording the PID in g_state.current_pid.
 * Blocks until exit. Used by the worker thread only. */
int RunSubprocess(const char* const argv[]) {
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) {
        return -1;
    }
    g_state.current_pid.store(pid);
    /* If shutdown began between the worker's dequeue and this spawn,
     * Port_TTS_Stop()'s KillCurrent() may have run while current_pid was
     * still 0 (this child hadn't been spawned yet), leaving spd-say
     * unkilled — waitpid() below would then block forever and deadlock
     * Port_TTS_Shutdown()'s worker.join() (frozen quit). Re-check the
     * flag now that the pid is recorded and kill if we're shutting down. */
    if (g_state.quitting.load()) {
        kill(pid, SIGTERM);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    g_state.current_pid.store(0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void KillCurrent() {
    pid_t pid = g_state.current_pid.exchange(0);
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
}

void SpeakSpdSay(const std::string& text, const PortTtsOptions& opts) {
    /* spd-say flags:  -r/--rate (-100..100)  -p/--pitch (-100..100)
     *                 -i/--volume (-100..100)  -t/--voice-type
     *                 -l/--language */
    char rate_str[16], pitch_str[16], vol_str[16];
    auto map = [](float v) { return (int)((v - 0.5f) * 200.0f); }; /* 0..1 → -100..100 */
    std::snprintf(rate_str, sizeof(rate_str), "%d", map(opts.rate));
    std::snprintf(pitch_str, sizeof(pitch_str), "%d", map(opts.pitch));
    std::snprintf(vol_str, sizeof(vol_str), "%d", map(opts.volume));
    std::vector<const char*> argv = {
        "spd-say", "--wait", "-r", rate_str, "-p", pitch_str, "-i", vol_str,
    };
    if (opts.language && *opts.language) {
        argv.push_back("-l");
        argv.push_back(opts.language);
    }
    if (opts.voice && *opts.voice) {
        argv.push_back("-o");
        argv.push_back(opts.voice);
    }
    argv.push_back(text.c_str());
    argv.push_back(nullptr);
    RunSubprocess(argv.data());
}

void SpeakEspeak(const char* binary, const std::string& text, const PortTtsOptions& opts) {
    /* espeak / espeak-ng:  -s/--speed (words/min, default 175)
     *                       -p/--pitch (0..99, default 50)
     *                       -a/--amplitude (0..200, default 100)
     *                       -v/--voice */
    char speed_str[16], pitch_str[16], amp_str[16];
    std::snprintf(speed_str, sizeof(speed_str), "%d", (int)(80.0f + opts.rate * 250.0f));
    std::snprintf(pitch_str, sizeof(pitch_str), "%d", (int)(opts.pitch * 99.0f));
    std::snprintf(amp_str, sizeof(amp_str), "%d", (int)(opts.volume * 200.0f));
    std::vector<const char*> argv = {
        binary, "-s", speed_str, "-p", pitch_str, "-a", amp_str,
    };
    std::string voiceWithLang;
    if (opts.voice && *opts.voice) {
        voiceWithLang = opts.voice;
        argv.push_back("-v");
        argv.push_back(voiceWithLang.c_str());
    } else if (opts.language && *opts.language) {
        voiceWithLang = opts.language;
        argv.push_back("-v");
        argv.push_back(voiceWithLang.c_str());
    }
    argv.push_back(text.c_str());
    argv.push_back(nullptr);
    RunSubprocess(argv.data());
}

void SpeakSay(const std::string& text, const PortTtsOptions& opts) {
    /* macOS `say -v <voice> -r <words/min>` — no pitch/volume on the
     * CLI, but volume can be set globally and pitch is voice-property
     * controlled. We pass what we have and ignore the rest. */
    char rate_str[16];
    std::snprintf(rate_str, sizeof(rate_str), "%d", (int)(120.0f + opts.rate * 280.0f));
    std::vector<const char*> argv = { "say", "-r", rate_str };
    if (opts.voice && *opts.voice) {
        argv.push_back("-v");
        argv.push_back(opts.voice);
    }
    argv.push_back(text.c_str());
    argv.push_back(nullptr);
    RunSubprocess(argv.data());
}
#endif /* !_WIN32 */

#ifdef _WIN32
/* Windows: invoke PowerShell with a one-liner that loads System.Speech
 * and synthesises the text. The synth call is synchronous so we don't
 * need to track a finish event. */
int RunPowerShell(const std::string& script) {
    /* The subprocess takes ownership of the script via -Command. We
     * need to escape any single quotes in the text to keep PowerShell
     * happy. */
    std::string cmd = "powershell.exe -NoProfile -NonInteractive -Command \"" + script + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    /* CREATE_NO_WINDOW prevents a stray console flash. */
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    g_state.current_proc.store(pi.hProcess);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 0;
    GetExitCodeProcess(pi.hProcess, &rc);
    g_state.current_proc.store(nullptr);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)rc;
}

void SpeakNvda(const std::string& text, const PortTtsOptions& opts) {
    /* NVDA's controller API only does Speak / Cancel — rate / pitch /
     * volume are controlled by the user inside NVDA itself. That's
     * the *correct* behaviour: screen-reader users have already tuned
     * NVDA to their preferences, and overriding from app code would
     * fight muscle memory. We just hand it the text. */
    if (!g_nvda_speakText)
        return;
    /* nvdaController_speakText queues asynchronously; for URGENT we
     * cancel first to clear the queue. */
    if (opts.priority == PORT_TTS_PRIO_URGENT && g_nvda_cancelSpeech) {
        g_nvda_cancelSpeech();
    }
    /* UTF-8 → UTF-16 (NVDA expects wchar_t* / UTF-16 on Windows). */
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0)
        return;
    std::vector<wchar_t> wtext(static_cast<size_t>(len));
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), len);
    g_nvda_speakText(wtext.data());
}

void KillCurrent() {
    /* NVDA path: cancel its queue instead of killing a subprocess. */
    if (g_nvda_cancelSpeech && NvdaIsRunning()) {
        g_nvda_cancelSpeech();
    }
    HANDLE h = g_state.current_proc.exchange(nullptr);
    if (h)
        TerminateProcess(h, 1);
}

std::string EscapePsString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\'')
            out += "''";
        else if (c == '"')
            out += "`\"";
        else if (c == '`')
            out += "``";
        else
            out += c;
    }
    return out;
}

void SpeakSapi(const std::string& text, const PortTtsOptions& opts) {
    /* PowerShell snippet — rate -10..10, volume 0..100, voice picked
     * by display name. Pitch isn't directly exposed on SpVoice.Rate
     * so we skip it (handled via SSML if needed; not worth the
     * escape complexity here). */
    int rate_pct = (int)((opts.rate - 0.5f) * 20.0f); /* -10..10 */
    int volume_pct = (int)(opts.volume * 100.0f);
    std::string voicePick;
    if (opts.voice && *opts.voice) {
        voicePick = "$s.SelectVoice('" + EscapePsString(opts.voice) + "');";
    }
    char rate_buf[16], vol_buf[16];
    std::snprintf(rate_buf, sizeof(rate_buf), "%d", rate_pct);
    std::snprintf(vol_buf, sizeof(vol_buf), "%d", volume_pct);

    std::string script = "Add-Type -AssemblyName System.Speech;"
                         "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;" +
                         voicePick + "$s.Rate = " + rate_buf +
                         ";"
                         "$s.Volume = " +
                         vol_buf +
                         ";"
                         "$s.Speak('" +
                         EscapePsString(text) + "')";
    RunPowerShell(script);
}
#endif /* _WIN32 */

void SpeakOne(const Utterance& u) {
    g_state.in_flight.store(true);
#ifdef _WIN32
    /* Re-test NVDA each frame so the backend transparently follows
     * the user's choice: if NVDA starts mid-session we route there;
     * if NVDA exits we fall back to SAPI on the next call. */
    if (NvdaIsRunning()) {
        SpeakNvda(u.text, u.opts);
    } else {
        SpeakSapi(u.text, u.opts);
    }
#else
    switch (g_state.backend) {
        case Backend::SpdSay:
            SpeakSpdSay(u.text, u.opts);
            break;
        case Backend::EspeakNg:
            SpeakEspeak("espeak-ng", u.text, u.opts);
            break;
        case Backend::Espeak:
            SpeakEspeak("espeak", u.text, u.opts);
            break;
        case Backend::Say:
            SpeakSay(u.text, u.opts);
            break;
        default:
            break;
    }
#endif
    g_state.in_flight.store(false);
}

void WorkerLoop() {
    while (true) {
        Utterance u;
        {
            std::unique_lock<std::mutex> lk(g_state.queue_mu);
            g_state.queue_cv.wait(lk, [] { return g_state.quitting.load() || !g_state.queue.empty(); });
            if (g_state.quitting.load() && g_state.queue.empty())
                break;
            if (g_state.paused.load()) {
                lk.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            u = std::move(g_state.queue.front());
            g_state.queue.pop_front();
        }
        SpeakOne(u);
    }
}

PortTtsOptions ResolveOptions(const PortTtsOptions* in) {
    PortTtsOptions o = {};
    o.priority = in ? in->priority : PORT_TTS_PRIO_NORMAL;
    auto useOrDefault = [](float v, float def) { return (v != v /*NaN*/ || v < 0.0f || v > 1.0f) ? def : v; };
    o.rate = useOrDefault(in ? in->rate : 0.0f / 0.0f, g_state.rate.load());
    o.pitch = useOrDefault(in ? in->pitch : 0.0f / 0.0f, g_state.pitch.load());
    o.volume = useOrDefault(in ? in->volume : 0.0f / 0.0f, g_state.volume.load());
    std::lock_guard<std::mutex> lk(g_state.settings_mu);
    static thread_local std::string voiceCopy;
    static thread_local std::string langCopy;
    voiceCopy = in && in->voice && *in->voice ? in->voice : g_state.voice;
    langCopy = in && in->language && *in->language ? in->language : g_state.language;
    o.voice = voiceCopy.empty() ? nullptr : voiceCopy.c_str();
    o.language = langCopy.empty() ? nullptr : langCopy.c_str();
    o.dedupe = in ? in->dedupe : true;
    return o;
}

} // namespace

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

extern "C" bool Port_TTS_Init(void) {
    if (g_state.initialized.load())
        return g_state.backend != Backend::None;
    g_state.backend = DetectBackend();
    g_state.initialized.store(true);
    if (g_state.backend == Backend::None) {
        std::fprintf(stderr, "[tts] no backend available — feature disabled\n");
        return false;
    }
    /* Pull persisted settings into the cache. */
    g_state.enabled.store(Port_Config_GetTtsEnabled());
    g_state.rate.store(Port_Config_GetTtsRate());
    g_state.pitch.store(Port_Config_GetTtsPitch());
    g_state.volume.store(Port_Config_GetTtsVolume());
    {
        std::lock_guard<std::mutex> lk(g_state.settings_mu);
        const char* v = Port_Config_GetTtsVoice();
        const char* l = Port_Config_GetTtsLanguage();
        g_state.voice = v ? v : "";
        g_state.language = l ? l : "";
    }
    g_state.worker = std::thread(WorkerLoop);
    std::fprintf(stderr, "[tts] backend=%s, enabled=%d\n", BackendName(g_state.backend), (int)g_state.enabled.load());
    return true;
}

extern "C" void Port_TTS_Shutdown(void) {
    if (!g_state.initialized.load())
        return;
    g_state.quitting.store(true);
    Port_TTS_Stop();
    g_state.queue_cv.notify_all();
    if (g_state.worker.joinable())
        g_state.worker.join();
    g_state.initialized.store(false);
}

extern "C" void Port_TTS_Speak(const char* text, const PortTtsOptions* opts) {
    if (!g_state.initialized.load() || g_state.backend == Backend::None)
        return;
    if (!g_state.enabled.load())
        return;
    if (!text || !*text)
        return;
    PortTtsOptions o = ResolveOptions(opts);
    if (o.dedupe && IsDuplicate(text))
        return;

    Utterance u;
    u.text = text;
    u.opts = o;

    if (o.priority == PORT_TTS_PRIO_URGENT) {
        /* Drop current queue, kill the running subprocess, push at
         * the front. */
        {
            std::lock_guard<std::mutex> lk(g_state.queue_mu);
            g_state.queue.clear();
            g_state.queue.push_front(std::move(u));
        }
        KillCurrent();
    } else {
        std::lock_guard<std::mutex> lk(g_state.queue_mu);
        g_state.queue.push_back(std::move(u));
    }
    g_state.queue_cv.notify_one();
}

extern "C" void Port_TTS_Stop(void) {
    if (!g_state.initialized.load())
        return;
    {
        std::lock_guard<std::mutex> lk(g_state.queue_mu);
        g_state.queue.clear();
    }
    KillCurrent();
}

extern "C" void Port_TTS_Pause(void) {
    if (!g_state.initialized.load())
        return;
    g_state.paused.store(true);
    /* No portable "pause this subprocess" call — kill the current
     * utterance so Resume starts from the next queued one. */
    KillCurrent();
}

extern "C" void Port_TTS_Resume(void) {
    if (!g_state.initialized.load())
        return;
    g_state.paused.store(false);
    g_state.queue_cv.notify_one();
}

extern "C" bool Port_TTS_IsSpeaking(void) {
    return g_state.initialized.load() && g_state.in_flight.load();
}

extern "C" void Port_TTS_OnFocusChanged(const char* label) {
    PortTtsOptions o = {};
    o.priority = PORT_TTS_PRIO_NORMAL;
    o.rate = o.pitch = o.volume = 0.0f / 0.0f;
    o.dedupe = true;
    Port_TTS_Speak(label, &o);
}

extern "C" void Port_TTS_AnnounceMessage(const char* text) {
    PortTtsOptions o = {};
    o.priority = PORT_TTS_PRIO_NORMAL;
    o.rate = o.pitch = o.volume = 0.0f / 0.0f;
    o.dedupe = true;
    Port_TTS_Speak(text, &o);
}

extern "C" void Port_TTS_AnnounceError(const char* text) {
    PortTtsOptions o = {};
    o.priority = PORT_TTS_PRIO_URGENT;
    o.rate = o.pitch = o.volume = 0.0f / 0.0f;
    o.dedupe = false;
    Port_TTS_Speak(text, &o);
}

extern "C" bool Port_TTS_GetEnabled(void) {
    return g_state.enabled.load();
}
extern "C" void Port_TTS_SetEnabled(bool on) {
    g_state.enabled.store(on);
    Port_Config_SetTtsEnabled(on);
    if (!on)
        Port_TTS_Stop();
}

extern "C" float Port_TTS_GetRate(void) {
    return g_state.rate.load();
}
extern "C" void Port_TTS_SetRate(float v) {
    if (v < 0.0f)
        v = 0.0f;
    else if (v > 1.0f)
        v = 1.0f;
    g_state.rate.store(v);
    Port_Config_SetTtsRate(v);
}

extern "C" float Port_TTS_GetPitch(void) {
    return g_state.pitch.load();
}
extern "C" void Port_TTS_SetPitch(float v) {
    if (v < 0.0f)
        v = 0.0f;
    else if (v > 1.0f)
        v = 1.0f;
    g_state.pitch.store(v);
    Port_Config_SetTtsPitch(v);
}

extern "C" float Port_TTS_GetVolume(void) {
    return g_state.volume.load();
}
extern "C" void Port_TTS_SetVolume(float v) {
    if (v < 0.0f)
        v = 0.0f;
    else if (v > 1.0f)
        v = 1.0f;
    g_state.volume.store(v);
    Port_Config_SetTtsVolume(v);
}

extern "C" const char* Port_TTS_GetVoice(void) {
    /* Returns a pointer to the cached string; valid until the next
     * SetVoice. Callers must copy if they need to outlive the call. */
    std::lock_guard<std::mutex> lk(g_state.settings_mu);
    return g_state.voice.c_str();
}
extern "C" void Port_TTS_SetVoice(const char* v) {
    {
        std::lock_guard<std::mutex> lk(g_state.settings_mu);
        g_state.voice = v ? v : "";
    }
    Port_Config_SetTtsVoice(v ? v : "");
}

extern "C" const char* Port_TTS_GetLanguage(void) {
    std::lock_guard<std::mutex> lk(g_state.settings_mu);
    return g_state.language.c_str();
}
extern "C" void Port_TTS_SetLanguage(const char* v) {
    {
        std::lock_guard<std::mutex> lk(g_state.settings_mu);
        g_state.language = v ? v : "";
    }
    Port_Config_SetTtsLanguage(v ? v : "");
}

extern "C" const char* Port_TTS_GetBackendName(void) {
    return BackendName(g_state.backend);
}
#endif /* !__ANDROID__ */
