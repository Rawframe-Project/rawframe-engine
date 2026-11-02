// Coverage-guided fuzzing of a glTF source as the animation importer reads
// it: a `.gltf` or `.glb` from an author's tools meets cgltf, the skin's
// joints and the animations' channels through the limits, held and looped,
// as poses and as differences, and what imports is a skeleton and clips
// their documents write and read back unchanged. Every file the source
// names reads as the same small buffer, so sibling buffers are reached
// too.

#include "rawframe/animation/clip.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/animation_import/import.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string_view>

using namespace rawframe;

namespace {

/// The seeds' arm: three key times, three shoulder translations and three
/// elbow rotations, as floats.
constexpr auto kSibling = [] {
    constexpr float kHalf = 0.70710677F;
    constexpr std::array<float, 24> kValues = {0, 0.5F, 1, 0, 0, 0, 1,     1,     0, 0, 0, 0,
                                               0, 0,    0, 1, 0, 0, kHalf, kHalf, 0, 0, 0, 1};
    std::array<std::byte, 96> out{};
    for (std::size_t i = 0; i < kValues.size(); ++i) {
        const auto kBits = std::bit_cast<std::uint32_t>(kValues[i]);
        for (std::size_t shift = 0; shift < 4; ++shift) {
            out[(i * 4) + shift] = static_cast<std::byte>((kBits >> (shift * 8)) & 0xFFU);
        }
    }
    return out;
}();

result::Result<std::span<const std::byte>> readSibling(std::string_view) {
    return std::span<const std::byte>{kSibling};
}

void importAs(std::span<const std::byte> bytes, bool loop, std::optional<animation::AdditiveBasis> additive) {
    const animation_import::ImportSettings kSettings{
        .skeleton = base::Bits128{0, 0x5c},
        .loop = loop,
        .additive = additive,
        .maximumBytes = std::size_t{1} << 16U,
        .skeletonLimits = {.maximumBones = 64},
        .clipLimits = {.maximumTracks = 256, .maximumKeys = 4096, .maximumEvents = 64}};
    const auto kImported = animation_import::importGltf(bytes, &readSibling, kSettings);
    if (!kImported.has_value()) {
        return;
    }
    const auto kSkeleton = animation::writeSkeleton(kImported->skeleton, kSettings.skeletonLimits);
    if (!kSkeleton.has_value()) {
        std::abort();
    }
    const auto kSkeletonRead = animation::readSkeleton(*kSkeleton, kSettings.skeletonLimits);
    if (!kSkeletonRead.has_value() || *kSkeletonRead != kImported->skeleton) {
        std::abort();
    }
    for (const animation_import::ImportedClip& kClip : kImported->clips) {
        const auto kWritten = animation::writeClip(kClip.clip, kSettings.clipLimits);
        if (!kWritten.has_value()) {
            std::abort();
        }
        const auto kRead = animation::readClip(*kWritten, kSettings.clipLimits);
        if (!kRead.has_value() || *kRead != kClip.clip) {
            std::abort();
        }
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    importAs(kBytes, false, std::nullopt);
    importAs(kBytes, true, animation::AdditiveBasis::Bind);
    importAs(kBytes, false, animation::AdditiveBasis::FirstFrame);
    return 0;
}
