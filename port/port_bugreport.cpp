/*
 * port_bugreport.cpp — F9-triggered + auto-on-crash bug-report capture.
 *
 * Bundles screenshot (PNG) + save file + game state into a timestamped
 * directory so testers can attach it to GitHub issues without needing to
 * gather logs themselves. The crash handler hooks SIGSEGV/SIGABRT/SIGFPE/
 * SIGILL/SIGBUS (POSIX) and SetUnhandledExceptionFilter (Windows), captures
 * a bundle plus a backtrace, then re-raises so the OS still produces its
 * default core dump / WER report.
 */

#include "port_bugreport.h"
#include "port_version.h"
#include "port_widescreen.h"

#include <SDL3/SDL.h>
#include <png.h>
#include <virtuappu.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__linux__) || defined(__APPLE__)
#if !defined(__ANDROID__)
#include <execinfo.h> /* backtrace(); bionic only grew this at API 33 */
#endif
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/ucontext.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

extern "C" {
extern uint8_t* gRomData;
extern uint32_t gRomSize;
extern uint32_t virtuappu_frame_buffer[];
extern uint16_t gBgPltt[256];
extern uint8_t gVram[];
extern uint8_t gIoMem[];
}

extern "C" {
struct PortBugReportState {
    uint8_t area;
    uint8_t room;
    int16_t playerX;
    int16_t playerY;
    int16_t playerZ;
    uint8_t playerHealth;
    uint8_t playerMaxHealth;
    int frameCount;
};
PortBugReportState Port_BugReport_GetGameState(void);
}

namespace {

/* Framebuffer stride tracks the configured widescreen width (xmake injects
 * MODE1_GBA_WIDTH for the whole target; 240 = native). Reading at the wrong
 * stride shears the capture, so this MUST match virtuappu_frame_buffer. */
constexpr int kFrameW = MODE1_GBA_WIDTH;
constexpr int kFrameH = 160;

std::string TimestampString() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_buf);
    return buf;
}

const char* PlatformString() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

const char* ArchString() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#else
    return "unknown";
#endif
}

const char* BuildModeString() {
#ifdef NDEBUG
    return "release";
#else
    return "debug";
#endif
}

const char* GameRegionString() {
#if defined(EU)
    return "EU";
#elif defined(USA)
    return "USA";
#elif defined(JP)
    return "JP";
#else
    return "?";
#endif
}

bool WriteScreenshotPNG(const std::filesystem::path& path) {
    /* virtuappu_frame_buffer is stride kFrameW. Runtime-disabled widescreen
     * captures only the native 240 columns so F9/repro output matches what
     * the presenter shows; active true-wide gameplay captures the full frame. */
    FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) {
        std::fprintf(stderr, "[BUG] PNG fopen failed: %s\n", path.string().c_str());
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        std::fprintf(stderr, "[BUG] libpng longjmp\n");
        return false;
    }

    const int outW =
        (Port_Widescreen_IsActive() && Port_Widescreen_ShadowsLive()) ? Port_Widescreen_EffectiveViewWidth() : 240;

    png_init_io(png, fp);
    png_set_IHDR(png, info, outW, kFrameH, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    uint8_t row[kFrameW * 3];
    for (int y = 0; y < kFrameH; y++) {
        const uint32_t* src = &virtuappu_frame_buffer[y * kFrameW];
        for (int x = 0; x < outW; x++) {
            uint32_t p = src[x];
            row[x * 3 + 0] = static_cast<uint8_t>(p & 0xFF);         /* R (ABGR LE: byte0=R) */
            row[x * 3 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);  /* G */
            row[x * 3 + 2] = static_cast<uint8_t>((p >> 16) & 0xFF); /* B */
        }
        png_write_row(png, row);
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
    return true;
}

/* Repro-harness shim (#138/#79): capture the base GBA framebuffer to an
 * explicit path, headless-safe and cross-platform (used by
 * port_repro_litarea.c on Linux and under Wine). */
extern "C" int Port_CaptureBaseFramebufferPNG(const char* path) {
    return WriteScreenshotPNG(std::filesystem::path(path)) ? 1 : 0;
}

bool CopySaveFile(const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::copy_file("tmc.sav", dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "[BUG] copy save: %s\n", ec.message().c_str());
        return false;
    }
    return true;
}

bool CopyProcMaps(const std::filesystem::path& dest) {
    /* Snapshot /proc/self/maps so the crash IP in backtrace.txt can be
     * resolved offline (subtract binary load base, then addr2line on the
     * offset). The handler can't safely call dladdr to do this online —
     * /proc reads are async-signal-safe via plain syscalls. */
    std::error_code ec;
    std::filesystem::copy_file("/proc/self/maps", dest, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

bool WriteStateText(const std::filesystem::path& path, const PortBugReportState& s, const char* reason) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "TMC PC port bug report\n";
    out << "------\n";
    out << "Reason:    " << (reason && *reason ? reason : "user") << "\n";
    out << "Version:   " << TMC_PC_VERSION << " (" << GameRegionString() << ")\n";
    out << "Build:     " << BuildModeString() << " " << PlatformString() << "/" << ArchString() << "\n";
    out << "Area:      0x" << std::hex << static_cast<unsigned>(s.area) << "\n";
    out << "Room:      0x" << std::hex << static_cast<unsigned>(s.room) << std::dec << "\n";
    out << "Pos:       (" << s.playerX << ", " << s.playerY << ", " << s.playerZ << ")\n";
    out << "HP:        " << static_cast<unsigned>(s.playerHealth) << " / " << static_cast<unsigned>(s.playerMaxHealth)
        << "\n";
    out << "Frame:     " << s.frameCount << "\n";
    out << "ROM size:  " << gRomSize << " bytes\n";

    /* Compact BG-layer dump for triaging "screen-is-black" repros (#108,
     * #103 family). One line per BG with: ctl reg, scroll, char-base byte
     * sum, screen-base byte sum, and the palette bank's zero-count. If a
     * BG renders black, exactly one of these will explain why. */
    {
        auto sum_region = [](size_t off, size_t len) -> uint64_t {
            uint64_t s = 0;
            for (size_t i = 0; i < len; ++i)
                s += gVram[off + i];
            return s;
        };
        uint16_t dispCtl = static_cast<uint16_t>(gIoMem[0x00] | (gIoMem[0x01] << 8));
        out << "\ndispCtl=0x" << std::hex << dispCtl << std::dec << "\n";
        for (int b = 0; b < 4; ++b) {
            uint16_t ctl = static_cast<uint16_t>(gIoMem[0x08 + b * 2] | (gIoMem[0x09 + b * 2] << 8));
            uint16_t hofs = static_cast<uint16_t>(gIoMem[0x10 + b * 4] | (gIoMem[0x11 + b * 4] << 8));
            uint16_t vofs = static_cast<uint16_t>(gIoMem[0x12 + b * 4] | (gIoMem[0x13 + b * 4] << 8));
            int cb = (ctl >> 2) & 3;
            int sb = (ctl >> 8) & 0x1F;
            size_t firstEntryOff = static_cast<size_t>(sb) * 0x800u;
            uint16_t firstEntry = static_cast<uint16_t>(gVram[firstEntryOff] | (gVram[firstEntryOff + 1] << 8));
            int pal = (firstEntry >> 12) & 0xF;
            unsigned palZero = 0;
            for (int i = 0; i < 16; ++i) {
                if (gBgPltt[pal * 16 + i] == 0)
                    palZero++;
            }
            out << std::hex << "BG" << b << " ctl=0x" << ctl << std::dec << " scroll=(" << hofs << "," << vofs << ")"
                << " cb" << cb << "(sum=" << sum_region(cb * 0x4000u, 0x4000) << ")"
                << " sb" << sb << "(sum=" << sum_region(sb * 0x800u, 0x800) << ")"
                << " firstTile=0x" << std::hex << firstEntry << std::dec << " pal" << pal << "(zero=" << palZero
                << "/16)"
                << "\n";
        }
        unsigned bgZero = 0;
        for (int i = 0; i < 256; ++i)
            if (gBgPltt[i] == 0)
                bgZero++;
        out << "gBgPltt zero entries: " << bgZero << " / 256\n";
    }

    return out.good();
}

#if defined(__linux__) || defined(__APPLE__)
struct CrashRegs {
    void* ip; /* RIP / PC at fault */
    void* sp; /* RSP / SP at fault */
    void* bp; /* RBP / FP at fault (may be omitted by -O3) */
    void* lr; /* link register on aarch64; nullptr on x86-64 */
};

CrashRegs ExtractCrashRegs(void* ucontext) {
    CrashRegs r{};
    if (!ucontext) {
        return r;
    }
    auto* uc = static_cast<ucontext_t*>(ucontext);
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    r.ip = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RIP]);
    r.sp = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RSP]);
    r.bp = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RBP]);
#elif defined(__linux__) && defined(__aarch64__)
    r.ip = reinterpret_cast<void*>(uc->uc_mcontext.pc);
    r.sp = reinterpret_cast<void*>(uc->uc_mcontext.sp);
    r.lr = reinterpret_cast<void*>(uc->uc_mcontext.regs[30]);
#elif defined(__APPLE__) && defined(__x86_64__)
    r.ip = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rip);
    r.sp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rsp);
    r.bp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rbp);
#elif defined(__APPLE__) && defined(__aarch64__)
    r.ip = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__pc);
    r.sp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__sp);
    r.lr = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__lr);
#endif
    return r;
}

#if defined(__linux__) || defined(__APPLE__)
/* Pre-made pipe used to probe pointer readability from inside the signal
 * handler without faulting (see SafeReadPointer). Created once at install. */
int s_probe_fds[2] = { -1, -1 };
#endif

void* SafeReadPointer(void* p) {
    /* Read 8 bytes from *p without faulting if p is unmapped — this runs
     * INSIDE the SIGSEGV handler while walking the rbp frame chain, and a
     * corrupt frame pointer (e.g. rbp ~0x6 → reading at 0xe) used to fault
     * the handler itself and truncate the bugreport (the recurring
     * `crash:SIGSEGV@0xe` in WriteBacktracePosix). */
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    /* Cheap pre-filter: reject NULL, the never-mapped low 64 KB (catches the
     * small garbage frame pointers that crashed us), misaligned addresses (a
     * real saved-rbp / return slot is 8-byte aligned), and non-canonical
     * x86-64 user addresses. */
    if (a < 0x10000 || (a & 7u) != 0u || a >= 0x800000000000ULL) {
        return nullptr;
    }
    void* v = nullptr;
#if defined(__linux__) || defined(__APPLE__)
    /* Definitive readability probe: write() to a pre-made pipe returns
     * -1/EFAULT for an unmapped source instead of raising SIGSEGV, and write()
     * is async-signal-safe. If the kernel accepts the 8 bytes, read them back
     * to recover the value; the pipe is non-blocking and drained every call so
     * it never fills. */
    if (s_probe_fds[1] >= 0) {
        ssize_t w = write(s_probe_fds[1], p, sizeof(v));
        if (w == static_cast<ssize_t>(sizeof(v))) {
            if (read(s_probe_fds[0], &v, sizeof(v)) != static_cast<ssize_t>(sizeof(v))) {
                v = nullptr;
            }
            return v;
        }
        if (w > 0) { /* defensive: drain a partial write so the pipe stays in sync */
            char tmp[sizeof(v)];
            (void)read(s_probe_fds[0], tmp, static_cast<size_t>(w));
        }
        return nullptr;
    }
#endif
    /* Fallback (probe pipe unavailable): the pre-filter above has already
     * rejected the obviously-bogus pointers that caused the handler crash. */
    std::memcpy(&v, p, sizeof(v));
    return v;
}

/* Resolve a code address (relative to the binary base) and write a one-line
 * description: `0xADDR <module>(+0xOFFSET) [symbol+disp]`. dladdr is
 * sufficient for the crash IP on glibc — for a full unwind, libunwind would
 * be needed. */
void WriteResolvedAddr(FILE* fp, const char* label, void* addr) {
    Dl_info info{};
    if (dladdr(addr, &info) && info.dli_fname) {
        uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
        uintptr_t offs = reinterpret_cast<uintptr_t>(addr) - base;
        if (info.dli_sname) {
            uintptr_t sym_off = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(info.dli_saddr);
            std::fprintf(fp, "%s 0x%lx %s(+0x%lx) [%s+0x%lx]\n", label,
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(addr)), info.dli_fname,
                         static_cast<unsigned long>(offs), info.dli_sname, static_cast<unsigned long>(sym_off));
        } else {
            std::fprintf(fp, "%s 0x%lx %s(+0x%lx)\n", label,
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(addr)), info.dli_fname,
                         static_cast<unsigned long>(offs));
        }
    } else {
        std::fprintf(fp, "%s 0x%lx (unmapped)\n", label, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(addr)));
    }
}

void WriteBacktracePosix(const std::filesystem::path& path, const CrashRegs& regs, void* fault_addr) {
    FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) {
        return;
    }

    /* Stage 1 — write raw hex addresses with fflush BEFORE *any* call
     * that's not async-signal-safe (no dladdr, no malloc, no anything
     * that might lock libc internals). Past attempts at "hardening"
     * still kicked off with a dladdr() to get the binary base for
     * offset reporting; that itself crashes inside SIGSEGV handling
     * and leaves the file empty. Skip offsets entirely in Stage 1 —
     * the absolute IP is enough for `addr2line -e tmc_pc <ip>` since
     * the binary is the only thing in the address range we care about. */
    if (regs.ip) {
        std::fprintf(fp, "Crash IP:    0x%lx\n", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(regs.ip)));
    } else {
        std::fprintf(fp, "Crash IP:    0x0 (program jumped to NULL — likely a NULL function-pointer call)\n");
    }
    std::fprintf(fp, "Fault addr:  0x%lx\n", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(fault_addr)));
    std::fflush(fp); /* CHECKPOINT 1 */

#if defined(__x86_64__) || defined(_M_X64)
    {
        void* caller = SafeReadPointer(regs.sp);
        if (caller) {
            std::fprintf(fp, "Caller (*sp):0x%lx\n", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(caller)));
        }
        void* fp_link = regs.bp;
        for (int i = 0; i < 16 && fp_link; i++) {
            void* saved_rbp = SafeReadPointer(fp_link);
            void* saved_ret = SafeReadPointer(static_cast<char*>(fp_link) + 8);
            if (!saved_ret)
                break;
            std::fprintf(fp, "fp[%d]:       0x%lx\n", i,
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(saved_ret)));
            if (saved_rbp == fp_link)
                break;
            fp_link = saved_rbp;
        }
    }
#elif defined(__aarch64__)
    if (regs.lr) {
        std::fprintf(fp, "Caller (lr): 0x%lx\n", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(regs.lr)));
    }
#endif
    std::fflush(fp); /* CHECKPOINT 2 — raw frame chain durable */

    /* Stage 2 — try to resolve via dladdr to add a binary-base line and
     * symbol names. dladdr is NOT signal-safe and can deadlock or fault
     * if the signal interrupted code that already held the linker lock.
     * If it crashes us we still have the absolute IPs from above. */
    {
        Dl_info self_info{};
        if (dladdr(reinterpret_cast<void*>(&WriteBacktracePosix), &self_info) && self_info.dli_fbase) {
            std::fprintf(fp, "\nBinary base: 0x%lx  (subtract from above for offsets)\n",
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(self_info.dli_fbase)));
            std::fprintf(fp, "Resolve:     addr2line -e tmc_pc -fp <offset>\n");
        }
    }
    std::fflush(fp);

    std::fprintf(fp, "\n--- symbolicated (best-effort) ---\n");
    if (regs.ip)
        WriteResolvedAddr(fp, "Crash IP:    ", regs.ip);
#if defined(__x86_64__) || defined(_M_X64)
    {
        void* caller = SafeReadPointer(regs.sp);
        if (caller)
            WriteResolvedAddr(fp, "Caller (*sp):", caller);
    }
#endif
#if !defined(__ANDROID__)
    std::fprintf(fp, "\nHandler stack (backtrace() — does not cross the signal frame):\n");
    std::fflush(fp);
    void* frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, fileno(fp));
#endif
    std::fclose(fp);
}
#endif

#ifdef _WIN32
void WriteBacktraceWindows(const std::filesystem::path& path, CONTEXT* ctx) {
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);

    void* frames[64];
    USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);

    FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) {
        return;
    }

    char symBuf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    if (ctx) {
        std::fprintf(fp, "Exception context:\n");
#if defined(_M_ARM64) || defined(__aarch64__)
        /* Windows ARM64 CONTEXT exposes the program counter as Pc (Rip is x64-only). */
        std::fprintf(fp, "  PC=0x%llx\n", static_cast<unsigned long long>(ctx->Pc));
#else
        std::fprintf(fp, "  RIP=0x%llx\n", static_cast<unsigned long long>(ctx->Rip));
#endif
    }
    for (USHORT i = 0; i < n; i++) {
        DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 displ = 0;
        if (SymFromAddr(proc, addr, &displ, sym)) {
            std::fprintf(fp, "  [%2u] 0x%llx %s+0x%llx\n", i, static_cast<unsigned long long>(addr), sym->Name,
                         static_cast<unsigned long long>(displ));
        } else {
            std::fprintf(fp, "  [%2u] 0x%llx (no symbol)\n", i, static_cast<unsigned long long>(addr));
        }
    }
    std::fclose(fp);
}
#endif

/* Re-entry guard: a fault while we're already capturing should not loop.
 * 0 = idle, 1 = capturing. compare_exchange flips to 1 on entry. */
std::atomic<int> g_capturing{ 0 };

#if defined(__linux__) || defined(__APPLE__)
/* Set by CrashHandlerPosix right before it calls Port_BugReport_Capture so that
 * Capture writes backtrace.txt FIRST — before the heavier, not-async-signal-safe
 * screenshot/save/state/malloc steps that can deadlock or re-fault if the crash
 * happened inside malloc/SDL/stdio while a lock was held. The backtrace is the
 * most valuable artifact, so it must escape first. Cleared after the early
 * write; the F9 manual-capture path never sets it (and gets no backtrace, as
 * before). */
std::atomic<bool> g_crashBacktracePending{ false };
CrashRegs g_crashRegs{};
void* g_crashFaultAddr = nullptr;
#endif

} // namespace

extern "C" char* Port_BugReport_Capture(const char* reason) {
    int expected = 0;
    if (!g_capturing.compare_exchange_strong(expected, 1)) {
        return nullptr;
    }
    struct Releaser {
        ~Releaser() {
            g_capturing.store(0);
        }
    } releaser;

    const std::string ts = TimestampString();
    const std::string dirname = "bugreport_" + ts;

    std::error_code ec;
    std::filesystem::create_directory(dirname, ec);
    if (ec) {
        std::fprintf(stderr, "[BUG] mkdir failed: %s\n", ec.message().c_str());
        return nullptr;
    }

#if defined(__linux__) || defined(__APPLE__)
    /* Crash path: write backtrace.txt FIRST (the most valuable artifact),
     * before the heavier steps below that may deadlock if the fault occurred
     * inside malloc/SDL/stdio holding a lock. The F9 manual path skips this
     * (g_crashBacktracePending is false). WriteBacktracePosix still uses stdio
     * — not strictly async-signal-safe — but ordering it first is a strict
     * improvement over writing it only after the heavy bundle steps succeed. */
    if (g_crashBacktracePending.load()) {
        WriteBacktracePosix(std::filesystem::path(dirname) / "backtrace.txt", g_crashRegs, g_crashFaultAddr);
        g_crashBacktracePending.store(false);
    }
#endif

    PortBugReportState s = Port_BugReport_GetGameState();

    bool ok = true;
    ok &= WriteScreenshotPNG(std::filesystem::path(dirname) / "screenshot.png");
    ok &= CopySaveFile(std::filesystem::path(dirname) / "save.bin");
    ok &= WriteStateText(std::filesystem::path(dirname) / "state.txt", s, reason);
#if defined(__linux__)
    /* Snapshot /proc/self/maps too. Best-effort — failure doesn't taint
     * the bundle's `ok` flag because crash reports without maps are
     * still useful (just harder to addr2line). */
    CopyProcMaps(std::filesystem::path(dirname) / "maps.txt");
#endif

    std::fprintf(stderr, "[BUG] Captured %s (ok=%d, reason=%s)\n", dirname.c_str(), ok ? 1 : 0,
                 reason ? reason : "user");

    char* out = static_cast<char*>(std::malloc(dirname.size() + 1));
    if (out) {
        std::memcpy(out, dirname.c_str(), dirname.size() + 1);
    }
    return out;
}

/* ---------- Crash handlers ---------- */

namespace {

#if defined(__linux__) || defined(__APPLE__)

const char* SignalName(int sig) {
    switch (sig) {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGABRT:
            return "SIGABRT";
        case SIGFPE:
            return "SIGFPE";
        case SIGILL:
            return "SIGILL";
        case SIGBUS:
            return "SIGBUS";
        default:
            return "SIG?";
    }
}

/* Alternate signal stack so we can still capture on stack-overflow SEGV.
 * 64 KiB — glibc 2.34+ made SIGSTKSZ a runtime sysconf, so we can't size
 * statically off it; this is comfortably above MINSIGSTKSZ on supported
 * targets and small enough not to bloat .bss. */
alignas(16) uint8_t g_altstack[65536];

void CrashHandlerPosix(int sig, siginfo_t* info, void* ucontext) {
    CrashRegs regs = ExtractCrashRegs(ucontext);
    void* fault_addr = info ? info->si_addr : nullptr;

    char reason[96];
    std::snprintf(reason, sizeof(reason), "crash:%s@%p ip=%p", SignalName(sig), fault_addr, regs.ip);

    /* Hand the crash context to Port_BugReport_Capture so it writes
     * backtrace.txt BEFORE its heavier (non-async-signal-safe) steps, in case
     * those deadlock when the fault came from inside malloc/SDL/stdio. */
    g_crashRegs = regs;
    g_crashFaultAddr = fault_addr;
    g_crashBacktracePending.store(true);

    char* dir = Port_BugReport_Capture(reason);
    g_crashBacktracePending.store(false); /* clear if Capture bailed pre-write */
    if (dir) {
        /* backtrace.txt was already written (early, inside Capture). Report the
         * path on stderr. Deliberately NO SDL_ShowSimpleMessageBox here:
         * calling SDL from a signal handler can re-enter a held SDL/windowing
         * lock and deadlock — and the crash often originates in the SDL-heavy
         * present path. The stderr line conveys the path; the re-raise below
         * still produces an OS core dump. */
        char abs[4096];
        const char* shown = realpath(dir, abs) ? abs : dir;
        std::fprintf(stderr, "\n[BUG] CRASH (%s) — bundle saved to:\n    %s\n", SignalName(sig), shown);
        std::fflush(stderr);
        std::free(dir);
    } else {
        std::fprintf(stderr, "[BUG] CRASH (%s) but bug-report capture FAILED.\n", SignalName(sig));
        std::fflush(stderr);
    }

    /* SA_RESETHAND was set during installation, so the next delivery uses
     * the default disposition. Re-raise to let the OS produce a core dump
     * and exit with the conventional 128+sig status. */
    std::raise(sig);
}

void InstallPosixHandlers() {
    /* Pipe used by SafeReadPointer to probe pointer readability from inside the
     * handler without faulting. Non-blocking + close-on-exec; drained on every
     * use so it never fills. Created here (not in the handler) since pipe() is
     * not async-signal-safe. */
    if (s_probe_fds[0] < 0 && pipe(s_probe_fds) == 0) {
        fcntl(s_probe_fds[0], F_SETFL, O_NONBLOCK);
        fcntl(s_probe_fds[1], F_SETFL, O_NONBLOCK);
        fcntl(s_probe_fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(s_probe_fds[1], F_SETFD, FD_CLOEXEC);
    }

    stack_t ss{};
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof(g_altstack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = CrashHandlerPosix;

    int signals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
    for (int s : signals) {
        sigaction(s, &sa, nullptr);
    }
}

#endif /* POSIX */

#ifdef _WIN32

LONG WINAPI CrashHandlerWindows(EXCEPTION_POINTERS* ep) {
    char reason[96];
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    std::snprintf(reason, sizeof(reason), "crash:0x%08lx@%p", static_cast<unsigned long>(code), addr);

    char* dir = Port_BugReport_Capture(reason);
    if (dir) {
        WriteBacktraceWindows(std::filesystem::path(dir) / "backtrace.txt", ep ? ep->ContextRecord : nullptr);
        std::free(dir);
    }

    /* Let WER / the debugger pick up after us. */
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallWindowsHandler() {
    SetUnhandledExceptionFilter(CrashHandlerWindows);
}

#endif /* _WIN32 */

std::atomic<int> g_handlers_installed{ 0 };

} // namespace

extern "C" void Port_BugReport_InstallCrashHandlers(void) {
    int expected = 0;
    if (!g_handlers_installed.compare_exchange_strong(expected, 1)) {
        return;
    }
#if defined(__linux__) || defined(__APPLE__)
    InstallPosixHandlers();
#endif
#ifdef _WIN32
    InstallWindowsHandler();
#endif
}
