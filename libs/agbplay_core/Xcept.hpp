#pragma once

#include <exception>
#include "SimpleFormat.hpp"
#include <string>

class Xcept : public std::exception
{
public:
    template<typename... Args> Xcept(const char* fmt, Args &&...args)
    {
        msg = agbplay::fmt::Format(fmt, std::forward<Args>(args)...);
    }
    ~Xcept() override;

    const char *what() const noexcept override;

private:
    std::string msg;
};
