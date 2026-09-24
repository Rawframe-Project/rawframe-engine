#pragma once

#include <cstddef>
#include <string_view>

namespace rawframe::diagnostics {

/// The longest domain, code, or field key (SPEC-0047 kMaximumIdentifierBytes).
inline constexpr std::size_t kMaximumIdentifierBytes = 64;

namespace detail {

// Called only when a compile-time check fails. It is not constexpr, so reaching
// it during constant evaluation is the compile error that names the mistake.
void identityMustBeLowerSnakeCase() noexcept;
void fieldKeyMustBeLowerCamelCase() noexcept;

[[nodiscard]] constexpr bool isLowerSnakeCase(std::string_view text) noexcept {
    if (text.empty() || text.size() > kMaximumIdentifierBytes || text.front() < 'a' || text.front() > 'z') {
        return false;
    }
    for (const char kCharacter : text) {
        const bool kLower = kCharacter >= 'a' && kCharacter <= 'z';
        const bool kDigit = kCharacter >= '0' && kCharacter <= '9';
        if (!kLower && !kDigit && kCharacter != '_') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool isLowerCamelCase(std::string_view text) noexcept {
    if (text.empty() || text.size() > kMaximumIdentifierBytes || text.front() < 'a' || text.front() > 'z') {
        return false;
    }
    for (const char kCharacter : text) {
        const bool kLetter = (kCharacter >= 'a' && kCharacter <= 'z') || (kCharacter >= 'A' && kCharacter <= 'Z');
        const bool kDigit = kCharacter >= '0' && kCharacter <= '9';
        if (!kLetter && !kDigit) {
            return false;
        }
    }
    return true;
}

} // namespace detail

/// What an event is: an owner-scoped domain and a code within it, both
/// lower_snake_case. Constructible only from constants, so text from untrusted
/// input can never become an identity (SPEC-0004).
struct EventIdentity {
    std::string_view domain;
    std::string_view code;

    consteval EventIdentity(std::string_view domainText, std::string_view codeText) noexcept
        : domain(domainText), code(codeText) {
        if (!detail::isLowerSnakeCase(domainText) || !detail::isLowerSnakeCase(codeText)) {
            detail::identityMustBeLowerSnakeCase();
        }
    }
};

/// A structured field's key: a lowerCamelCase constant, never built at run time.
struct FieldKey {
    std::string_view text;

    consteval FieldKey(const char* keyText) noexcept : text(keyText) {
        if (!detail::isLowerCamelCase(text)) {
            detail::fieldKeyMustBeLowerCamelCase();
        }
    }
};

} // namespace rawframe::diagnostics
