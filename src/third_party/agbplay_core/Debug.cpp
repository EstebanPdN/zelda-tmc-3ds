#include "Debug.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

static FILE *debug_file = nullptr;

bool Debug::open(const char *file)
{
    if (!file)
        return true;

    debug_file = fopen(file, "w");
    if (debug_file == nullptr) {
        perror("fopen");
        return false;
    }
    return true;
}

bool Debug::close()
{
    if (debug_file == nullptr)
        return true;

    if (fclose(debug_file) != 0)
        return false;
    debug_file = nullptr;
    return true;
}

static void (*callback)(const std::string &, void *) = nullptr;
static void *cb_obj = nullptr;

void Debug::set_callback(void (*cb)(const std::string &, void *), void *obj)
{
    callback = cb;
    cb_obj = obj;
}

void Debug::puts(const std::string &msg)
{
    /* gcc/libstdc++ 12 does not yet support std::format, use C fallback for now */
    const auto curTime = std::chrono::system_clock::now();
    const auto curTimeT = std::chrono::system_clock::to_time_t(curTime);
    const auto curTimeTM = *std::localtime(&curTimeT);
    std::ostringstream curTimeStream;
    curTimeStream << "[" << std::put_time(&curTimeTM, "%H:%M:%S") << "] " << msg << std::flush;
    const std::string finalMsg = curTimeStream.str();
    // std::string finalMsg = std::format("[{:%T}] {}", std::chrono::system_clock::now(), msg);

    if (debug_file) {
        fprintf(debug_file, "%s\n", finalMsg.c_str());
        fflush(debug_file);
    }

    if (callback)
        callback(finalMsg.c_str(), cb_obj);
    else
        std::cout << finalMsg << std::endl;
}
