#include "rawframe/animation/sample.h"

#include "rawframe/animation/errors.h"
#include "rawframe/base/assert.h"
#include "rotation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

namespace rawframe::animation {

namespace {

std::unexpected<result::Error> unbound(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::BindingInvalid), why);
}

/// A time in [0, duration).
double wrapped(double time, double duration) noexcept {
    double made = std::fmod(time, duration);
    if (made < 0.0) {
        made += duration;
    }
    // A sliver below nought can round up to the duration itself.
    return made < duration ? made : 0.0;
}

/// From `from` into `to` at `at` in [0, 1], over `span` seconds, as the
/// channel turns or moves.
std::array<double, 4> between(Channel channel, const Key& from, const Key& to, double at, double span) {
    switch (from.interpolation) {
    case Interpolation::Step:
        return from.value;
    case Interpolation::Linear:
        break;
    case Interpolation::Cubic: {
        // Hermite, with the tangents per second scaled by the span (glTF's
        // CUBICSPLINE).
        const double kSquare = at * at;
        const double kCube = kSquare * at;
        const double kFromValue = (2.0 * kCube) - (3.0 * kSquare) + 1.0;
        const double kFromTangent = kCube - (2.0 * kSquare) + at;
        const double kToValue = (-2.0 * kCube) + (3.0 * kSquare);
        const double kToTangent = kCube - kSquare;
        std::array<double, 4> made{};
        for (std::size_t each = 0; each < widthOf(channel); ++each) {
            made[each] = (kFromValue * from.value[each]) + (kFromTangent * span * from.out[each]) +
                         (kToValue * to.value[each]) + (kToTangent * span * to.in[each]);
        }
        return made;
    }
    }
    if (channel == Channel::Rotation) {
        return slerp(from.value, to.value, at);
    }
    std::array<double, 4> made{};
    for (std::size_t each = 0; each < widthOf(channel); ++each) {
        made[each] = from.value[each] + ((to.value[each] - from.value[each]) * at);
    }
    return made;
}

/// A looping track's first key where the wrap arrives: moved by the
/// track's drift, if it has one.
Key arrival(const Track& track) {
    Key made = track.keys.front();
    if (!track.drift.has_value()) {
        return made;
    }
    if (track.channel == Channel::Rotation) {
        made.value = normalized(multiplied(*track.drift, made.value));
    } else {
        for (std::size_t each = 0; each < 3; ++each) {
            made.value[each] += (*track.drift)[each];
        }
    }
    return made;
}

} // namespace

Pose bindPose(const Skeleton& skeleton) {
    Pose pose;
    pose.bones.reserve(skeleton.bones.size());
    for (const Bone& bone : skeleton.bones) {
        pose.bones.push_back(bone.bind);
    }
    return pose;
}

void toModelSpace(std::span<const std::optional<BoneIndex>> parents, const Pose& local, Pose& model) {
    RAWFRAME_CHECK(parents.size() == local.bones.size(), "a pose of the skeleton");
    model.bones.resize(local.bones.size());
    for (std::size_t at = 0; at < local.bones.size(); ++at) {
        const Transform& bone = local.bones[at];
        if (!parents[at].has_value()) {
            model.bones[at] = bone;
            continue;
        }
        const Transform& parent = model.bones[parents[at]->value];
        const std::array<double, 4>& q = parent.rotation;
        // v + 2w (q x v) + 2 q x (q x v), of the scaled translation.
        const std::array<double, 3> kV{parent.scale[0] * bone.translation[0],
                                       parent.scale[1] * bone.translation[1],
                                       parent.scale[2] * bone.translation[2]};
        const std::array<double, 3> kC{
            (q[1] * kV[2]) - (q[2] * kV[1]), (q[2] * kV[0]) - (q[0] * kV[2]), (q[0] * kV[1]) - (q[1] * kV[0])};
        const std::array<double, 3> kCc{
            (q[1] * kC[2]) - (q[2] * kC[1]), (q[2] * kC[0]) - (q[0] * kC[2]), (q[0] * kC[1]) - (q[1] * kC[0])};
        Transform& made = model.bones[at];
        for (std::size_t each = 0; each < 3; ++each) {
            made.translation[each] = parent.translation[each] + kV[each] + (2.0 * q[3] * kC[each]) + (2.0 * kCc[each]);
            made.scale[each] = parent.scale[each] * bone.scale[each];
        }
        const std::array<double, 4>& r = bone.rotation;
        made.rotation = {(q[3] * r[0]) + (q[0] * r[3]) + (q[1] * r[2]) - (q[2] * r[1]),
                         (q[3] * r[1]) - (q[0] * r[2]) + (q[1] * r[3]) + (q[2] * r[0]),
                         (q[3] * r[2]) + (q[0] * r[1]) - (q[1] * r[0]) + (q[2] * r[3]),
                         (q[3] * r[3]) - (q[0] * r[0]) - (q[1] * r[1]) - (q[2] * r[2])};
    }
}

Transform composed(const Transform& a, const Transform& b) noexcept {
    const std::array<double, 3> kMoved = rotated(a.rotation, b.translation);
    return Transform{
        .translation = {a.translation[0] + kMoved[0], a.translation[1] + kMoved[1], a.translation[2] + kMoved[2]},
        .rotation = normalized(multiplied(a.rotation, b.rotation))};
}

result::Result<BoundClip>
BoundClip::bind(std::shared_ptr<const Clip> clip, const Skeleton& skeleton, base::Bits128 skeletonId) {
    if (clip->skeleton.has_value() && *clip->skeleton != skeletonId) {
        return unbound("a clip binds to the skeleton it names");
    }
    std::vector<std::tuple<base::Bits128, std::uint32_t>> targets;
    for (std::size_t at = 0; at < skeleton.bones.size(); ++at) {
        targets.emplace_back(skeleton.bones[at].target, static_cast<std::uint32_t>(at));
    }
    std::ranges::sort(targets);
    BoundClip made;
    for (const Track& track : clip->tracks) {
        if (track.property.has_value()) {
            // Not a bone's: no bone of any skeleton.
            made.bones_.push_back(BoneIndex{UINT32_MAX});
            continue;
        }
        const auto kFound = std::ranges::lower_bound(targets, track.bone, {}, [](const auto& target) {
            return std::get<0>(target);
        });
        if (kFound == targets.end() || std::get<0>(*kFound) != track.bone) {
            return unbound("a clip animates only bones its skeleton has");
        }
        if (track.drift.has_value() && std::get<1>(*kFound) != 0) {
            return unbound("only the skeleton's root drifts");
        }
        made.bones_.push_back(BoneIndex{std::get<1>(*kFound)});
    }
    made.clip_ = std::move(clip);
    made.boneCount_ = skeleton.bones.size();
    return made;
}

void BoundClip::sample(double time, Pose& pose, std::span<const std::uint8_t> only) const {
    RAWFRAME_CHECK(pose.bones.size() == boneCount_, "a pose of the clip's skeleton");
    RAWFRAME_CHECK(only.empty() || only.size() == boneCount_, "a subset of the clip's skeleton");
    for (std::size_t at = 0; at < bones_.size(); ++at) {
        if (clip_->tracks[at].property.has_value() || (!only.empty() && only[bones_[at].value] == 0)) {
            continue;
        }
        const Track& track = clip_->tracks[at];
        const std::array<double, 4> kValue = sampleTrack(*clip_, track, time);
        Transform& bone = pose.bones[bones_[at].value];
        switch (track.channel) {
        case Channel::Translation:
            bone.translation = {kValue[0], kValue[1], kValue[2]};
            break;
        case Channel::Rotation:
            bone.rotation = kValue;
            break;
        case Channel::Scale:
            bone.scale = {kValue[0], kValue[1], kValue[2]};
            break;
        case Channel::Float:
        case Channel::Discrete:
            break;
        }
    }
}

std::array<double, 4> sampleTrack(const Clip& clip, const Track& track, double time) {
    const double kDuration = clip.duration;
    if (!std::isfinite(time)) {
        time = 0.0;
    }
    time = clip.loop == Loop::Loop ? wrapped(time, kDuration) : std::clamp(time, 0.0, kDuration);
    const std::vector<Key>& keys = track.keys;
    const Key& first = keys.front();
    const Key& last = keys.back();
    // The key at or before `time`.
    const auto kAfter = std::ranges::upper_bound(keys, time, {}, &Key::time);
    std::array<double, 4> made{};
    if ((keys.size() == 1 && !track.drift.has_value()) ||
        (clip.loop == Loop::Clamp && (kAfter == keys.begin() || kAfter == keys.end()))) {
        made = kAfter == keys.begin() ? first.value : (kAfter - 1)->value;
    } else if (kAfter == keys.end()) {
        // Across the wrap, from the last key to the first where the wrap
        // arrives.
        const double kSpan = kDuration - last.time + first.time;
        made = between(track.channel, last, arrival(track), (time - last.time) / kSpan, kSpan);
    } else if (kAfter == keys.begin()) {
        // Before the first key, still on the way from the last: where the
        // previous period's wrap was going, the first key as it is.
        const double kSpan = kDuration - last.time + first.time;
        Key from = last;
        if (track.drift.has_value()) {
            // The last key a period earlier, so the segment ends on the
            // first key as it is.
            if (track.channel == Channel::Rotation) {
                from.value = normalized(multiplied(inverted(*track.drift), from.value));
            } else {
                for (std::size_t each = 0; each < 3; ++each) {
                    from.value[each] -= (*track.drift)[each];
                }
            }
        }
        made = between(track.channel, from, first, (time + kDuration - last.time) / kSpan, kSpan);
    } else {
        const Key& from = *(kAfter - 1);
        const double kSpan = kAfter->time - from.time;
        made = between(track.channel, from, *kAfter, (time - from.time) / kSpan, kSpan);
    }
    return track.channel == Channel::Rotation ? normalized(made) : made;
}

std::array<double, 4> sampleTrackAtEnd(const Clip& clip, const Track& track) {
    if (clip.loop == Loop::Clamp) {
        return sampleTrack(clip, track, clip.duration);
    }
    return arrival(track).value;
}

Advance advance(
    const Clip& clip, double time, double delta, std::vector<EventCrossing>& crossed, std::size_t maximumCrossings) {
    const double kDuration = clip.duration;
    const std::vector<ClipEvent>& events = clip.events;
    time = std::isfinite(time) ? time : 0.0;
    delta = std::isfinite(delta) ? delta : 0.0;
    Advance made;
    std::size_t reported = 0;
    const auto kReport = [&](std::size_t event, bool reverse) {
        if (reported == maximumCrossings) {
            made.overflowed = true;
            return;
        }
        crossed.push_back(EventCrossing{.event = event, .reverse = reverse});
        ++reported;
    };
    // Each event of one period whose moment, `base` on, is in the window:
    // forwards (from, to] in written order, backwards [to, from) latest
    // first, events of one time still in written order.
    const auto kPeriod = [&](double base, double from, double to, bool reverse) {
        if (!reverse) {
            for (std::size_t at = 0; at < events.size() && !made.overflowed; ++at) {
                const double kMoment = base + events[at].time;
                if (kMoment > from && kMoment <= to) {
                    kReport(at, false);
                }
            }
            return;
        }
        for (std::size_t end = events.size(); end > 0 && !made.overflowed;) {
            std::size_t start = end - 1;
            while (start > 0 && events[start - 1].time == events[end - 1].time) {
                --start;
            }
            const double kMoment = base + events[start].time;
            for (std::size_t at = start; at < end && kMoment >= to && kMoment < from && !made.overflowed; ++at) {
                kReport(at, true);
            }
            end = start;
        }
    };
    if (clip.loop == Loop::Clamp) {
        const double kFrom = std::clamp(time, 0.0, kDuration);
        const double kTo = std::clamp(kFrom + delta, 0.0, kDuration);
        if (kTo != kFrom) {
            kPeriod(0.0, kFrom, kTo, kTo < kFrom);
        }
        made.time = kTo;
        return made;
    }
    // Looping: the window in unwrapped time, one period of the clip at a
    // time. Every whole period crosses every event, so a long advance stops
    // at the limit long before it runs out of periods.
    const double kFrom = wrapped(time, kDuration);
    const double kTo = kFrom + delta;
    if (!events.empty() && delta > 0.0) {
        for (double period = 0.0; period * kDuration <= kTo && !made.overflowed; period += 1.0) {
            kPeriod(period * kDuration, kFrom, kTo, false);
        }
    } else if (!events.empty() && delta < 0.0) {
        for (double period = 0.0; (period + 1.0) * kDuration > kTo && !made.overflowed; period -= 1.0) {
            kPeriod(period * kDuration, kFrom, kTo, true);
        }
    }
    made.time = wrapped(kTo, kDuration);
    return made;
}

} // namespace rawframe::animation
