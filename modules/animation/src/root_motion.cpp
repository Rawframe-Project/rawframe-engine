#include "root_motion.h"

#include "rawframe/animation/sample.h"
#include "rotation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rawframe::animation {

namespace {

/// Where the source puts the character at `time`, or at the end of a
/// period: its taken translation axes and its turn.
Transform placedAt(const Clip& clip,
                   const RootTracks& tracks,
                   const RootMotionSource& source,
                   const Transform& rootBind,
                   double time,
                   bool end) {
    const auto kSampled = [&clip, time, end](const Track& track) {
        return end ? sampleTrackAtEnd(clip, track) : sampleTrack(clip, track, time);
    };
    Transform made;
    if (tracks.translation != nullptr) {
        const std::array<double, 4> kTranslation = kSampled(*tracks.translation);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            made.translation[axis] = source.translation[axis] ? kTranslation[axis] : 0.0;
        }
    } else {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            made.translation[axis] = source.translation[axis] ? rootBind.translation[axis] : 0.0;
        }
    }
    if (tracks.rotation != nullptr) {
        made.rotation = twistOf(source, kSampled(*tracks.rotation), rootBind.rotation);
    }
    return made;
}

/// From `from` to `to`, in the frame of `from`.
Transform between(const Transform& from, const Transform& to) noexcept {
    const std::array<double, 4> kBack = inverted(from.rotation);
    return Transform{.translation = rotated(kBack,
                                            {to.translation[0] - from.translation[0],
                                             to.translation[1] - from.translation[1],
                                             to.translation[2] - from.translation[2]}),
                     .rotation = normalized(multiplied(kBack, to.rotation))};
}

/// `move` made `count` times over, by squaring.
Transform repeated(Transform move, double count) {
    // Past this many periods a tick is no longer playing the clip; the
    // count stays whole and bounded so the loop below is.
    constexpr double kMostPeriods = 4294967296.0;
    auto left = static_cast<std::uint64_t>(std::min(count, kMostPeriods));
    Transform made;
    while (left != 0) {
        if ((left & 1U) != 0) {
            made = composed(made, move);
        }
        move = composed(move, move);
        left >>= 1U;
    }
    return made;
}

} // namespace

std::array<double, 4>
twistOf(const RootMotionSource& source, const std::array<double, 4>& rotation, const std::array<double, 4>& bind) {
    if (!source.rotation.has_value()) {
        return {0.0, 0.0, 0.0, 1.0};
    }
    // The turn from the bind, then its part about the axis: the vector part
    // projected onto the axis, made unit (swing-twist).
    const std::array<double, 4> kTurn = multiplied(rotation, inverted(bind));
    const auto kAxis = static_cast<std::size_t>(*source.rotation);
    std::array<double, 4> made{0.0, 0.0, 0.0, kTurn[3]};
    made[kAxis] = kTurn[kAxis];
    return normalized(made);
}

Transform motionOver(const Clip& clip,
                     const RootTracks& tracks,
                     const RootMotionSource& source,
                     const Transform& rootBind,
                     double from,
                     double delta,
                     double landed) {
    const auto kAt = [&](double time) {
        return placedAt(clip, tracks, source, rootBind, time, false);
    };
    if (!std::isfinite(from) || !std::isfinite(delta)) {
        return {};
    }
    if (clip.loop == Loop::Clamp) {
        const double kFrom = std::clamp(from, 0.0, clip.duration);
        return between(kAt(kFrom), kAt(std::clamp(kFrom + delta, 0.0, clip.duration)));
    }
    // The wraps between: how many whole durations part where the playhead
    // would be unwrapped from where it landed.
    const double kWraps = std::round((from + delta - landed) / clip.duration);
    if (kWraps == 0.0) {
        return between(kAt(from), kAt(landed));
    }
    const Transform kStart = kAt(0.0);
    const Transform kEnd = placedAt(clip, tracks, source, rootBind, clip.duration, true);
    if (kWraps > 0.0) {
        const Transform kPeriod = between(kStart, kEnd);
        return composed(composed(between(kAt(from), kEnd), repeated(kPeriod, kWraps - 1.0)),
                        between(kStart, kAt(landed)));
    }
    const Transform kBackPeriod = between(kEnd, kStart);
    return composed(composed(between(kAt(from), kStart), repeated(kBackPeriod, -kWraps - 1.0)),
                    between(kEnd, kAt(landed)));
}

Transform blendedMotion(std::span<const Transform* const> moves, std::span<const double> shares) {
    Transform sum{.translation = {}, .rotation = {0.0, 0.0, 0.0, 0.0}};
    const std::array<double, 4>* first = nullptr;
    for (std::size_t at = 0; at < moves.size(); ++at) {
        if (shares[at] == 0.0) {
            continue;
        }
        const Transform& move = *moves[at];
        if (first == nullptr) {
            first = &move.rotation;
        }
        // Each rotation into the hemisphere of the first, as q and -q are
        // one rotation.
        const double kDot = (move.rotation[0] * (*first)[0]) + (move.rotation[1] * (*first)[1]) +
                            (move.rotation[2] * (*first)[2]) + (move.rotation[3] * (*first)[3]);
        const double kSign = kDot < 0.0 ? -shares[at] : shares[at];
        for (std::size_t each = 0; each < 3; ++each) {
            sum.translation[each] += shares[at] * move.translation[each];
        }
        for (std::size_t each = 0; each < 4; ++each) {
            sum.rotation[each] += kSign * move.rotation[each];
        }
    }
    sum.rotation = normalized(sum.rotation);
    return sum;
}

} // namespace rawframe::animation
