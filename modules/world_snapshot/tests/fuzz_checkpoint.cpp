// Coverage-guided fuzzing of a checkpoint read back (D242). The input is a
// run of edits, each an offset and a byte, made in place to the sample's
// artifact, whose digests are then made to match again: a digest is not a
// signature. What restores must capture to the very same bytes; anything
// else must be refused.

#include "sample.h"

#include <cstdlib>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_snapshot::testing;

namespace {

struct Captured {
    Sample sample;
    std::vector<std::byte> artifact;

    Captured() {
        auto made = world_snapshot::capture(sample.world, projection(), settings());
        must(made.has_value());
        artifact = std::move(*made);
    }
};

Captured& captured() {
    static Captured once;
    return once;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const Captured& kCaptured = captured();
    const std::vector<std::byte>& kArtifact = kCaptured.artifact;
    // The footer stays whole, so the layout holds and can be resealed.
    const std::size_t kEditable = kArtifact.size() - 128;
    std::vector<std::byte> damaged = kArtifact;
    for (std::size_t at = 0; at + 3 <= size && at < 3 * 64; at += 3) {
        const std::size_t kOffset = (static_cast<std::size_t>(data[at]) << 8U) | data[at + 1];
        damaged[kOffset % kEditable] = static_cast<std::byte>(data[at + 2]);
    }
    damaged = resealed(std::move(damaged), kArtifact);
    world::World candidate{kCaptured.sample.schema};
    const auto kRestored = world_snapshot::restore(damaged, projection(), kIdentity, {}, candidate);
    if (!kRestored.has_value()) {
        return 0;
    }
    const auto kAgain = world_snapshot::capture(
        candidate,
        projection(),
        world_snapshot::CaptureSettings{.tick = kRestored->tick, .rate = kRestored->rate, .identity = kIdentity});
    if (!kAgain.has_value() || *kAgain != damaged) {
        std::abort();
    }
    return 0;
}
