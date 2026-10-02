#include "Xcept.hpp"

Xcept::~Xcept()
{
}

const char *Xcept::what() const noexcept
{
    return msg.c_str();
}
