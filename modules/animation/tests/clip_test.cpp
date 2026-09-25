// Clips: one canonical text, and every rule of keys, bindings, and events
// held against hostile documents.

#include "rawframe/animation/clip.h"
#include "rawframe/animation/errors.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

constexpr base::Bits128 kSkeleton{0x52771075251e7361ULL, 0xdeaecf4939c72e56ULL};
constexpr base::Bits128 kHips{1, 1};
constexpr base::Bits128 kSpine{1, 2};

/// A looping second: the hips move, stepped and eased, and the spine turns.
Clip walk() {
    const double kHalf = std::sqrt(0.5);
    return Clip{.skeleton = kSkeleton,
                .duration = 1.0,
                .loop = Loop::Loop,
                .tracks = {Track{.bone = kHips,
                                 .channel = Channel::Translation,
                                 .keys = {Key{.time = 0.0, .value = {0.0, 1.0, 0.0, 0.0}},
                                          Key{.time = 0.5,
                                              .value = {0.0, 1.25, 0.0, 0.0},
                                              .interpolation = Interpolation::Cubic,
                                              .in = {0.0, 1.0, 0.0, 0.0},
                                              .out = {0.0, -1.0, 0.0, 0.0}},
                                          Key{.time = 0.75,
                                              .value = {0.0, 1.0, 0.5, 0.0},
                                              .interpolation = Interpolation::Step}}},
                           Track{.bone = kSpine,
                                 .channel = Channel::Rotation,
                                 .keys = {Key{.time = 0.0, .value = {0.0, 0.0, 0.0, 1.0}},
                                          Key{.time = 0.5, .value = {0.0, 0.0, kHalf, kHalf}}}}},
                .events = {ClipEvent{.event = 0x5f3a0c2d9e81b746ULL,
                                     .name = "footstep",
                                     .time = 0.25,
                                     .relevance = Relevance::Simulation},
                           ClipEvent{.event = 0x1111111111111111ULL, .name = "dust", .time = 0.25},
                           ClipEvent{.event = 0x5f3a0c2d9e81b746ULL,
                                     .name = "footstep",
                                     .time = 0.75,
                                     .relevance = Relevance::Simulation}},
                .syncMarkers = {SyncMarker{.name = "left_foot", .time = 0.25}}};
}

} // namespace

RAWFRAME_TEST(AClipHasOneText) {
    const auto kText = writeClip(walk());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    // Changing the form moves this, and needs a new format version.
    RAWFRAME_EXPECT(kText->starts_with("{\n  \"formatVersion\": 1,\n  \"kind\": \"animation.clip\",\n  \"skeleton\": "
                                       "\"52771075251e7361deaecf4939c72e56\",\n  \"duration\": 1,\n  \"loop\": "
                                       "\"loop\",\n  \"tracks\": [\n"));
    RAWFRAME_EXPECT(kText->contains("\"interpolation\": \"cubic\",\n          \"in\": ["));
    RAWFRAME_EXPECT(kText->contains("\"event\": \"5f3a0c2d9e81b746\",\n      \"name\": \"footstep\",\n      \"time\": "
                                    "0.25,\n      \"relevance\": \"simulation\""));
    RAWFRAME_EXPECT(kText->ends_with("\"syncMarkers\": [\n    {\n      \"name\": \"left_foot\",\n      \"time\": "
                                     "0.25\n    }\n  ]\n}\n"));
    const auto kRead = readClip(*kText);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == walk());
    // A clip of events alone names no skeleton.
    const Clip kCue{.duration = 2.0, .events = {ClipEvent{.event = 1, .name = "cue", .time = 2.0}}};
    const auto kCueText = writeClip(kCue);
    RAWFRAME_EXPECT(kCueText.has_value() && !kCueText->contains("skeleton") && kCueText->contains("\"tracks\": []"));
    RAWFRAME_EXPECT(kCueText.has_value() && readClip(*kCueText) == kCue);
}

RAWFRAME_TEST(AClipOutOfItsRulesIsRefused) {
    std::vector<Clip> wrong(19, walk());
    wrong[0].duration = 0.0;
    wrong[1].duration = std::numeric_limits<double>::quiet_NaN();
    wrong[2].tracks[0].keys[1].time = 0.0;
    wrong[3].tracks[0].keys[2].time = 1.0;
    wrong[4].tracks[1].keys[1].interpolation = Interpolation::Cubic;
    wrong[5].tracks[1].keys[1].value = {0.0, 0.0, 1.0, 1.0};
    wrong[6].tracks[1].bone = kHips;
    wrong[6].tracks[1].channel = Channel::Translation;
    wrong[6].tracks[1].keys = {Key{.time = 0.0}};
    wrong[7].tracks[0].keys.clear();
    wrong[8].skeleton.reset();
    wrong[9].events[1].time = 0.125;
    wrong[10].events[1].name = "footstep";
    wrong[11].events[2].event = 7;
    wrong[12].events[1].name = "Dust";
    wrong[13].events[0].event = 0;
    wrong[14].syncMarkers[0].time = 1.0;
    wrong[15].tracks[0].keys[0].value[3] = 1.0;
    wrong[16].tracks[0].keys[0].in = {1.0, 0.0, 0.0, 0.0};
    wrong[17].tracks[0].keys[1].out[3] = 1.0;
    wrong[18].tracks[0].bone = {};
    for (const Clip& kClip : wrong) {
        RAWFRAME_EXPECT(refusedWith(writeClip(kClip), AnimationError::ClipInvalid));
    }
    // A clamped clip may hold a key at its duration; a looping one may not,
    // for its duration is its start.
    Clip clamped = walk();
    clamped.loop = Loop::Clamp;
    clamped.tracks[0].keys[2].time = 1.0;
    RAWFRAME_EXPECT(writeClip(clamped).has_value());
    RAWFRAME_EXPECT(refusedWith(writeClip(walk(), {.maximumTracks = 1}), AnimationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(writeClip(walk(), {.maximumKeys = 2}), AnimationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(writeClip(walk(), {.maximumEvents = 3}), AnimationError::OverLimit));
}

RAWFRAME_TEST(ALoopingTrackMayDrift) {
    // The hips carried a meter along z a period, and the spine a quarter
    // turn about y.
    const double kHalf = std::sqrt(0.5);
    Clip drifting = walk();
    drifting.tracks[0].drift = std::array<double, 4>{0.0, 0.0, 1.0, 0.0};
    drifting.tracks[1].drift = std::array<double, 4>{0.0, kHalf, 0.0, kHalf};
    const auto kText = writeClip(drifting);
    RAWFRAME_EXPECT(kText.has_value() && kText->contains("\"drift\": ["));
    RAWFRAME_EXPECT(kText.has_value() && readClip(*kText) == drifting);
    // Only a looping clip's translation or unit rotation drifts.
    std::vector<Clip> wrong(4, drifting);
    wrong[0].loop = Loop::Clamp;
    wrong[1].tracks[1].drift = std::array<double, 4>{0.0, 1.0, 0.0, 1.0};
    wrong[2].tracks[0].drift = std::array<double, 4>{0.0, 0.0, 1.0, 1.0};
    wrong[3].tracks[0].channel = Channel::Scale;
    for (const Clip& kClip : wrong) {
        RAWFRAME_EXPECT(refusedWith(writeClip(kClip), AnimationError::ClipInvalid));
    }
}

RAWFRAME_TEST(OnlyTheCanonicalClipTextReads) {
    const auto kText = writeClip(walk());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const auto kReplaced = [&kText](std::string_view from, std::string_view to) {
        std::string made = *kText;
        made.replace(made.find(from), from.size(), to);
        return made;
    };
    for (const std::string& kWrong :
         {kReplaced("\"duration\": 1", "\"duration\": 1.0"),
          kReplaced("\"loop\": \"loop\"", "\"loop\": \"wrap\""),
          kReplaced("\"channel\": \"rotation\"", "\"channel\": \"shear\""),
          kReplaced("\"interpolation\": \"step\"", "\"interpolation\": \"linear\""),
          kReplaced("\"interpolation\": \"step\"", "\"interpolation\": \"bezier\""),
          kReplaced("\"relevance\": \"simulation\"", "\"relevance\": \"server\""),
          kReplaced("\"event\": \"5f3a0c2d9e81b746\"", "\"event\": \"5F3A0C2D9E81B746\""),
          kReplaced("\"event\": \"5f3a0c2d9e81b746\"", "\"event\": \"5f3a0c2d9e81b74\""),
          kReplaced("52771075251e7361deaecf4939c72e56", "52771075251e7361deaecf4939c72e5"),
          kReplaced("\"name\": \"left_foot\",", "\"name\": \"left_foot\",\n      \"leader\": true,"),
          *kText + "\n"}) {
        RAWFRAME_EXPECT(!readClip(kWrong).has_value());
    }
    for (std::size_t length = 0; length < kText->size(); ++length) {
        RAWFRAME_EXPECT(!readClip(std::string_view{*kText}.substr(0, length)).has_value());
    }
    RAWFRAME_EXPECT(refusedWith(readClip(*kText, {.maximumKeys = 2}), AnimationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(readClip(*kText, {.maximumEvents = 3}), AnimationError::OverLimit));
}
