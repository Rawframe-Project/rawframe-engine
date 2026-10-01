// Grading tables (D344): a `.cube` file decodes into a volume whose texel
// at red, green, and blue is the table's entry there, red fastest; titles,
// comments, a nought-to-one domain, and Windows line ends are taken; a
// one-dimensional table, another domain, a missing or surplus entry, a
// number that is not one, and a size past the limit are refused.

#include "rawframe/test/test.h"
#include "rawframe/texture/errors.h"
#include "rawframe/texture_import/import.h"

#include <span>
#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::texture_import;

namespace {

result::Result<texture::Texture> decoded(std::string_view text, const GradingLimits& limits = {}) {
    return decodeGrading(std::as_bytes(std::span{text.data(), text.size()}), limits);
}

bool refusedWith(const result::Result<texture::Texture>& made, texture::TextureError error) {
    return !made.has_value() && made.error().domain() == texture::kTextureDomain && made.error().code() == code(error);
}

/// A texel's channel, as a float.
float channelAt(const texture::Texture& table, std::size_t texel, std::size_t channel) {
    const std::size_t kAt = (texel * 8) + (channel * 2);
    const auto kLow = std::to_integer<std::uint16_t>(table.levels[0].bytes[kAt]);
    const auto kHigh = std::to_integer<std::uint16_t>(table.levels[0].bytes[kAt + 1]);
    return texture::floatOf(static_cast<std::uint16_t>(kLow | (kHigh << 8U)));
}

/// A table of side two whose entry swaps red and green.
constexpr std::string_view kSwap = "# swaps red and green\r\n"
                                   "TITLE \"swap\"\r\n"
                                   "LUT_3D_SIZE 2\r\n"
                                   "DOMAIN_MIN 0 0 0\r\n"
                                   "DOMAIN_MAX 1.0 1.0 1.0\r\n"
                                   "\r\n"
                                   "0 0 0\r\n"
                                   "0 1 0\r\n"
                                   "1 0 0\r\n"
                                   "1 1 0\r\n"
                                   "0 0 1\r\n"
                                   "0 1 1\r\n"
                                   "1 0 1\r\n"
                                   "1 1 1\r\n";

} // namespace

RAWFRAME_TEST(AGradingTableDecodesIntoAVolumeRedFastest) {
    const auto kTable = decoded(kSwap);
    RAWFRAME_EXPECT(kTable.has_value());
    if (!kTable.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kTable->format == texture::Format::Rgba16Float && kTable->depth == 2 && kTable->faces == 1 &&
                    kTable->levels.size() == 1 && kTable->levels[0].width == 2 && kTable->levels[0].height == 2 &&
                    texture::validate(*kTable).has_value());
    // Red one, green and blue nought (texel 1) holds green; green one (texel
    // 2) holds red; every alpha is one.
    RAWFRAME_EXPECT(channelAt(*kTable, 1, 0) == 0 && channelAt(*kTable, 1, 1) == 1 && channelAt(*kTable, 2, 0) == 1 &&
                    channelAt(*kTable, 2, 1) == 0 && channelAt(*kTable, 5, 2) == 1 && channelAt(*kTable, 7, 3) == 1);
}

RAWFRAME_TEST(AGradingTableNotAsTheFormatHasItIsRefused) {
    const std::string kEntries = "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    RAWFRAME_EXPECT(decoded("LUT_3D_SIZE 2\n" + kEntries).has_value());
    for (const std::string& kBroken : {"LUT_1D_SIZE 2\n" + kEntries,
                                       "LUT_3D_SIZE 2\nDOMAIN_MIN -1 0 0\n" + kEntries,
                                       std::string{"LUT_3D_SIZE 1\n0 0 0\n"},
                                       "LUT_3D_SIZE 2\n" + kEntries.substr(6),
                                       "LUT_3D_SIZE 2\n" + kEntries + "1 1 1\n",
                                       "LUT_3D_SIZE 2\n0 0 x\n" + kEntries.substr(6),
                                       "LUT_3D_SIZE 2\n0 0\n" + kEntries.substr(6),
                                       "LUT_3D_SIZE 2\nLUT_3D_SIZE 2\n" + kEntries,
                                       "LUT_3D_SIZE 2\n0 0 0\nTITLE \"late\"\n" + kEntries.substr(6),
                                       "LUT_3D_SIZE 2\n0 0 inf\n" + kEntries.substr(6),
                                       kEntries,
                                       std::string{}}) {
        RAWFRAME_EXPECT(refusedWith(decoded(kBroken), texture::TextureError::BadSource));
    }
    RAWFRAME_EXPECT(refusedWith(decoded("LUT_3D_SIZE 66\n"), texture::TextureError::OverLimit));
    RAWFRAME_EXPECT(
        refusedWith(decoded("LUT_3D_SIZE 2\n" + kEntries, {.maximumSide = 1}), texture::TextureError::OverLimit));
}
