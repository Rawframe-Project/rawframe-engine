// The cook's font sanitizer (ADR-0078, D385): a font rebuilt the same each
// time and taken again as it was written, a table OTS does not know dropped,
// web fonts and bytes that are not a font refused, a font cut short or
// damaged refused with what OTS found, and a font past the limit refused.

#include "rawframe/font_import/errors.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/test/files.h"
#include "rawframe/test/test.h"

#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::font_import;

namespace {

std::span<const std::byte> bytesOf(const std::string& text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

std::uint32_t big32(std::span<const std::byte> bytes, std::size_t at) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[at + index]);
    }
    return value;
}

/// Whether the font's table directory names `tag`.
bool hasTable(std::span<const std::byte> font, const char* tag) noexcept {
    const std::uint32_t kTables =
        (std::to_integer<std::uint32_t>(font[4]) << 8U) | std::to_integer<std::uint32_t>(font[5]);
    for (std::uint32_t table = 0; table < kTables; ++table) {
        if (std::memcmp(font.data() + 12 + (16 * table), tag, 4) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

RAWFRAME_TEST(AFontIsRebuiltTheSameAndTakenAgain) {
    const std::string kAhem = test::readFile(RAWFRAME_FONT_IMPORT_FONTS "Ahem.ttf");
    const auto kRebuilt = sanitize(bytesOf(kAhem));
    RAWFRAME_EXPECT(kRebuilt.has_value());
    if (!kRebuilt.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(big32(*kRebuilt, 0) == 0x00010000U);
    RAWFRAME_EXPECT(hasTable(*kRebuilt, "glyf") && hasTable(*kRebuilt, "cmap") && hasTable(*kRebuilt, "hmtx"));
    const auto kAgain = sanitize(bytesOf(kAhem));
    RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == *kRebuilt);
    // What it wrote it takes again, unchanged.
    const auto kTwice = sanitize(*kRebuilt);
    RAWFRAME_EXPECT(kTwice.has_value() && *kTwice == *kRebuilt);
}

RAWFRAME_TEST(ATableOtsDoesNotKnowIsDropped) {
    std::string ahem = test::readFile(RAWFRAME_FONT_IMPORT_FONTS "Ahem.ttf");
    // Renamed in the directory, the gasp table is one OTS has never heard of.
    bool renamed = false;
    const std::uint32_t kTables = (static_cast<std::uint8_t>(ahem[4]) << 8U) | static_cast<std::uint8_t>(ahem[5]);
    for (std::uint32_t table = 0; table < kTables; ++table) {
        if (ahem.compare(12 + (16 * table), 4, "gasp") == 0) {
            ahem.replace(12 + (16 * table), 4, "zzzz");
            renamed = true;
        }
    }
    RAWFRAME_EXPECT(renamed);
    const auto kRebuilt = sanitize(bytesOf(ahem));
    RAWFRAME_EXPECT(kRebuilt.has_value());
    if (kRebuilt.has_value()) {
        RAWFRAME_EXPECT(!hasTable(*kRebuilt, "zzzz") && hasTable(*kRebuilt, "glyf"));
    }
}

RAWFRAME_TEST(WebFontsAndWhatIsNotAFontAreRefused) {
    const std::string kAhem = test::readFile(RAWFRAME_FONT_IMPORT_FONTS "Ahem.ttf");
    for (const char* kTag : {"wOFF", "wOF2"}) {
        std::string web = kAhem;
        web.replace(0, 4, kTag);
        const auto kRefused = sanitize(bytesOf(web));
        RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().code() == code(FontImportError::Unsupported));
    }
    const auto kText = sanitize(bytesOf(std::string(64, 'x')));
    RAWFRAME_EXPECT(!kText.has_value() && kText.error().code() == code(FontImportError::BadFont));
    RAWFRAME_EXPECT(!sanitize({}).has_value());
    // Cut short, and with its glyph table's place pointing past its end.
    const auto kCut = sanitize(bytesOf(kAhem.substr(0, kAhem.size() / 2)));
    RAWFRAME_EXPECT(!kCut.has_value() && kCut.error().code() == code(FontImportError::BadFont));
    std::string damaged = kAhem;
    const std::uint32_t kTables = (static_cast<std::uint8_t>(damaged[4]) << 8U) | static_cast<std::uint8_t>(damaged[5]);
    for (std::uint32_t table = 0; table < kTables; ++table) {
        if (damaged.compare(12 + (16 * table), 4, "loca") == 0) {
            damaged.replace(12 + (16 * table) + 8, 4, std::string{"\x7f\xff\xff\x00", 4});
        }
    }
    const auto kDamaged = sanitize(bytesOf(damaged));
    RAWFRAME_EXPECT(!kDamaged.has_value() && kDamaged.error().code() == code(FontImportError::BadFont));
    if (!kDamaged.has_value()) {
        RAWFRAME_EXPECT(kDamaged.error().description().find("sanitizer refused it") != std::string_view::npos);
    }
}

RAWFRAME_TEST(AFontPastTheLimitIsRefused) {
    const std::string kAhem = test::readFile(RAWFRAME_FONT_IMPORT_FONTS "Ahem.ttf");
    const auto kRefused = sanitize(bytesOf(kAhem), {.maximumBytes = kAhem.size() - 1});
    RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().code() == code(FontImportError::OverLimit));
}
