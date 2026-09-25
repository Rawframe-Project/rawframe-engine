// Evaluation: keys interpolated as their kind says, rotations turned by a
// slerp that is the same bits on every machine, clips bound only to their
// skeleton, and events fired exactly once per crossing.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/sample.h"
#include "rawframe/base/sha256.h"
#include "rawframe/test/test.h"

#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

bool near(double a, double b, double tolerance = 1e-12) {
    return std::abs(a - b) <= tolerance;
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr base::Bits128 kArm{1, 2};

Skeleton rig() {
    return Skeleton{
        .bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}},
                  Bone{.target = kArm, .name = "arm", .parent = BoneIndex{0}, .bind = {.translation = {1, 0, 0}}}}};
}

/// About z by `angle` radians.
std::array<double, 4> aboutZ(double angle) {
    return {0.0, 0.0, std::sin(angle / 2.0), std::cos(angle / 2.0)};
}

Track moving(std::vector<Key> keys) {
    return Track{.bone = kRoot, .channel = Channel::Translation, .keys = std::move(keys)};
}

Clip clipOf(Loop loop, std::vector<Track> tracks, std::vector<ClipEvent> events = {}) {
    return Clip{.skeleton = kSkeletonId,
                .duration = 2.0,
                .loop = loop,
                .tracks = std::move(tracks),
                .events = std::move(events),
                .syncMarkers = {}};
}

double yAt(const Clip& clip, double time) {
    return sampleTrack(clip, clip.tracks[0], time)[1];
}

} // namespace

RAWFRAME_TEST(KeysInterpolateAsTheirKindSays) {
    const Clip kClamped =
        clipOf(Loop::Clamp,
               {moving({Key{.time = 0.5, .value = {0, 1, 0, 0}},
                        Key{.time = 1.0, .value = {0, 3, 0, 0}, .interpolation = Interpolation::Step},
                        Key{.time = 1.5, .value = {0, 5, 0, 0}, .interpolation = Interpolation::Cubic},
                        Key{.time = 2.0, .value = {0, 9, 0, 0}, .interpolation = Interpolation::Cubic}})});
    RAWFRAME_EXPECT(yAt(kClamped, 0.0) == 1.0);
    RAWFRAME_EXPECT(yAt(kClamped, 0.75) == 2.0);
    RAWFRAME_EXPECT(yAt(kClamped, 1.0) == 3.0);
    RAWFRAME_EXPECT(yAt(kClamped, 1.25) == 3.0);
    // No tangents: smoothstep, a quarter of the way is 5 + 4 * 5/32.
    RAWFRAME_EXPECT(yAt(kClamped, 1.625) == 5.625);
    RAWFRAME_EXPECT(yAt(kClamped, 2.0) == 9.0);
    RAWFRAME_EXPECT(yAt(kClamped, 7.0) == 9.0);
    RAWFRAME_EXPECT(yAt(kClamped, -1.0) == 1.0);
    // Tangents per second, scaled by the span: a rise of 2 per second over
    // half a second is 1 by the midpoint's derivative terms.
    const Clip kTangent = clipOf(Loop::Clamp,
                                 {moving({Key{.time = 0.0, .interpolation = Interpolation::Cubic, .out = {0, 2, 0, 0}},
                                          Key{.time = 0.5, .interpolation = Interpolation::Step, .in = {}}})});
    RAWFRAME_EXPECT(near(yAt(kTangent, 0.25), 0.125));
    // Looping, the last key eases into the first across the wrap.
    const Clip kLooped = clipOf(
        Loop::Loop, {moving({Key{.time = 0.5, .value = {0, 2, 0, 0}}, Key{.time = 1.5, .value = {0, 4, 0, 0}}})});
    RAWFRAME_EXPECT(yAt(kLooped, 1.0) == 3.0);
    RAWFRAME_EXPECT(yAt(kLooped, 1.75) == 3.5);
    RAWFRAME_EXPECT(yAt(kLooped, 0.0) == 3.0);
    RAWFRAME_EXPECT(yAt(kLooped, 0.25) == 2.5);
    RAWFRAME_EXPECT(yAt(kLooped, 2.25) == 2.5);
    RAWFRAME_EXPECT(yAt(kLooped, -1.75) == 2.5);
}

RAWFRAME_TEST(RotationsTurnBySlerpAlongTheShorterArc) {
    const auto kTurned = [](std::array<double, 4> to, double at) {
        const Clip kClip =
            clipOf(Loop::Clamp,
                   {Track{.bone = kRoot,
                          .channel = Channel::Rotation,
                          .keys = {Key{.time = 0.0, .value = aboutZ(0.0)}, Key{.time = 1.0, .value = to}}}});
        return sampleTrack(kClip, kClip.tracks[0], at);
    };
    // A constant rate of turn, where lerping components would bunch up.
    for (const double kAngle : {0.001, 0.5, std::numbers::pi / 2.0, 3.0}) {
        for (const double kAt : {0.0, 0.1, 0.25, 0.5, 0.9, 1.0}) {
            const std::array<double, 4> kMade = kTurned(aboutZ(kAngle), kAt);
            const std::array<double, 4> kWanted = aboutZ(kAngle * kAt);
            for (std::size_t each = 0; each < 4; ++each) {
                RAWFRAME_EXPECT(near(kMade[each], kWanted[each], 1e-14));
            }
        }
    }
    // -q is q: the negated end still turns the short way, and lands on q.
    const std::array<double, 4> kFar = aboutZ(1.0);
    const std::array<double, 4> kNegated{-kFar[0], -kFar[1], -kFar[2], -kFar[3]};
    const std::array<double, 4> kHalfway = kTurned(kNegated, 0.5);
    RAWFRAME_EXPECT(near(kHalfway[2], aboutZ(0.5)[2], 1e-14) && near(kHalfway[3], aboutZ(0.5)[3], 1e-14));
}

RAWFRAME_TEST(EvaluationIsTheSameBitsEverywhere) {
    // Poses along a looping clip of every interpolation, hashed bit for
    // bit: a machine or compiler that turns or eases differently moves it.
    const double kHalf = std::sqrt(0.5);
    const Clip kClip = clipOf(
        Loop::Loop,
        {moving(
             {Key{.time = 0.0, .value = {0, 1, 0, 0}, .interpolation = Interpolation::Cubic, .out = {1, 0, 2, 0}},
              Key{.time = 0.7, .value = {0.5, 1.5, -1, 0}, .interpolation = Interpolation::Cubic, .in = {0, 1, 0, 0}},
              Key{.time = 1.3, .value = {0, 2, 0, 0}}}),
         Track{.bone = kArm,
               .channel = Channel::Rotation,
               .keys = {Key{.time = 0.25, .value = {0, 0, 0, 1}},
                        Key{.time = 1.0, .value = {kHalf, 0, 0, kHalf}},
                        Key{.time = 1.5, .value = {0, 0.6, 0, 0.8}}}}});
    const auto kBound = BoundClip::bind(std::make_shared<const Clip>(kClip), rig(), kSkeletonId);
    RAWFRAME_EXPECT(kBound.has_value());
    if (!kBound.has_value()) {
        return;
    }
    base::Sha256 digest;
    Pose pose = bindPose(rig());
    for (int step = 0; step < 240; ++step) {
        kBound->sample(step / 60.0, pose);
        for (const Transform& kBone : pose.bones) {
            for (const std::span<const double> kNumbers : {std::span<const double>{kBone.translation},
                                                           std::span<const double>{kBone.rotation},
                                                           std::span<const double>{kBone.scale}}) {
                for (const double kNumber : kNumbers) {
                    const auto kBits = std::bit_cast<std::array<std::byte, 8>>(kNumber);
                    digest.update(kBits);
                }
            }
        }
    }
    const base::Sha256Digest kDigest = digest.finish();
    std::string hex;
    for (const std::byte kByte : kDigest) {
        hex.push_back("0123456789abcdef"[std::to_integer<std::size_t>(kByte) >> 4U]);
        hex.push_back("0123456789abcdef"[std::to_integer<std::size_t>(kByte) & 0xFU]);
    }
    RAWFRAME_EXPECT(hex == "372dffcc9502c76df2de13be54a617e10bf8f4630905da1a1d5be546cbf8d629");
}

RAWFRAME_TEST(AClipBindsOnlyToItsSkeleton) {
    const Clip kClip =
        clipOf(Loop::Clamp,
               {Track{.bone = kArm, .channel = Channel::Scale, .keys = {Key{.time = 0.0, .value = {2, 2, 2, 0}}}}});
    const auto kClipped = std::make_shared<const Clip>(kClip);
    RAWFRAME_EXPECT(refusedWith(BoundClip::bind(kClipped, rig(), base::Bits128{7, 8}), AnimationError::BindingInvalid));
    Skeleton armless = rig();
    armless.bones.pop_back();
    RAWFRAME_EXPECT(refusedWith(BoundClip::bind(kClipped, armless, kSkeletonId), AnimationError::BindingInvalid));
    const auto kBound = BoundClip::bind(kClipped, rig(), kSkeletonId);
    RAWFRAME_EXPECT(kBound.has_value());
    if (!kBound.has_value()) {
        return;
    }
    // Only what the clip animates changes.
    Pose pose = bindPose(rig());
    kBound->sample(0.5, pose);
    RAWFRAME_EXPECT(pose.bones[1].scale == (std::array<double, 3>{2, 2, 2}));
    RAWFRAME_EXPECT(pose.bones[1].translation == rig().bones[1].bind.translation);
    RAWFRAME_EXPECT(pose.bones[0] == rig().bones[0].bind);
}

RAWFRAME_TEST(EventsFireOncePerCrossing) {
    const std::vector<ClipEvent> kEvents{ClipEvent{.event = 1, .name = "start", .time = 0.0},
                                         ClipEvent{.event = 2, .name = "left", .time = 0.5},
                                         ClipEvent{.event = 3, .name = "dust", .time = 0.5},
                                         ClipEvent{.event = 4, .name = "right", .time = 1.5}};
    const auto kCrossed = [](const Clip& clip, double time, double delta, std::size_t limit = 256) {
        std::vector<EventCrossing> crossed;
        const Advance kMade = advance(clip, time, delta, crossed, limit);
        std::vector<std::size_t> events;
        for (const EventCrossing& kEach : crossed) {
            events.push_back(kEach.event);
            RAWFRAME_EXPECT(kEach.reverse == (delta < 0.0));
        }
        return std::tuple{kMade.time, events, kMade.overflowed};
    };
    using Events = std::vector<std::size_t>;
    const Clip kLooped = clipOf(Loop::Loop, {}, kEvents);
    // (from, to]: the start of a window is not in it, its end is.
    RAWFRAME_EXPECT((kCrossed(kLooped, 0.0, 0.5) == std::tuple{0.5, Events{1, 2}, false}));
    RAWFRAME_EXPECT((kCrossed(kLooped, 0.5, 1.0) == std::tuple{1.5, Events{3}, false}));
    // Split into steps, the same events as in one.
    RAWFRAME_EXPECT((kCrossed(kLooped, 1.5, 1.0) == std::tuple{0.5, Events{0, 1, 2}, false}));
    RAWFRAME_EXPECT((kCrossed(kLooped, 1.5, 0.5) == std::tuple{0.0, Events{0}, false}));
    RAWFRAME_EXPECT((kCrossed(kLooped, 0.0, 0.5) == std::tuple{0.5, Events{1, 2}, false}));
    // Two whole loops and a little: each event once a loop.
    RAWFRAME_EXPECT((kCrossed(kLooped, 1.0, 4.75) == std::tuple{1.75, Events{3, 0, 1, 2, 3, 0, 1, 2, 3}, false}));
    // Backwards, [to, from), latest first, events of one time as written.
    RAWFRAME_EXPECT((kCrossed(kLooped, 1.0, -1.0) == std::tuple{0.0, Events{1, 2, 0}, false}));
    RAWFRAME_EXPECT((kCrossed(kLooped, 0.25, -0.5) == std::tuple{1.75, Events{0}, false}));
    RAWFRAME_EXPECT((kCrossed(kLooped, 0.25, -1.0) == std::tuple{1.25, Events{0, 3}, false}));
    RAWFRAME_EXPECT((kCrossed(kLooped, 0.5, 0.0) == std::tuple{0.5, Events{}, false}));
    // Past the limit, the advance says so instead of dropping them unseen.
    RAWFRAME_EXPECT(
        (kCrossed(kLooped, 0.0, 1e300, 5) == std::tuple{std::fmod(1e300, 2.0), Events{1, 2, 3, 0, 1}, true}));
    // Clamped, it stops at the ends and fires nothing past them.
    const Clip kClamped = clipOf(Loop::Clamp, {}, kEvents);
    RAWFRAME_EXPECT((kCrossed(kClamped, 0.0, 9.0) == std::tuple{2.0, Events{1, 2, 3}, false}));
    RAWFRAME_EXPECT((kCrossed(kClamped, 2.0, 1.0) == std::tuple{2.0, Events{}, false}));
    RAWFRAME_EXPECT((kCrossed(kClamped, 2.0, -9.0) == std::tuple{0.0, Events{3, 1, 2, 0}, false}));
    // What is not a number moves nothing.
    RAWFRAME_EXPECT((kCrossed(kLooped, 0.25, std::nan("")) == std::tuple{0.25, Events{}, false}));
}
