#pragma once

#include <stdexcept>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PI_F (float(M_PI))

inline void CStrAppend(char *dest, size_t *index, const char *src)
{
    char ch;
    const char *copyChar = src;
    while ((ch = *copyChar++) != '\0')
        dest[(*index)++] = ch;
}

namespace {
    template<typename T>
        void ReplaceIllegalPathCharacters(std::basic_string<T> &str, T filler)
    {
        static const char illegalChars[] = "<>:\"/\\|?*";
        for (auto &c : str) {
            if (c <= 31) {
                c = filler;
            } else {
                for (const char *illegalChar = &illegalChars[0]; *illegalChar; illegalChar++) {
                    if (c == static_cast<T>(*illegalChar))
                        c = filler;
                }
            }
        }
    }

    inline std::u8string StrToU8Str(const std::string &s)
    {
        return reinterpret_cast<const char8_t *>(s.c_str());
    }
}
