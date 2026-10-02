#pragma once

#include "SimpleFormat.hpp"
#include <string>

namespace Debug
{
    void puts(const std::string &msg);
    template<typename... Args> void print(const char* fmt, Args &&...args)
    {
        Debug::puts(agbplay::fmt::Format(fmt, std::forward<Args>(args)...));
    }
    bool open(const char *file);
    bool close();
    void set_callback(void (*cb)(const std::string &, void *), void *obj);
}    // namespace Debug
