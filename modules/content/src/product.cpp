#include "rawframe/content/product.h"

#include <algorithm>

namespace rawframe::content {

namespace {

bool semverIdentifier(std::string_view part, bool numericNeedsNoLeadingZero) noexcept {
    if (part.empty() || !std::ranges::all_of(part, [](char each) {
            return (each >= '0' && each <= '9') || (each >= 'a' && each <= 'z') || (each >= 'A' && each <= 'Z') ||
                   each == '-';
        })) {
        return false;
    }
    const bool kNumeric = std::ranges::all_of(part, [](char each) {
        return each >= '0' && each <= '9';
    });
    return !numericNeedsNoLeadingZero || !kNumeric || part.size() == 1 || part.front() != '0';
}

bool dotted(std::string_view text, bool numericNeedsNoLeadingZero) noexcept {
    while (true) {
        const std::size_t kDot = text.find('.');
        if (!semverIdentifier(text.substr(0, kDot), numericNeedsNoLeadingZero)) {
            return false;
        }
        if (kDot == std::string_view::npos) {
            return true;
        }
        text.remove_prefix(kDot + 1);
    }
}

} // namespace

bool validVersion(std::string_view text) noexcept {
    if (text.empty() || text.size() > 64) {
        return false;
    }
    std::string_view core = text;
    const std::size_t kPlus = core.find('+');
    if (kPlus != std::string_view::npos) {
        if (!dotted(core.substr(kPlus + 1), false)) {
            return false;
        }
        core = core.substr(0, kPlus);
    }
    const std::size_t kDash = core.find('-');
    if (kDash != std::string_view::npos) {
        if (!dotted(core.substr(kDash + 1), true)) {
            return false;
        }
        core = core.substr(0, kDash);
    }
    int numbers = 0;
    while (true) {
        const std::size_t kDot = core.find('.');
        const std::string_view kNumber = core.substr(0, kDot);
        if (kNumber.empty() ||
            !std::ranges::all_of(kNumber,
                                 [](char each) {
                                     return each >= '0' && each <= '9';
                                 }) ||
            (kNumber.size() > 1 && kNumber.front() == '0')) {
            return false;
        }
        ++numbers;
        if (kDot == std::string_view::npos) {
            break;
        }
        core.remove_prefix(kDot + 1);
    }
    return numbers == 3;
}

bool validSubject(std::string_view text) noexcept {
    const std::size_t kSlash = text.find('/');
    if (kSlash == std::string_view::npos) {
        return false;
    }
    const auto kSegment = [](std::string_view segment) {
        return !segment.empty() && segment.front() != '-' && segment.back() != '-' &&
               std::ranges::all_of(segment, [](char each) {
                   return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '-';
               });
    };
    return kSegment(text.substr(0, kSlash)) && kSegment(text.substr(kSlash + 1));
}

std::string_view publisherOf(std::string_view subject) noexcept {
    return subject.substr(0, subject.find('/'));
}

} // namespace rawframe::content
