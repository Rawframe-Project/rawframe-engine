#include "rawframe/composition/configuration.h"

#include "rawframe/composition/errors.h"

#include <charconv>

namespace rawframe::composition {

namespace {

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool validKey(std::string_view key) noexcept {
    if (key.empty() || key.size() > kMaximumConfigurationKeyBytes || key.front() < 'a' || key.front() > 'z') {
        return false;
    }
    for (const char kCharacter : key) {
        const bool kAllowed = (kCharacter >= 'a' && kCharacter <= 'z') || (kCharacter >= '0' && kCharacter <= '9') ||
                              kCharacter == '_' || kCharacter == '.';
        if (!kAllowed) {
            return false;
        }
    }
    return true;
}

std::unexpected<result::Error> invalid(std::string_view description, std::string_view key) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                       kCompositionDomain,
                                                       code(CompositionError::BadConfiguration),
                                                       description)
                                              .error()
                                              .withContext("key", key)};
}

} // namespace

result::Result<Configuration> Configuration::parse(std::string_view text) {
    Configuration configuration;
    while (!text.empty()) {
        const std::size_t kEnd = text.find('\n');
        const std::string_view kLine = trim(text.substr(0, kEnd));
        text.remove_prefix(kEnd == std::string_view::npos ? text.size() : kEnd + 1);
        if (kLine.empty() || kLine.front() == '#') {
            continue;
        }
        const std::size_t kEquals = kLine.find('=');
        if (kEquals == std::string_view::npos) {
            return invalid("a configuration line has no `=`", kLine.substr(0, kMaximumConfigurationKeyBytes));
        }
        const std::string_view kKey = trim(kLine.substr(0, kEquals));
        const std::string_view kValue = trim(kLine.substr(kEquals + 1));
        if (!validKey(kKey)) {
            return invalid("a configuration key is not dotted lower_snake_case within its bound",
                           kKey.substr(0, kMaximumConfigurationKeyBytes));
        }
        if (kValue.size() > kMaximumConfigurationValueBytes) {
            return invalid("a configuration value is longer than its bound", kKey);
        }
        if (configuration.entries_.size() == kMaximumConfigurationEntries) {
            return invalid("the configuration has more entries than its bound", kKey);
        }
        const auto [entry, kAdded] = configuration.entries_.try_emplace(std::string{kKey});
        if (!kAdded) {
            return invalid("a configuration key appears twice", kKey);
        }
        entry->second.value = kValue;
    }
    return configuration;
}

result::Result<Configuration> Configuration::parse(std::string_view text, std::string_view base) {
    RAWFRAME_TRY_ASSIGN(Configuration configuration, parse(text));
    while (base.size() > 1 && (base.back() == '/' || base.back() == '\\')) {
        base.remove_suffix(1);
    }
    configuration.base_ = base;
    return configuration;
}

std::optional<std::string> Configuration::path(std::string_view key) const {
    const auto kValue = text(key);
    if (!kValue) {
        return std::nullopt;
    }
    // Absolute: from the root, a drive, or a share.
    const bool kAbsolute =
        kValue->starts_with('/') || kValue->starts_with('\\') || (kValue->size() >= 2 && (*kValue)[1] == ':');
    if (kAbsolute || base_.empty() || kValue->empty()) {
        return std::string{*kValue};
    }
    return base_ + "/" + std::string{*kValue};
}

std::optional<std::string_view> Configuration::text(std::string_view key) const {
    const auto kFound = entries_.find(key);
    if (kFound == entries_.end()) {
        return std::nullopt;
    }
    kFound->second.read.store(true, std::memory_order_relaxed);
    return std::string_view{kFound->second.value};
}

std::vector<std::string_view> Configuration::unread() const {
    std::vector<std::string_view> keys;
    for (const auto& [kKey, kEntry] : entries_) {
        if (!kEntry.read.load(std::memory_order_relaxed)) {
            keys.emplace_back(kKey);
        }
    }
    return keys;
}

result::Result<std::uint64_t> Configuration::unsignedInteger(std::string_view key, std::uint64_t fallback) const {
    const auto kValue = text(key);
    if (!kValue) {
        return fallback;
    }
    std::uint64_t parsed = 0;
    const auto kResult = std::from_chars(kValue->data(), kValue->data() + kValue->size(), parsed);
    if (kResult.ec != std::errc{} || kResult.ptr != kValue->data() + kValue->size()) {
        return invalid("a configuration value is not an unsigned integer", key);
    }
    return parsed;
}

} // namespace rawframe::composition
