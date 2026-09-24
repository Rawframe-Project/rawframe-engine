#include "rawframe/result/format.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace rawframe::result {

namespace {

/// Appends into a fixed destination and remembers whether anything did not fit.
/// Truncation may cut a multi-byte character: the text is for people, and a
/// partial last character is better than a missing line.
class Writer {
public:
    explicit Writer(std::span<char> destination) noexcept : destination_(destination) {
    }

    void text(std::string_view part) noexcept {
        const std::size_t kSpace = destination_.size() - written_;
        const std::size_t kCount = std::min(part.size(), kSpace);
        std::copy_n(part.data(), kCount, destination_.data() + written_);
        written_ += kCount;
        truncated_ = truncated_ || kCount < part.size();
    }

    void decimal(std::uint64_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + static_cast<int>(value % 10));
            value /= 10;
        } while (value != 0);
        while (count > 0) {
            --count;
            text(std::string_view{&digits[count], 1});
        }
    }

    [[nodiscard]] FormatResult result() const noexcept {
        return FormatResult{written_, truncated_};
    }

private:
    std::span<char> destination_;
    std::size_t written_ = 0;
    bool truncated_ = false;
};

constexpr std::string_view kElided = "...";

void location(Writer& out, std::source_location where) noexcept {
    const std::string_view kFile = where.file_name();
    if (kFile.size() <= kMaximumFormattedFileBytes) {
        out.text(kFile);
    } else {
        out.text(kElided);
        out.text(kFile.substr(kFile.size() - (kMaximumFormattedFileBytes - kElided.size())));
    }
    out.text(":");
    out.decimal(where.line());
}

void one(Writer& out, const Error& error) noexcept {
    out.text(describe(error.errorClass()));
    out.text(" (domain ");
    std::array<char, base::kBits128HexDigits> domain{};
    base::formatBits128Hex(error.domain().value, domain);
    out.text(std::string_view{domain.data(), domain.size()});
    out.text(", code ");
    out.decimal(error.code().value);
    out.text("): ");
    out.text(error.description());
    if (error.descriptionTruncated()) {
        out.text(" [truncated]");
    }
    out.text("\n  at ");
    location(out, error.origin());
    for (const Frame& frame : error.frames()) {
        out.text("\n  frame: ");
        out.text(frame.description);
        out.text(" at ");
        location(out, frame.location);
    }
    if (error.framesDropped() != 0) {
        out.text("\n  (");
        out.decimal(error.framesDropped());
        out.text(" frames dropped)");
    }
    for (const ContextField& field : error.context()) {
        out.text("\n  ");
        out.text(field.key);
        out.text("=");
        out.text(field.value);
    }
    if (error.contextFieldsDropped() != 0) {
        out.text("\n  (");
        out.decimal(error.contextFieldsDropped());
        out.text(" context fields dropped)");
    }
    if (error.contextTruncated()) {
        out.text("\n  (context truncated)");
    }
}

// The worst case, piece by piece, mirroring `one` and `formatError` below: the
// longest class name, every bound full, every marker present.
constexpr std::size_t kDecimalDigits = 20;
constexpr std::size_t kLocationBytes = kMaximumFormattedFileBytes + 1 + kDecimalDigits;
constexpr std::size_t kOneErrorBytes =
    19 + 9 + base::kBits128HexDigits + 7 + kDecimalDigits + 3 + kMaximumDescriptionBytes + 12 // header line
    + 6 + kLocationBytes                                                                      // origin
    + kMaximumFrames * (10 + kMaximumFrameDescriptionBytes + 4 + kLocationBytes)              // frames
    + 4 + kDecimalDigits + 16                                                                 // frames dropped
    + kMaximumContextFields * (3 + kMaximumContextKeyBytes + 1 + kMaximumContextValueBytes)   // context
    + 4 + kDecimalDigits + 24                                                                 // fields dropped
    + 22;                                                                                     // truncated
constexpr std::size_t kWorstCaseBytes = (kMaximumCauseDepth + 1) * kOneErrorBytes + kMaximumCauseDepth * 12 + 31;
static_assert(kWorstCaseBytes <= kMaximumFormattedBytes, "kMaximumFormattedBytes no longer covers the worst case");

} // namespace

FormatResult formatError(const Error& error, std::span<char> destination) noexcept {
    Writer out{destination};
    one(out, error);
    for (const Error* cause = error.cause(); cause != nullptr; cause = cause->cause()) {
        out.text("\ncaused by: ");
        one(out, *cause);
    }
    // The drop is recorded on the error whose cause was dropped, which is the
    // last one printed.
    const Error* last = &error;
    while (last->cause() != nullptr) {
        last = last->cause();
    }
    if (last->causeDropped()) {
        out.text("\n(cause dropped at depth limit)");
    }
    return out.result();
}

} // namespace rawframe::result
