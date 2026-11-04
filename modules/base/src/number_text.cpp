#include "rawframe/base/number_text.h"

#include <charconv>
#include <cstdio>
#include <cstring>

namespace rawframe::base {

std::string numberText(double value) {
    char made[32];
    std::snprintf(made, sizeof made, "%.9g", value);
    double read = 0;
    const char* const kEnd = made + std::strlen(made);
    if (const auto [kAt, kError] = std::from_chars(made, kEnd, read);
        kError == std::errc{} && kAt == kEnd && read == value) {
        return made;
    }
    const auto [kWritten, kError] = std::to_chars(made, made + sizeof made, value);
    return std::string{made, kWritten};
}

} // namespace rawframe::base
