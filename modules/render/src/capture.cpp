#include "rawframe/render/capture.h"

#include "rawframe/base/files.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <system_error>

namespace rawframe::render {

std::vector<std::byte> tgaOf(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height) {
    std::vector<std::byte> image(18 + pixels.size());
    image[2] = std::byte{2};
    image[12] = static_cast<std::byte>(width & 0xFFU);
    image[13] = static_cast<std::byte>(width >> 8U);
    image[14] = static_cast<std::byte>(height & 0xFFU);
    image[15] = static_cast<std::byte>(height >> 8U);
    image[16] = std::byte{32};
    // Eight bits of alpha, rows top first.
    image[17] = std::byte{0x28};
    for (std::size_t at = 0; at + 3 < pixels.size(); at += 4) {
        image[18 + at] = pixels[at + 2];
        image[18 + at + 1] = pixels[at + 1];
        image[18 + at + 2] = pixels[at];
        image[18 + at + 3] = pixels[at + 3];
    }
    return image;
}

namespace {

constexpr std::array<char, 8> kStreamMark = {'r', 'f', 'p', 'i', 'c', '1', '\0', '\0'};
constexpr std::size_t kStreamHeader = 8 + 8 + 4 + 4;
constexpr std::uint32_t kStreamSide = 8192;

void put(std::vector<std::byte>& into, std::uint64_t value, int bytes) {
    for (int each = 0; each < bytes; ++each) {
        into.push_back(static_cast<std::byte>((value >> (8U * static_cast<unsigned>(each))) & 0xFFU));
    }
}

std::uint64_t got(std::span<const std::byte> from, std::size_t at, int bytes) {
    std::uint64_t made = 0;
    for (int each = 0; each < bytes; ++each) {
        made |= std::to_integer<std::uint64_t>(from[at + static_cast<std::size_t>(each)])
                << (8U * static_cast<unsigned>(each));
    }
    return made;
}

} // namespace

std::vector<std::byte> streamedOf(const StreamedPicture& picture) {
    std::vector<std::byte> made;
    made.reserve(kStreamHeader + picture.pixels.size());
    for (const char kEach : kStreamMark) {
        made.push_back(static_cast<std::byte>(kEach));
    }
    put(made, picture.sequence, 8);
    put(made, picture.width, 4);
    put(made, picture.height, 4);
    made.insert(made.end(), picture.pixels.begin(), picture.pixels.end());
    return made;
}

std::optional<StreamedPicture> streamedFrom(std::span<const std::byte> bytes) {
    if (bytes.size() < kStreamHeader ||
        !std::ranges::equal(bytes.first(kStreamMark.size()), kStreamMark, [](std::byte held, char wanted) {
            return held == static_cast<std::byte>(wanted);
        })) {
        return std::nullopt;
    }
    StreamedPicture made{.sequence = got(bytes, 8, 8),
                         .width = static_cast<std::uint32_t>(got(bytes, 16, 4)),
                         .height = static_cast<std::uint32_t>(got(bytes, 20, 4)),
                         .pixels = {}};
    if (made.width == 0 || made.height == 0 || made.width > kStreamSide || made.height > kStreamSide ||
        bytes.size() - kStreamHeader != std::size_t{made.width} * made.height * 4) {
        return std::nullopt;
    }
    made.pixels.assign(bytes.begin() + kStreamHeader, bytes.end());
    return made;
}

#if RAWFRAME_FILE_SYSTEM

namespace {

/// Where the stream's flags and sequence lie in its header.
constexpr std::size_t kWriting = 6;
constexpr std::size_t kGivenUp = 7;
constexpr std::size_t kSequence = 8;
/// A stream file's room grows a mebibyte at a time.
constexpr std::size_t kRoomStep = std::size_t{1} << 20U;

std::atomic_ref<std::uint8_t> flag(std::span<std::byte> bytes, std::size_t at) noexcept {
    return std::atomic_ref<std::uint8_t>{*reinterpret_cast<std::uint8_t*>(bytes.data() + at)};
}

std::atomic_ref<std::uint64_t> sequenceIn(std::span<std::byte> bytes) noexcept {
    // Little-endian as the plain file's, on every machine the engine runs on.
    return std::atomic_ref<std::uint64_t>{*reinterpret_cast<std::uint64_t*>(bytes.data() + kSequence)};
}

void putAt(std::span<std::byte> into, std::size_t at, std::uint32_t value) noexcept {
    for (std::size_t each = 0; each < 4; ++each) {
        into[at + each] = static_cast<std::byte>((value >> (8U * each)) & 0xFFU);
    }
}

} // namespace

bool PictureStreamWriter::write(std::uint32_t width, std::uint32_t height, std::span<const std::byte> pixels) {
    if (width == 0 || height == 0 || width > kStreamSide || height > kStreamSide ||
        pixels.size() != std::size_t{width} * height * 4) {
        return false;
    }
    const std::size_t kNeeded = kStreamHeader + pixels.size();
    if (!file_.has_value() || file_->bytes().size() < kNeeded) {
        // Made beside it and moved into its place, so a reader of the old
        // keeps it whole until told it is given up.
        std::filesystem::path made = path_;
        made += ".writing";
        std::optional<base::MappedFile> larger =
            base::MappedFile::make(made, (kNeeded + kRoomStep - 1) / kRoomStep * kRoomStep);
        if (!larger.has_value()) {
            return false;
        }
        std::ranges::transform(kStreamMark, larger->bytes().begin(), [](char each) {
            return static_cast<std::byte>(each);
        });
        sequenceIn(larger->bytes()).store(sequence_, std::memory_order_relaxed);
        if (!base::moveOver(made, path_)) {
            std::error_code error;
            std::filesystem::remove(made, error);
            return false;
        }
        if (file_.has_value()) {
            flag(file_->bytes(), kGivenUp).store(1, std::memory_order_release);
        }
        file_ = std::move(larger);
    }
    // A reader that finds the first flag set, or the sequence moved on
    // between its looks, knows it read while this wrote.
    const std::span<std::byte> kBytes = file_->bytes();
    flag(kBytes, kWriting).store(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    putAt(kBytes, 16, width);
    putAt(kBytes, 20, height);
    std::ranges::copy(pixels, kBytes.begin() + kStreamHeader);
    ++sequence_;
    sequenceIn(kBytes).store(sequence_, std::memory_order_release);
    flag(kBytes, kWriting).store(0, std::memory_order_release);
    return true;
}

std::optional<StreamedPicture> PictureStreamReader::next() {
    const auto kNow = std::chrono::steady_clock::now();
    const bool kStill = file_.has_value() && kNow - changed_ > std::chrono::seconds{1};
    if (file_.has_value() && (kStill || flag(file_->bytes(), kGivenUp).load(std::memory_order_acquire) != 0)) {
        file_.reset();
        changed_ = kNow;
    }
    if (!file_.has_value()) {
        file_ = base::MappedFile::open(path_);
        if (!file_.has_value()) {
            return std::nullopt;
        }
        changed_ = kNow;
    }
    const std::span<std::byte> kBytes{const_cast<std::byte*>(file_->bytes().data()), file_->bytes().size()};
    if (kBytes.size() < kStreamHeader || !std::ranges::equal(kBytes.first(kWriting),
                                                             std::span{kStreamMark}.first(kWriting),
                                                             [](std::byte held, char wanted) {
                                                                 return held == static_cast<std::byte>(wanted);
                                                             })) {
        return std::nullopt;
    }
    const std::uint64_t kBefore = sequenceIn(kBytes).load(std::memory_order_acquire);
    if (flag(kBytes, kWriting).load(std::memory_order_acquire) != 0 || kBefore == taken_ || kBefore == 0) {
        return std::nullopt;
    }
    StreamedPicture read{.sequence = kBefore,
                         .width = static_cast<std::uint32_t>(got(kBytes, 16, 4)),
                         .height = static_cast<std::uint32_t>(got(kBytes, 20, 4)),
                         .pixels = {}};
    const std::size_t kSize = std::size_t{read.width} * read.height * 4;
    if (read.width == 0 || read.height == 0 || read.width > kStreamSide || read.height > kStreamSide ||
        kBytes.size() - kStreamHeader < kSize) {
        return std::nullopt;
    }
    read.pixels.assign(kBytes.begin() + kStreamHeader,
                       kBytes.begin() + static_cast<std::ptrdiff_t>(kStreamHeader + kSize));
    std::atomic_thread_fence(std::memory_order_acquire);
    if (flag(kBytes, kWriting).load(std::memory_order_relaxed) != 0 ||
        sequenceIn(kBytes).load(std::memory_order_relaxed) != kBefore) {
        return std::nullopt;
    }
    taken_ = kBefore;
    changed_ = kNow;
    return read;
}

#endif

} // namespace rawframe::render
