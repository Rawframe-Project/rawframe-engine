#include "description_words.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <system_error>

namespace rawframe::world_kest {

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> found;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) {
            ++at;
        }
        const std::size_t kStart = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t') {
            ++at;
        }
        if (at > kStart) {
            found.push_back(line.substr(kStart, at - kStart));
        }
    }
    return found;
}

std::optional<double> parseReal(std::string_view word, double lowest, double highest) noexcept {
    double value = 0;
    const auto kRead = std::from_chars(word.data(), word.data() + word.size(), value);
    if (kRead.ec != std::errc{} || kRead.ptr != word.data() + word.size() || !std::isfinite(value) || value < lowest ||
        value > highest) {
        return std::nullopt;
    }
    return value;
}

bool lowerSnake(std::string_view word) noexcept {
    return !word.empty() && std::ranges::all_of(word, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
    });
}

std::unexpected<result::Error> badLine(std::size_t line, WorldKestError error, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(error), why)
            .error()
            .withContext("line", std::to_string(line))};
}

std::unexpected<result::Error> badLine(std::size_t line, std::string_view why) {
    return badLine(line, WorldKestError::BadGameLine, why);
}

} // namespace rawframe::world_kest
