#include "port_asset_log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace PortAssetLog {
namespace {

constexpr std::chrono::milliseconds kRedrawInterval{60};

} // namespace

Reporter& Reporter::Instance()
{
    static Reporter instance;
    return instance;
}

Reporter::Reporter() = default;

void Reporter::SetVerbose(bool verbose)
{
    verbose_.store(verbose, std::memory_order_relaxed);
}

void Reporter::SetProgressCallback(ProgressCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    progress_cb_ = std::move(cb);
    has_progress_cb_.store(static_cast<bool>(progress_cb_), std::memory_order_release);
}

void Reporter::FireProgressLocked()
{
    if (!progress_cb_) {
        return;
    }
    /* Snapshot the counters under the Reporter mutex so the
     * callback sees a consistent (phase, done, total). The callback
     * itself runs while the lock is held — embedders MUST keep it
     * cheap (the contract is "store into atomics and return"). */
    const std::size_t done = std::min(count_.load(std::memory_order_relaxed), total_);
    progress_cb_(std::string_view(phase_), done, total_);
}

void Reporter::BeginPhase(std::string_view name, std::size_t total)
{
    std::lock_guard<std::mutex> lk(mutex_);
    phase_ = std::string(name);
    total_ = total;
    count_.store(0, std::memory_order_relaxed);
    last_redraw_ = std::chrono::steady_clock::now() - kRedrawInterval;
    phase_active_ = true;
    std::fprintf(stdout, "%s: 0/%zu\n", phase_.c_str(), total_);
    std::fflush(stdout);
    FireProgressLocked();
}

void Reporter::Tick(std::size_t delta)
{
    if (delta == 0) {
        return;
    }
    count_.fetch_add(delta, std::memory_order_relaxed);

    /* The only per-Tick consumer left is the embedder's progress
     * callback (the in-game GUI bar). With no callback registered — the
     * common standalone-CLI case — there's nothing to do but count. */
    if (!has_progress_cb_.load(std::memory_order_acquire)) {
        return;
    }

    std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
    if (!lk.owns_lock() || !phase_active_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_redraw_ < kRedrawInterval) {
        return;
    }
    last_redraw_ = now;
    FireProgressLocked();
}

void Reporter::EndPhase()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!phase_active_) {
        return;
    }

    std::fprintf(stdout, "%s: %zu/%zu\n", phase_.c_str(), total_, total_);
    std::fflush(stdout);

    /* Fire one final progress event so embedders see done == total
     * before phase_active_ flips off (no further Ticks will land). */
    count_.store(total_, std::memory_order_relaxed);
    FireProgressLocked();

    phase_active_ = false;
}

void Reporter::Note(std::string_view msg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::fprintf(stdout, "  ..  %.*s\n", static_cast<int>(msg.size()), msg.data());
    std::fflush(stdout);
}

void Reporter::Warn(std::string_view msg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::fprintf(stdout, " warn  %.*s\n", static_cast<int>(msg.size()), msg.data());
    std::fflush(stdout);
}

void Reporter::Error(std::string_view msg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::fprintf(stderr, " fail  %.*s\n", static_cast<int>(msg.size()), msg.data());
    std::fflush(stderr);
}

void Reporter::Finish(std::string_view msg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    phase_active_ = false;
    if (!msg.empty()) {
        std::fprintf(stdout, "done  %.*s\n", static_cast<int>(msg.size()), msg.data());
        std::fflush(stdout);
    }
}

namespace {
std::mutex g_ensure_dir_mu;
std::unordered_set<std::string> g_ensured_dirs;
} // namespace

void EnsureDir(const std::filesystem::path& dir)
{
    if (dir.empty()) {
        return;
    }
    /* Hold the mutex across create_directories to avoid a race where one
     * thread inserts the dir into the cache, another thread looks it up,
     * sees it's "known", and immediately tries to write into it - but the
     * actual mkdir hasn't happened yet. The number of unique parent dirs
     * is small (a few hundred) so the contention is irrelevant compared to
     * the cost of the mkdir itself. */
    std::string key = dir.generic_string();
    std::lock_guard<std::mutex> lk(g_ensure_dir_mu);
    if (!g_ensured_dirs.insert(std::move(key)).second) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
}

void ResetEnsureDirCache()
{
    std::lock_guard<std::mutex> lk(g_ensure_dir_mu);
    g_ensured_dirs.clear();
}

BackgroundWriter& BackgroundWriter::Instance()
{
    static BackgroundWriter instance;
    return instance;
}

BackgroundWriter::~BackgroundWriter()
{
    Shutdown();
}

void BackgroundWriter::Submit(std::filesystem::path output_path, nlohmann::json json, int indent)
{
    /* Each dump runs on its own std::async task so the caller can start
     * the next phase immediately. The future is retained (so the task
     * isn't blocked-on at Submit time); Wait() drains them all. */
    auto fut = std::async(std::launch::async,
        [path = std::move(output_path), json = std::move(json), indent]() {
            EnsureDir(path.parent_path());
            std::string serialized = indent > 0 ? json.dump(indent) : json.dump();
            /* Fat buffer to coalesce multi-MB JSON into a handful of
             * write() syscalls. */
            static constexpr std::streamsize kBuf = 256 * 1024;
            std::vector<char> file_buf(static_cast<std::size_t>(kBuf));
            std::ofstream f;
            f.rdbuf()->pubsetbuf(file_buf.data(), kBuf);
            f.open(path, std::ios::binary);
            if (!f) {
                throw std::runtime_error(
                    fmt::format("BackgroundWriter: failed to open {} for writing", path.string()));
            }
            f.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
            f.flush();
            if (!f) {
                throw std::runtime_error(
                    fmt::format("BackgroundWriter: failed to write {}", path.string()));
            }
        });

    std::lock_guard<std::mutex> lk(mutex_);
    futures_.push_back(std::move(fut));
}

void BackgroundWriter::Wait()
{
    std::vector<std::future<void>> pending;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pending = std::move(futures_);
        futures_.clear();
    }

    std::exception_ptr first_error;
    for (auto& f : pending) {
        if (!f.valid()) {
            continue;
        }
        try {
            f.get();
        } catch (...) {
            if (!first_error) {
                first_error = std::current_exception();
            }
        }
    }
    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

void BackgroundWriter::Shutdown()
{
    /* Drain any in-flight writes at process teardown; errors here are
     * swallowed since Wait() is where callers surface them. */
    std::vector<std::future<void>> pending;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pending = std::move(futures_);
        futures_.clear();
    }
    for (auto& f : pending) {
        if (f.valid()) {
            try {
                f.get();
            } catch (...) {
                // ignore at teardown
            }
        }
    }
}

std::size_t WorkerCount()
{
    static const std::size_t cached = []() {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) {
            hw = 4;
        }
        if (hw < 2) {
            hw = 2;
        }
        if (hw > 16) {
            hw = 16;
        }
        if (const char* env = std::getenv("TMC_ASSET_JOBS")) {
            int parsed = std::atoi(env);
            if (parsed > 0 && parsed <= 64) {
                return static_cast<std::size_t>(parsed);
            }
        }
        return static_cast<std::size_t>(hw);
    }();
    return cached;
}

} // namespace PortAssetLog
