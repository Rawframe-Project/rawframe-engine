#include "rawframe/texture/errors.h"
#include "rawframe/texture_import/import.h"

#include <charconv>
#include <cmath>
#include <string_view>

namespace rawframe::texture_import {

namespace {

std::unexpected<result::Error> badSource(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, texture::kTextureDomain, code(texture::TextureError::BadSource), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(
        result::ErrorClass::ResourceExhausted, texture::kTextureDomain, code(texture::TextureError::OverLimit), why);
}

/// The words of a line, split at spaces and tabs.
std::vector<std::string_view> wordsOf(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < line.size()) {
        const std::size_t kStart = line.find_first_not_of(" \t", at);
        if (kStart == std::string_view::npos) {
            break;
        }
        const std::size_t kEnd = std::min(line.find_first_of(" \t", kStart), line.size());
        words.push_back(line.substr(kStart, kEnd - kStart));
        at = kEnd;
    }
    return words;
}

/// A word as a finite float, all of it.
std::optional<float> floatOf(std::string_view word) {
    float value = 0;
    const auto [kEnd, kError] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (kError != std::errc{} || kEnd != word.data() + word.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace

result::Result<texture::Texture> decodeGrading(std::span<const std::byte> source, const GradingLimits& limits) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    std::uint32_t side = 0;
    texture::Texture table{.format = texture::Format::Rgba16Float};
    std::size_t entries = 0;
    std::size_t at = 0;
    while (at < kText.size()) {
        std::size_t end = kText.find('\n', at);
        end = end == std::string_view::npos ? kText.size() : end;
        std::string_view line = kText.substr(at, end - at);
        at = end + 1;
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        const std::vector<std::string_view> kWords = wordsOf(line);
        if (kWords.empty() || kWords[0].starts_with('#')) {
            continue;
        }
        const bool kKeyword = kWords[0] == "TITLE" || kWords[0] == "LUT_3D_SIZE" || kWords[0] == "DOMAIN_MIN" ||
                              kWords[0] == "DOMAIN_MAX" || kWords[0] == "LUT_1D_SIZE";
        if (kKeyword && entries > 0) {
            return badSource("a grading table's keywords come before its entries");
        }
        if (kWords[0] == "TITLE") {
            continue;
        }
        if (kWords[0] == "LUT_3D_SIZE") {
            std::uint32_t asked = 0;
            if (side != 0 || kWords.size() != 2) {
                return badSource("a grading table names its size once");
            }
            const auto [kEnd, kError] = std::from_chars(kWords[1].data(), kWords[1].data() + kWords[1].size(), asked);
            if (kError != std::errc{} || kEnd != kWords[1].data() + kWords[1].size() || asked < 2) {
                return badSource("a grading table's size is a whole number, at least two");
            }
            if (asked > limits.maximumSide) {
                return overLimit("a grading table's size is past its limit");
            }
            side = asked;
            table.depth = side;
            table.levels.push_back(texture::Level{.width = side, .height = side});
            table.levels[0].bytes.resize(std::size_t{side} * side * side * 8);
            continue;
        }
        if (kWords[0] == "DOMAIN_MIN" || kWords[0] == "DOMAIN_MAX") {
            const float kExpected = kWords[0] == "DOMAIN_MIN" ? 0.0F : 1.0F;
            if (kWords.size() != 4 || floatOf(kWords[1]) != kExpected || floatOf(kWords[2]) != kExpected ||
                floatOf(kWords[3]) != kExpected) {
                return badSource("a grading table's domain is nought to one");
            }
            continue;
        }
        if (kWords[0] == "LUT_1D_SIZE") {
            return badSource("a grading table is three-dimensional");
        }
        // An entry: red, green, and blue, red fastest, then green, then
        // blue, as a volume's texels run along x, y, and its slices.
        if (side == 0 || kWords.size() != 3 || entries == std::size_t{side} * side * side) {
            return badSource("a grading table's entries are three numbers each, as many as its size cubed");
        }
        std::array<std::uint16_t, 4> texel{0, 0, 0, texture::halfOf(1)};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::optional<float> kValue = floatOf(kWords[channel]);
            if (!kValue.has_value()) {
                return badSource("a grading table's entry is not three finite numbers");
            }
            texel[channel] = texture::halfOf(*kValue);
        }
        std::byte* written = table.levels[0].bytes.data() + (entries * 8);
        for (const std::uint16_t kHalf : texel) {
            *written++ = static_cast<std::byte>(kHalf & 0xFFU);
            *written++ = static_cast<std::byte>(kHalf >> 8U);
        }
        ++entries;
    }
    if (side == 0 || entries != std::size_t{side} * side * side) {
        return badSource("a grading table without its size or all its entries");
    }
    return table;
}

} // namespace rawframe::texture_import
