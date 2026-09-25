#pragma once

// Field readers the audio documents share.

#include "rawframe/document/errors.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace rawframe::audio {

/// The highest fader or level, in decibels; there is no lowest but silence.
inline constexpr double kMaximumDecibels = 24.0;

/// The value of a closed word, where its default (the first) is omitted.
template <typename Enum, std::size_t Count>
inline result::Result<Enum> closedWord(const document::Record& record,
                                       std::string_view field,
                                       const std::array<std::pair<std::string_view, Enum>, Count>& words) {
    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kWord, record.optionalText(field));
    if (!kWord) {
        return words[0].second;
    }
    if (*kWord == words[0].first) {
        return document::notCanonical(record.pathOf(field), "a field at its default is omitted");
    }
    for (const auto& [kName, kValue] : words) {
        if (kName == *kWord) {
            return kValue;
        }
    }
    return document::invalid(record.pathOf(field), "not one of the field's words");
}

inline bool machineName(std::string_view name, std::size_t limit) noexcept {
    if (name.empty() || name.size() > limit || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    return std::ranges::all_of(name, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
    });
}

inline std::optional<std::uint64_t> parseIdentity(std::string_view text) noexcept {
    if (text.size() != 16) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char kDigit : text) {
        value <<= 4U;
        if (kDigit >= '0' && kDigit <= '9') {
            value |= static_cast<std::uint64_t>(kDigit - '0');
        } else if (kDigit >= 'a' && kDigit <= 'f') {
            value |= static_cast<std::uint64_t>(kDigit - 'a' + 10);
        } else {
            return std::nullopt;
        }
    }
    return value;
}

/// A level in decibels, at most kMaximumDecibels.
inline result::Result<float> decibels(const document::Record& record, std::string_view field) {
    RAWFRAME_TRY_ASSIGN(const double kLevel, record.real(field, 0.0));
    if (!(kLevel <= kMaximumDecibels) || !std::isfinite(kLevel)) {
        return document::invalid(record.pathOf(field), "a level in decibels is finite and at most 24");
    }
    return static_cast<float>(kLevel);
}

} // namespace rawframe::audio
