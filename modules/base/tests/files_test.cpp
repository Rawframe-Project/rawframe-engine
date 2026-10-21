// A file replaced whole: written where there was none, then replaced, with
// nothing left beside it; and refused where its directory is missing. A
// file read whole, past its blocks, and none for one that is not there. A
// file moved over one a reader keeps reading, every time.

#include "rawframe/base/files.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace rawframe;

namespace {

std::string contentsOf(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> bytes(text.size());
    for (std::size_t at = 0; at < text.size(); ++at) {
        bytes[at] = static_cast<std::byte>(text[at]);
    }
    return bytes;
}

} // namespace

RAWFRAME_TEST(AFileIsReplacedWhole) {
    const std::filesystem::path kDirectory = test::scratchDirectory("files");
    const std::filesystem::path kPath = kDirectory / "kept.json";
    std::filesystem::create_directories(kDirectory);
    RAWFRAME_EXPECT(base::replaceFile(kPath, bytesOf("first and longer")));
    RAWFRAME_EXPECT(contentsOf(kPath) == "first and longer");
    RAWFRAME_EXPECT(base::replaceFile(kPath, bytesOf("second")));
    RAWFRAME_EXPECT(contentsOf(kPath) == "second");
    RAWFRAME_EXPECT(!std::filesystem::exists(kDirectory / "kept.json.partial"));
    // Nothing at all is a file too.
    RAWFRAME_EXPECT(base::replaceFile(kPath, {}));
    RAWFRAME_EXPECT(std::filesystem::file_size(kPath) == 0);
    // A directory that is not there is not made.
    RAWFRAME_EXPECT(!base::replaceFile(kDirectory / "absent" / "kept.json", bytesOf("none")));
    RAWFRAME_EXPECT(!std::filesystem::exists(kDirectory / "absent"));
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(AFileIsReadWhole) {
    const std::filesystem::path kDirectory = test::scratchDirectory("files_read");
    std::filesystem::create_directories(kDirectory);
    // Three blocks and seven bytes, every byte value in it, a picture's size.
    std::string written((std::size_t{3} << 20U) + 7, '\0');
    for (std::size_t at = 0; at < written.size(); ++at) {
        written[at] = static_cast<char>((at * 31U) % 251U);
    }
    RAWFRAME_EXPECT(
        base::writeWholeFile(kDirectory / "picture.bin", std::as_bytes(std::span{written.data(), written.size()})));
    const std::optional<std::string> kRead = base::readWholeFile(kDirectory / "picture.bin");
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == written);
    // Nothing at all is read as nothing, and a file not there as none.
    RAWFRAME_EXPECT(base::writeWholeFile(kDirectory / "empty.bin", {}));
    RAWFRAME_EXPECT(base::readWholeFile(kDirectory / "empty.bin") == std::optional<std::string>{std::string{}});
    RAWFRAME_EXPECT(!base::readWholeFile(kDirectory / "absent.bin").has_value());
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(AFileIsMovedOverOneBeingRead) {
    // A picture streamed a frame at a time while an editor reads it: on
    // Windows a classic move was refused while the reader had it open, and
    // the frame was lost (D965). Read without a pause meanwhile, every move
    // goes through, and every read is one whole picture.
    const std::filesystem::path kDirectory = test::scratchDirectory("files_moved");
    std::filesystem::create_directories(kDirectory);
    const std::filesystem::path kPicture = kDirectory / "picture";
    const std::string kFirst(std::size_t{1} << 20U, 'a');
    const std::string kSecond(std::size_t{1} << 20U, 'b');
    RAWFRAME_EXPECT(base::writeWholeFile(kPicture, std::as_bytes(std::span{kFirst.data(), kFirst.size()})));
    std::atomic<bool> moving{true};
    std::atomic<int> reads{0};
    std::atomic<int> broken{0};
    std::thread reader{[&] {
        while (moving.load()) {
            const std::optional<std::string> kRead = base::readWholeFile(kPicture);
            if (kRead.has_value()) {
                ++reads;
                if (*kRead != kFirst && *kRead != kSecond) {
                    ++broken;
                }
            }
        }
    }};
    int moved = 0;
    for (int frame = 0; frame < 100; ++frame) {
        const std::string& kNext = frame % 2 == 0 ? kSecond : kFirst;
        RAWFRAME_EXPECT(
            base::writeWholeFile(kDirectory / "picture.writing", std::as_bytes(std::span{kNext.data(), kNext.size()})));
        moved += base::moveOver(kDirectory / "picture.writing", kPicture) ? 1 : 0;
    }
    moving = false;
    reader.join();
    RAWFRAME_EXPECT(moved == 100);
    RAWFRAME_EXPECT(reads.load() > 0 && broken.load() == 0);
    RAWFRAME_EXPECT(contentsOf(kPicture) == kFirst && !std::filesystem::exists(kDirectory / "picture.writing"));
    std::filesystem::remove_all(kDirectory);
}

#if defined(__unix__) || defined(__APPLE__)
RAWFRAME_TEST(AFileIsNeverWrittenThroughALink) {
    // A directory from elsewhere, a game's, may hold links to files outside
    // it where a write would go (D847): the file they name is kept.
    const std::filesystem::path kDirectory = test::scratchDirectory("files_linked");
    std::filesystem::create_directories(kDirectory);
    const std::filesystem::path kElsewhere = kDirectory / "elsewhere.txt";
    RAWFRAME_EXPECT(base::writeWholeFile(kElsewhere, bytesOf("kept")));
    // Where the partial file goes: taken away, the file put whole.
    std::filesystem::create_symlink(kElsewhere, kDirectory / "level.scene.partial");
    RAWFRAME_EXPECT(base::replaceFile(kDirectory / "level.scene", bytesOf("written")));
    RAWFRAME_EXPECT(contentsOf(kDirectory / "level.scene") == "written" && contentsOf(kElsewhere) == "kept");
    // At the file itself: replaced, not followed.
    std::filesystem::create_symlink(kElsewhere, kDirectory / "other.scene");
    RAWFRAME_EXPECT(base::replaceFile(kDirectory / "other.scene", bytesOf("written")));
    RAWFRAME_EXPECT(!std::filesystem::is_symlink(kDirectory / "other.scene") && contentsOf(kElsewhere) == "kept");
    // Written whole in place: refused.
    std::filesystem::create_symlink(kElsewhere, kDirectory / "linked.scene");
    RAWFRAME_EXPECT(!base::writeWholeFile(kDirectory / "linked.scene", bytesOf("written")));
    RAWFRAME_EXPECT(contentsOf(kElsewhere) == "kept");
    std::filesystem::remove_all(kDirectory);
}
#endif
