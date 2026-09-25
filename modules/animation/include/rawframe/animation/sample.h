#pragma once

// Evaluation (SPEC-0035): a clip bound to a skeleton, sampled into a pose
// at a time, and a playhead advanced with the events it crosses. Pure and
// deterministic: the same clip, time, and advance give the same pose and
// events on every machine, with no platform trigonometry in them.

#include "rawframe/animation/clip.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::animation {

/// Local transforms of a skeleton's bones, in its order: the one pose
/// representation.
struct Pose {
    std::vector<Transform> bones;

    friend bool operator==(const Pose&, const Pose&) = default;
};

/// The skeleton's bind pose.
[[nodiscard]] Pose bindPose(const Skeleton& skeleton);

/// A local pose in model space: each bone composed onto its parent's, in
/// the skeleton's order, parents first (translation rotated and scaled by
/// the parent, rotations multiplied, scales multiplied).
void toModelSpace(std::span<const std::optional<BoneIndex>> parents, const Pose& local, Pose& model);

/// A clip with each track's bone found in one skeleton.
class BoundClip {
public:
    /// Refuses (`BindingInvalid`) a clip of another skeleton than
    /// `skeletonId`, a track of a bone `skeleton` lacks, or a drift on a
    /// bone other than its root.
    [[nodiscard]] static result::Result<BoundClip>
    bind(std::shared_ptr<const Clip> clip, const Skeleton& skeleton, base::Bits128 skeletonId);

    [[nodiscard]] const Clip& clip() const noexcept {
        return *clip_;
    }

    /// Writes the channels the clip animates at `time` (wrapped when the
    /// clip loops, held at its ends when it does not) over `pose`, which
    /// has the skeleton's bones; the others keep what `pose` had. Given
    /// `only` (a byte for each bone), bones whose byte is 0 are left alone.
    void sample(double time, Pose& pose, std::span<const std::uint8_t> only = {}) const;

private:
    std::shared_ptr<const Clip> clip_;
    std::vector<BoneIndex> bones_;
    std::size_t boneCount_ = 0;
};

/// A curve's value at `time`: one track's keys, as its clip wraps or holds
/// them. A rotation is unit.
[[nodiscard]] std::array<double, 4> sampleTrack(const Clip& clip, const Track& track, double time);

/// A curve's value at the end of a period: at its duration, which for a
/// looping clip is where the wrap arrives, its first key moved by the
/// track's drift, rather than the start of the next period.
[[nodiscard]] std::array<double, 4> sampleTrackAtEnd(const Clip& clip, const Track& track);

/// An event the playhead crossed: its place in the clip's events, and
/// whether it was crossed playing backwards.
struct EventCrossing {
    std::size_t event = 0;
    bool reverse = false;

    friend bool operator==(const EventCrossing&, const EventCrossing&) = default;
};

struct Advance {
    /// Where the playhead is now: in [0, duration) when the clip loops, in
    /// [0, duration] when it does not.
    double time = 0.0;
    /// More crossings than the limit allowed: the rest were not reported,
    /// and the advance says so rather than dropping them unseen.
    bool overflowed = false;
};

/// Moves a playhead at `time` by `delta` seconds (backwards when negative)
/// and appends each event crossed, in the order crossed, at most
/// `maximumCrossings` of them (SPEC-0035's window semantics). Forwards, an
/// event fires once for each time the playhead passes it in (from, to];
/// backwards, in [to, from). A looping clip wraps as often as `delta` says;
/// a clamped one stops at its ends. Moving a playhead any other way is a
/// seek, which fires nothing.
[[nodiscard]] Advance advance(const Clip& clip,
                              double time,
                              double delta,
                              std::vector<EventCrossing>& crossed,
                              std::size_t maximumCrossings = 256);

} // namespace rawframe::animation
