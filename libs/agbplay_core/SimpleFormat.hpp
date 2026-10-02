#pragma once

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace agbplay::fmt {

struct FormatSpec {
    bool alternate = false;
    bool zeroPad = false;
    int width = 0;
    char type = '\0';
};

inline FormatSpec ParseSpec(const std::string& spec) {
    FormatSpec parsed;
    size_t pos = 0;

    if (pos < spec.size() && spec[pos] == '#') {
        parsed.alternate = true;
        pos++;
    }
    if (pos < spec.size() && spec[pos] == '0') {
        parsed.zeroPad = true;
        pos++;
    }
    while (pos < spec.size() && spec[pos] >= '0' && spec[pos] <= '9') {
        parsed.width = parsed.width * 10 + (spec[pos] - '0');
        pos++;
    }
    if (pos < spec.size()) {
        parsed.type = spec[pos++];
    }
    if (pos != spec.size()) {
        throw std::runtime_error("Unsupported format specifier {" + spec + "}");
    }

    return parsed;
}

template <typename T>
inline void AppendValue(std::ostringstream& out, const T& value, const FormatSpec& spec) {
    if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        using PrintType = std::conditional_t<std::is_signed_v<T>, long long, unsigned long long>;
        const auto printable = static_cast<PrintType>(value);
        const bool useHex = spec.type == 'x' || spec.type == 'X';

        if (useHex) {
            if (spec.alternate) {
                out << "0x";
            }
            if (spec.zeroPad && spec.width > 0) {
                out << std::setfill('0') << std::setw(spec.width);
            } else if (spec.width > 0) {
                out << std::setw(spec.width);
            }
            if (spec.type == 'X') {
                out << std::uppercase;
            }
            out << std::hex << printable << std::dec;
            out << std::nouppercase << std::setfill(' ');
            return;
        }
    }

    out << value;
}

inline void FormatImpl(std::ostringstream& out, const std::string& format) {
    out << format;
}

template <typename T, typename... Rest>
void FormatImpl(std::ostringstream& out, const std::string& format, T&& value, Rest&&... rest) {
    const size_t open = format.find('{');
    if (open == std::string::npos) {
        throw std::runtime_error("Too many format arguments");
    }

    const size_t close = format.find('}', open);
    if (close == std::string::npos) {
        throw std::runtime_error("Unmatched '{' in format string");
    }

    out << format.substr(0, open);

    std::string spec;
    if (close > open + 1) {
        if (format[open + 1] != ':') {
            throw std::runtime_error("Unsupported format placeholder");
        }
        spec = format.substr(open + 2, close - open - 2);
    }

    AppendValue(out, std::forward<T>(value), ParseSpec(spec));
    FormatImpl(out, format.substr(close + 1), std::forward<Rest>(rest)...);
}

template <typename... Args>
std::string Format(const std::string& format, Args&&... args) {
    std::ostringstream out;
    FormatImpl(out, format, std::forward<Args>(args)...);
    return out.str();
}

} // namespace agbplay::fmt
