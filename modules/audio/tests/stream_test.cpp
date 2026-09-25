// Streams against ADR-0038: a cooked Opus sound played from a bounded ring
// sounds exactly as the same sound decoded whole; a ring run dry is silence,
// counted, and loses nothing; a looping stream joins its end to its start
// without a seam; and an executor's worker keeps the ring filled while the
// mix renders (ThreadSanitizer runs this in the full check). The fixture is
// a second and a half of 440 Hz at half scale on the left and 660 Hz at a
// quarter on the right, cooked by rawframe-import.

#include "rawframe/audio/errors.h"
#include "rawframe/audio/stream.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

using namespace rawframe;
using namespace rawframe::audio;

namespace {

constexpr std::size_t kBlock = 256;

std::shared_ptr<const std::vector<std::byte>> fixture() {
    std::ifstream file{std::string{RAWFRAME_AUDIO_DATA} + "tones.rfopus", std::ios::binary};
    const std::vector<char> kRead{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    auto bytes = std::make_shared<std::vector<std::byte>>(kRead.size());
    std::ranges::transform(kRead, bytes->begin(), [](char each) {
        return static_cast<std::byte>(each);
    });
    return bytes;
}

Layout master() {
    Layout made;
    made.buses.push_back(Bus{.id = 1, .name = "master", .role = Role::Master, .parent = 0});
    return made;
}

/// Renders `frames` frames in blocks, calling `between` before each.
template <typename Between> std::vector<float> renderIn(Mixer& mixer, std::size_t frames, Between between) {
    std::vector<float> out(frames * 2);
    for (std::size_t at = 0; at < frames; at += kBlock) {
        between();
        mixer.render(std::span{out}.subspan(at * 2, std::min(kBlock, frames - at) * 2));
    }
    return out;
}

/// The same sound decoded whole and played as a clip.
std::vector<float> wholeReference(std::size_t frames) {
    auto clip = std::make_shared<Clip>(*decodeCookedOpus(*fixture()));
    auto mixer = *Mixer::create(master(), {});
    static_cast<void>(mixer->play(clip, {.bus = 0}));
    return renderIn(*mixer, frames, [] {});
}

} // namespace

RAWFRAME_TEST(AStreamSoundsAsTheWholeSound) {
    auto stream = *Stream::open(fixture(), {.bufferFrames = 12'000});
    RAWFRAME_EXPECT(stream->channels() == 2 && stream->rate() == 48'000 && stream->frames() == 72'000);
    auto mixer = *Mixer::create(master(), {});
    const auto kPlayback = mixer->play(stream, {.bus = 0});
    RAWFRAME_EXPECT(kPlayback.has_value());
    // A stream plays in one voice at a time.
    RAWFRAME_EXPECT(!mixer->play(stream, {.bus = 0}).has_value());
    const std::vector<float> kStreamed = renderIn(*mixer, 80'000, [&] {
        stream->decodeAhead();
    });
    RAWFRAME_EXPECT(kStreamed == wholeReference(80'000));
    RAWFRAME_EXPECT(stream->statistics().underrunFrames == 0 && stream->statistics().decodeErrors == 0);
    // It ends when its frames do, and its voice comes back.
    mixer->collect();
    RAWFRAME_EXPECT(kPlayback.has_value() && mixer->state(*kPlayback) == PlaybackState::Finished);
    RAWFRAME_EXPECT(stream->finished());
}

RAWFRAME_TEST(ADryRingIsSilenceAndLosesNothing) {
    auto stream = *Stream::open(fixture(), {.bufferFrames = 12'000});
    auto mixer = *Mixer::create(master(), {});
    static_cast<void>(mixer->play(stream, {.bus = 0}));
    // Nothing decoded yet: silence, every frame counted.
    const std::vector<float> kDry = renderIn(*mixer, 1'024, [] {});
    RAWFRAME_EXPECT(std::ranges::all_of(kDry, [](float value) {
        return value == 0;
    }));
    RAWFRAME_EXPECT(stream->statistics().underrunFrames == 1'024);
    // Then the sound from its first frame.
    const std::vector<float> kLater = renderIn(*mixer, 20'000, [&] {
        stream->decodeAhead();
    });
    RAWFRAME_EXPECT(kLater == wholeReference(20'000));
}

RAWFRAME_TEST(ALoopingStreamHasNoSeam) {
    auto stream = *Stream::open(fixture(), {.bufferFrames = 12'000, .loop = true});
    auto mixer = *Mixer::create(master(), {});
    const auto kPlayback = mixer->play(stream, {.bus = 0});
    const std::vector<float> kOut = renderIn(*mixer, 180'000, [&] {
        stream->decodeAhead();
    });
    // The second time through is the first, sample for sample, across the
    // join.
    bool same = true;
    for (std::size_t frame = 71'000; frame < 74'000; ++frame) {
        same = same && kOut[(frame + 72'000) * 2] == kOut[frame * 2];
    }
    RAWFRAME_EXPECT(same);
    const std::vector<float> kReference = wholeReference(72'000);
    RAWFRAME_EXPECT(std::equal(kReference.begin(), kReference.end(), kOut.begin() + (72'000 * 2)));
    mixer->collect();
    RAWFRAME_EXPECT(kPlayback.has_value() && mixer->state(*kPlayback) == PlaybackState::Playing);
    RAWFRAME_EXPECT(stream->statistics().underrunFrames == 0);
}

RAWFRAME_TEST(AnExecutorKeepsTheRingFilled) {
    execution::Executor executor{execution::ExecutorSettings{.kind = execution::ExecutorKind::Cpu, .workers = 1}};
    const execution::OwnerId kOwner{7};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, {.maximumPendingTasks = 4}).has_value());
    Streamer streamer{executor, kOwner};
    auto stream = *Stream::open(fixture(), {.bufferFrames = 12'000});
    streamer.add(stream);
    auto mixer = *Mixer::create(master(), {});
    static_cast<void>(mixer->play(stream, {.bus = 0}));
    // As a frame loop would: ask for decoding, and render once the worker
    // has kept up, however slowly a loaded machine runs it.
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const std::vector<float> kOut = renderIn(*mixer, 80'000, [&] {
        streamer.update();
        while ((stream->available() < kBlock * 2 && !stream->ended()) && std::chrono::steady_clock::now() < kDeadline) {
            std::this_thread::yield();
            streamer.update();
        }
    });
    RAWFRAME_EXPECT(kOut == wholeReference(80'000));
    RAWFRAME_EXPECT(stream->statistics().underrunFrames == 0 && streamer.refused() == 0);
    executor.stop();
}

RAWFRAME_TEST(StreamsAreRefusedWhenTheyCannotPlay) {
    RAWFRAME_EXPECT(!Stream::open(nullptr).has_value());
    RAWFRAME_EXPECT(!Stream::open(fixture(), {.bufferFrames = 1'000}).has_value());
    auto broken = std::make_shared<std::vector<std::byte>>(*fixture());
    (*broken)[0] = std::byte{'X'};
    RAWFRAME_EXPECT(!Stream::open(broken).has_value());
    auto stream = *Stream::open(fixture());
    auto mixer = *Mixer::create(master(), {});
    const auto kLooped = mixer->play(stream, {.bus = 0, .loop = true});
    RAWFRAME_EXPECT(!kLooped.has_value() && kLooped.error().code() == code(AudioError::BadPlay));
    RAWFRAME_EXPECT(!mixer->play(stream, {.bus = 0, .startFrame = 10}).has_value());
    RAWFRAME_EXPECT(!mixer->play(std::shared_ptr<Stream>{}, {.bus = 0}).has_value());
}
