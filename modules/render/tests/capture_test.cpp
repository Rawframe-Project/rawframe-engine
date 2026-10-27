// A picture streamed to an editor: written and read back whole, and
// bytes that are not one whole picture, as a reader that came while it was
// written may find, read as none.

#include "rawframe/render/capture.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

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

#if RAWFRAME_FILE_SYSTEM
RAWFRAME_TEST(AStreamWrittenInPlaceIsReadOnePictureAtATimeWhole) {
    // A reader without a pause beside a writer: each picture it
    // takes whole, every pixel the one its sequence wrote, the sequences
    // rising; and a picture past the file's room put in a larger file the
    // reader follows. The writer waits for the reader to take one of the
    // first file's pictures before it writes the larger ones, so however
    // the two threads are scheduled the reader takes one from each file.
    const std::filesystem::path kDirectory = test::scratchDirectory("picture_stream");
    std::filesystem::create_directories(kDirectory);
    const std::filesystem::path kPath = kDirectory / "picture";
    render::PictureStreamWriter writer{kPath};
    render::PictureStreamReader reader{kPath};
    RAWFRAME_EXPECT(!reader.next().has_value());
    std::atomic<bool> writing{true};
    std::atomic<int> failed{0};
    std::atomic<int> taken{0};
    std::thread written{[&] {
        for (std::uint32_t each = 1; each <= 400; ++each) {
            // Larger after the first half: a second file, put in its place.
            const std::uint32_t kSide = each <= 200 ? 256 : 640;
            // A reader that never takes one fails the test in seconds, not at
            // the check's timeout.
            const auto kGiveUp = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (each == 201 && taken.load() == 0 && std::chrono::steady_clock::now() < kGiveUp) {
                std::this_thread::yield();
            }
            const std::vector<std::byte> kPixels(std::size_t{kSide} * kSide * 4, static_cast<std::byte>(each % 251));
            failed += writer.write(kSide, kSide, kPixels) ? 0 : 1;
        }
        writing = false;
    }};
    int torn = 0;
    std::uint64_t last = 0;
    std::uint32_t widest = 0;
    const auto kTake = [&](const render::StreamedPicture& read) {
        ++taken;
        const auto kOwn = static_cast<std::byte>(read.sequence % 251);
        torn += read.sequence <= last || std::ranges::any_of(read.pixels,
                                                             [kOwn](std::byte pixel) {
                                                                 return pixel != kOwn;
                                                             })
                    ? 1
                    : 0;
        last = read.sequence;
        widest = std::max(widest, read.width);
    };
    while (writing.load()) {
        if (const std::optional<render::StreamedPicture> kRead = reader.next(); kRead.has_value()) {
            kTake(*kRead);
        }
    }
    written.join();
    // And the last one, once the writer is done, unless it was taken.
    if (const std::optional<render::StreamedPicture> kRead = reader.next(); kRead.has_value()) {
        kTake(*kRead);
    }
    RAWFRAME_EXPECT(failed.load() == 0 && writer.written() == 400);
    RAWFRAME_EXPECT(taken.load() > 1 && torn == 0);
    RAWFRAME_EXPECT(last == 400 && widest == 640);
    RAWFRAME_EXPECT(!std::filesystem::exists(kDirectory / "picture.writing"));
    std::filesystem::remove_all(kDirectory);
}
#endif
