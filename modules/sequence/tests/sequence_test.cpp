// Sequences: a timeline read and written back alike, refused where
// it is not one, and played on so each key fires once when the play
// reaches it, in a loop once for each pass.

#include "rawframe/sequence/errors.h"
#include "rawframe/sequence/sequence.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::sequence;

namespace {

constexpr std::string_view kOpening = "sequence 1\n"
                                      "duration 12\n"
                                      "track door  # the gate\n"
                                      "track hero\n"
                                      "play 0 hero 3f2a0c2d9e81b746\n"
                                      "play 4.5 door 5a1e6c0d2b9f8374\n"
                                      "event 6 5f3a0c2d9e81b746 opened\n"
                                      "play 12 hero 0000000000000000\n";

bool refuses(std::string_view text) {
    const auto kRead = parseSequence(text);
    return !kRead.has_value() && kRead.error().code() == code(SequenceError::BadSource);
}

} // namespace

RAWFRAME_TEST(ASequenceIsReadAndWrittenBackAlike) {
    const auto kRead = parseSequence(kOpening);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kRead->duration == 12 && !kRead->loop &&
                    (kRead->tracks == std::vector<std::string>{"door", "hero"}));
    RAWFRAME_EXPECT(kRead->keys.size() == 4);
    if (kRead->keys.size() != 4) {
        return;
    }
    RAWFRAME_EXPECT(kRead->keys[1].kind == KeyKind::Play && kRead->keys[1].time == 4.5 && kRead->keys[1].track == 0 &&
                    kRead->keys[1].animator == 0x5a1e6c0d2b9f8374ULL);
    RAWFRAME_EXPECT(kRead->keys[2].kind == KeyKind::Event && kRead->keys[2].event == 0x5f3a0c2d9e81b746ULL &&
                    kRead->keys[2].name == "opened");
    const std::string kWritten = writeSequence(*kRead);
    RAWFRAME_EXPECT(kWritten.find("play 12 hero 0000000000000000\n") != std::string::npos);
    const auto kAgain = parseSequence(kWritten);
    RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == *kRead && writeSequence(*kAgain) == kWritten);
}

RAWFRAME_TEST(WhatIsNotASequenceIsRefused) {
    RAWFRAME_EXPECT(refuses(""));
    RAWFRAME_EXPECT(refuses("sequence 2\nduration 1\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\ntrack a\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 0\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 1\ntrack a\ntrack a\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 1\ntrack Door\n"));
    // A key past the duration, out of order, at the end of a loop, or on
    // a track not named before it.
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 1\ntrack a\nplay 2 a 0000000000000001\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 2\ntrack a\nplay 1 a 0000000000000001\nplay 0 a 0000000000000001\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 1\nloop\ntrack a\nplay 1 a 0000000000000001\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 1\nplay 0 a 0000000000000001\ntrack a\n"));
    // An identity not of 16 lowercase hex digits, and an event's identity
    // and name that are not one to each other.
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 1\ntrack a\nplay 0 a 3F2A0C2D9E81B746\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 1\nevent 0 0000000000000001 a\nevent 0 0000000000000002 a\n"));
    RAWFRAME_EXPECT(refuses("sequence 1\nduration 1\nevent 0 0000000000000001 a\nevent 0 0000000000000001 b\n"));
    RAWFRAME_EXPECT(
        parseSequence("sequence 1\nduration 1\nevent 0 0000000000000001 a\nevent 1 0000000000000001 a\n").has_value());
    RAWFRAME_EXPECT(!parseSequence(kOpening, SequenceLimits{.mostKeys = 3}).has_value());
}

RAWFRAME_TEST(EachKeyFiresOnceAsThePlayReachesItAndAnEndHolds) {
    const auto kRead = parseSequence(kOpening);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    // The key at nought fires on the first step; 4.5 and 6 when reached.
    Advance step = advance(*kRead, 0, 0.5);
    RAWFRAME_EXPECT(step.keys == std::vector<std::size_t>{0} && step.time == 0.5 && !step.ended);
    step = advance(*kRead, 0.5, 4);
    RAWFRAME_EXPECT(step.keys.empty() && step.time == 4.5);
    step = advance(*kRead, 4.5, 2);
    RAWFRAME_EXPECT((step.keys == std::vector<std::size_t>{1, 2}));
    // The key at the end fires as the play comes to it, and then nothing.
    step = advance(*kRead, 11, 5);
    RAWFRAME_EXPECT(step.keys == std::vector<std::size_t>{3} && step.time == 12 && step.ended);
    step = advance(*kRead, 12, 1);
    RAWFRAME_EXPECT(step.keys.empty() && step.time == 12 && step.ended);
}

RAWFRAME_TEST(ALoopFiresItsKeysOnceForEachPass) {
    const auto kRead = parseSequence("sequence 1\nduration 2\nloop\nevent 0 0000000000000001 start\n"
                                     "event 1 0000000000000002 half\n");
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    Advance step = advance(*kRead, 1.5, 1);
    RAWFRAME_EXPECT(step.keys == std::vector<std::size_t>{0} && step.time == 0.5 && !step.ended);
    // Three passes and a half: each key three or four times, in order.
    step = advance(*kRead, 0.5, 7);
    RAWFRAME_EXPECT((step.keys == std::vector<std::size_t>{1, 0, 1, 0, 1, 0, 1}) && step.time == 1.5);
    // Past the most passes, the play lands where it would.
    step = advance(*kRead, 0, 1001);
    RAWFRAME_EXPECT(step.keys.size() == 128 && step.time == 1 && !step.ended);
    // A time past the end wraps, and a time before nought or a step back
    // are held.
    step = advance(*kRead, 4.5, 0.25);
    RAWFRAME_EXPECT(step.keys.empty() && step.time == 0.75);
    step = advance(*kRead, -3, -1);
    RAWFRAME_EXPECT(step.keys.empty() && step.time == 0);
}
