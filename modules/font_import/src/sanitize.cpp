#include "rawframe/font_import/sanitize.h"

#include "rawframe/font_import/errors.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
// GCC sees OTSStream's checksum read four bytes of a one-byte write, which
// it never does past the bytes written.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
#include <opentype-sanitiser.h>
#include <ots-memory-stream.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <string>
#include <string_view>

namespace rawframe::font_import {

namespace {

std::unexpected<result::Error> refuse(FontImportError error, std::string_view why) {
    const result::ErrorClass kClass = error == FontImportError::OverLimit ? result::ErrorClass::ResourceExhausted
                                                                          : result::ErrorClass::InvalidArgument;
    return std::unexpected<result::Error>{result::fail(kClass, kFontImportDomain, code(error), why).error()};
}

/// OTS's context, keeping the first failure it reports (level 0); its
/// warnings say what it dropped, which a cook does not need.
class Context final : public ots::OTSContext {
public:
    void Message(int level, const char* format, ...) override {
        if (level != 0 || !first_.empty()) {
            return;
        }
        std::array<char, 256> line{};
        std::va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(line.data(), line.size(), format, arguments);
        va_end(arguments);
        first_ = line.data();
    }

    [[nodiscard]] const std::string& first() const noexcept {
        return first_;
    }

private:
    std::string first_;
};

bool tagged(std::span<const std::byte> source, std::string_view tag) noexcept {
    return source.size() >= 4 && std::memcmp(source.data(), tag.data(), 4) == 0;
}

} // namespace

result::Result<std::vector<std::byte>> sanitize(std::span<const std::byte> source, const SanitizeLimits& limits) {
    if (source.size() > limits.maximumBytes) {
        return refuse(FontImportError::OverLimit, "a font is past the limit");
    }
    if (tagged(source, "wOFF") || tagged(source, "wOF2")) {
        return refuse(FontImportError::Unsupported, "a web font (WOFF): only TrueType and OpenType are taken");
    }
    // A collection, CFF, or TrueType ("true" is Apple's TrueType tag).
    static constexpr std::string_view kTrueType{"\x00\x01\x00\x00", 4};
    if (!tagged(source, kTrueType) && !tagged(source, "OTTO") && !tagged(source, "true") && !tagged(source, "ttcf")) {
        return refuse(FontImportError::BadFont, "not a TrueType or OpenType font or collection");
    }
    Context context;
    ots::ExpandingMemoryStream output(source.size(), limits.maximumBytes);
    if (!context.Process(&output, reinterpret_cast<const std::uint8_t*>(source.data()), source.size())) {
        const std::string kWhy = context.first().empty() ? std::string{"the font sanitizer refused it"}
                                                         : "the font sanitizer refused it: " + context.first();
        return refuse(static_cast<std::size_t>(output.Tell()) >= limits.maximumBytes ? FontImportError::OverLimit
                                                                                     : FontImportError::BadFont,
                      kWhy);
    }
    const auto* kWritten = static_cast<const std::byte*>(output.get());
    return std::vector<std::byte>(kWritten, kWritten + static_cast<std::size_t>(output.Tell()));
}

} // namespace rawframe::font_import
