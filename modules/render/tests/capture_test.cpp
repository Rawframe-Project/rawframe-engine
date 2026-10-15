// A picture streamed to an editor (D789): written and read back whole, and
// bytes that are not one whole picture, as a reader that came while it was
// written may find, read as none.

#include "rawframe/render/capture.h"
#include "rawframe/test/test.h"

using namespace rawframe;

RAWFRAME_TEST(AStreamedPictureReadsBackWhole) {
    const render::StreamedPicture kPicture{
        .sequence = 7, .width = 2, .height = 1, .pixels = std::vector<std::byte>(8, std::byte{0x40})};
    const std::vector<std::byte> kBytes = render::streamedOf(kPicture);
    const std::optional<render::StreamedPicture> kRead = render::streamedFrom(kBytes);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->sequence == 7 && kRead->width == 2 && kRead->height == 1 &&
                    kRead->pixels == kPicture.pixels);
}

RAWFRAME_TEST(PartOfAStreamedPictureIsNone) {
    const std::vector<std::byte> kBytes = render::streamedOf(
        {.sequence = 1, .width = 4, .height = 4, .pixels = std::vector<std::byte>(64, std::byte{1})});
    RAWFRAME_EXPECT(!render::streamedFrom(std::span{kBytes}.first(kBytes.size() - 1)).has_value());
    RAWFRAME_EXPECT(!render::streamedFrom(std::span{kBytes}.first(10)).has_value());
    std::vector<std::byte> marked = kBytes;
    marked[0] = std::byte{'x'};
    RAWFRAME_EXPECT(!render::streamedFrom(marked).has_value());
}
