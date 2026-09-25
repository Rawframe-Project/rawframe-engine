#include "rawframe/content/identity.h"

#include <array>

namespace rawframe::content {

namespace {

constexpr std::size_t kLongestRepresentation = 64;
constexpr std::string_view kSha256Prefix = "sha256:";
constexpr std::string_view kHex = "0123456789abcdef";

} // namespace

std::optional<RepresentationId> RepresentationId::parse(std::string_view text) {
    if (text.empty() || text.size() > kLongestRepresentation) {
        return std::nullopt;
    }
    std::size_t words = 1;
    bool wordStart = true;
    for (const char kCharacter : text) {
        if (kCharacter == '.') {
            if (wordStart) {
                return std::nullopt;
            }
            ++words;
            wordStart = true;
            continue;
        }
        const bool kLetter = kCharacter >= 'a' && kCharacter <= 'z';
        const bool kDigit = kCharacter >= '0' && kCharacter <= '9';
        if (!kLetter && !(kDigit && !wordStart)) {
            return std::nullopt;
        }
        wordStart = false;
    }
    if (wordStart || words < 2) {
        return std::nullopt;
    }
    return RepresentationId{std::string{text}};
}

ContentDigest ContentDigest::of(std::span<const std::byte> content) noexcept {
    return ContentDigest{.algorithm = DigestAlgorithm::Sha256, .bytes = base::sha256(content)};
}

std::optional<ContentDigest> ContentDigest::parse(std::string_view text) noexcept {
    if (!text.starts_with(kSha256Prefix) || text.size() != kSha256Prefix.size() + 64) {
        return std::nullopt;
    }
    ContentDigest digest;
    for (std::size_t index = 0; index < 32; ++index) {
        const auto kHigh = kHex.find(text[kSha256Prefix.size() + (index * 2)]);
        const auto kLow = kHex.find(text[kSha256Prefix.size() + (index * 2) + 1]);
        if (kHigh == std::string_view::npos || kLow == std::string_view::npos) {
            return std::nullopt;
        }
        digest.bytes[index] = static_cast<std::byte>((kHigh << 4U) | kLow);
    }
    return digest;
}

std::string ContentDigest::text() const {
    std::string made{kSha256Prefix};
    for (const std::byte kByte : bytes) {
        const auto kValue = std::to_integer<unsigned>(kByte);
        made.push_back(kHex[kValue >> 4U]);
        made.push_back(kHex[kValue & 0xFU]);
    }
    return made;
}

bool sameDigest(const ContentDigest& left, const ContentDigest& right) noexcept {
    unsigned difference = left.algorithm == right.algorithm ? 0U : 1U;
    for (std::size_t index = 0; index < left.bytes.size(); ++index) {
        difference |= std::to_integer<unsigned>(left.bytes[index] ^ right.bytes[index]);
    }
    return difference == 0;
}

} // namespace rawframe::content
