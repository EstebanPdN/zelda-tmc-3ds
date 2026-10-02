#ifndef UTIL_H
#define UTIL_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

void check_call(const std::vector<std::string>& cmd);

std::string opt_param(const std::string& format, int defaultVal, int value);
std::string hex_u32(uint32_t value);

#endif
