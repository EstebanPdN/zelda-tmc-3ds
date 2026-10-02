#include "util.h"
#include "simple_format.h"
#include <iostream>
#include <iomanip>
#include <sstream>

void check_call(const std::vector<std::string>& cmd) {
    std::string cmdstr;
    bool first = true;
    for (const auto& segment : cmd) {
        if (first) {
            first = false;
        } else {
            cmdstr += " ";
        }
        cmdstr += segment;
    }
    int code = system(cmdstr.c_str());
    if (code != 0) {
        std::cerr << cmdstr << " failed with return code " << code << std::endl;
        std::exit(1);
    }
}

std::string opt_param(const std::string& format, int defaultVal, int value) {
    if (value != defaultVal) {
        return assetfmt::Format(format, value);
    }
    return "";
}

std::string hex_u32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << value;
    return out.str();
}
