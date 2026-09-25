// The mixer against ADR-0038's validation list: levels, pans, and faders as
// declared, faders and stops that never step, sends before and after the
// fader, a filter and a delay doing what they say, finite voices, and a mix
// thread rendering while the owner plays and stops (ThreadSanitizer runs
// this in the full check).

#include "rawframe/audio/errors.h"
#include "rawframe/audio/mixer.h"
#include "rawframe/test/test.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

using namespace rawframe;
using namespace rawframe::audio;

namespace {

constexpr std::size_t kMaster = 0;
constexpr std::size_t kMusic = 1;
constexpr std::size_t kEffects = 2;
constexpr std::size_t kEchoes = 3;

/// master <- music, effects (sending to echoes before its fader), echoes.
Layout layout(std::vector<Effect> musicEffects = {}, std::vector<Effect> echoEffects = {}) {
    Layout made;
    made.buses.push_back(Bus{.id = 1, .name = "master", .role = Role::Master, .parent = 0});
    made.buses.push_back(
        Bus{.id = 2, .name = "music", .role = Role::Music, .effects = std::move(musicEffects), .parent = 0});
    made.buses.push_back(Bus{.id = 3,
                             .name = "effects",
                             .role = Role::Sfx,
                             .sends = {Send{.target = kEchoes, .level = 0, .position = SendPosition::PreFader}},
                             .parent = 0});
    made.buses.push_back(Bus{.id = 4, .name = "echoes", .effects = std::move(echoEffects), .parent = 0});
    return made;
}

std::shared_ptr<const Clip> constant(float value, std::size_t frames, std::uint32_t channels = 1) {
    auto clip = std::make_shared<Clip>();
    clip->channels = channels;
    clip->samples.assign(frames * channels, value);
    return clip;
}

std::shared_ptr<const Clip> sine(float hertz, std::size_t frames) {
    auto clip = std::make_shared<Clip>();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        clip->samples.push_back(
            std::sin(2.0F * std::numbers::pi_v<float> * hertz * static_cast<float>(frame) / 48'000.0F));
    }
    return clip;
}

std::vector<float> render(Mixer& mixer, std::size_t frames) {
    std::vector<float> out(frames * 2);
    mixer.render(out);
    return out;
}

bool near(float value, float expected, float tolerance = 1e-4F) {
    return std::abs(value - expected) <= tolerance;
}

float rmsLeft(const std::vector<float>& out, std::size_t from) {
    float power = 0;
    std::size_t count = 0;
    for (std::size_t index = from * 2; index < out.size(); index += 2) {
        power += out[index] * out[index];
        ++count;
    }
    return std::sqrt(power / static_cast<float>(count));
}

} // namespace

RAWFRAME_TEST(LevelsPansAndFadersAreAsDeclared) {
    auto mixer = *Mixer::create(layout(), {});
    // One channel at the centre: constant power, each side 1/sqrt(2).
    const auto kPlay = mixer->play(constant(0.5F, 48'000), {.bus = kMusic});
    RAWFRAME_EXPECT(kPlay.has_value());
    auto out = render(*mixer, 512);
    RAWFRAME_EXPECT(near(out[0], 0.5F / std::numbers::sqrt2_v<float>) && near(out[1], out[0]));
    // Hard right: nothing on the left.
    RAWFRAME_EXPECT(mixer->play(constant(0.25F, 48'000), {.bus = kMusic, .pan = 1}).has_value());
    out = render(*mixer, 256);
    RAWFRAME_EXPECT(near(out[0], 0.5F / std::numbers::sqrt2_v<float>) &&
                    near(out[1], (0.5F / std::numbers::sqrt2_v<float>)+0.25F));
    // The music fader down 6 dB: ramped across one block, then held.
    mixer->setBusVolume(kMusic, -6);
    out = render(*mixer, 256);
    RAWFRAME_EXPECT(near(out[0], 0.5F / std::numbers::sqrt2_v<float>));
    float largestStep = 0;
    for (std::size_t index = 2; index < out.size(); index += 2) {
        largestStep = std::max(largestStep, std::abs(out[index] - out[index - 2]));
    }
    RAWFRAME_EXPECT(largestStep < 0.002F);
    out = render(*mixer, 256);
    RAWFRAME_EXPECT(near(out[0], gainOf(-6) * 0.5F / std::numbers::sqrt2_v<float>));
    // Muted: silence after one ramped block. The meter saw the last block.
    mixer->setBusMuted(kMusic, true);
    static_cast<void>(render(*mixer, 256));
    out = render(*mixer, 256);
    RAWFRAME_EXPECT(out[0] == 0 && out[511] == 0 && mixer->meter(kMaster).peakLeft == 0);
    mixer->setBusMuted(kMusic, false);
    static_cast<void>(render(*mixer, 512));
    RAWFRAME_EXPECT(near(mixer->meter(kMusic).peakLeft, gainOf(-6) * 0.5F / std::numbers::sqrt2_v<float>) &&
                    near(mixer->meter(kMusic).rmsLeft, mixer->meter(kMusic).peakLeft));
}

RAWFRAME_TEST(PitchResamplesAndPlaybacksFinish) {
    auto mixer = *Mixer::create(layout(), {});
    // A clip of 1000 frames at an octave up lasts 500.
    auto ramp = std::make_shared<Clip>();
    for (int frame = 0; frame < 1000; ++frame) {
        ramp->samples.push_back(static_cast<float>(frame) / 1000.0F);
    }
    ramp->channels = 1;
    const auto kPlay = *mixer->play(ramp, {.bus = kMusic, .pitch = 2, .pan = -1});
    auto out = render(*mixer, 400);
    RAWFRAME_EXPECT(near(out[2 * 100], 200.0F / 1000.0F) && mixer->state(kPlay) == PlaybackState::Playing);
    out = render(*mixer, 400);
    mixer->collect();
    RAWFRAME_EXPECT(mixer->state(kPlay) == PlaybackState::Finished && out[2 * 150] == 0);
    // A looping clip plays on.
    const auto kLoop = *mixer->play(ramp, {.bus = kEffects, .loop = true});
    static_cast<void>(render(*mixer, 5000));
    mixer->collect();
    RAWFRAME_EXPECT(mixer->state(kLoop) == PlaybackState::Playing);
}

RAWFRAME_TEST(AStopFadesAndNeverClicks) {
    auto mixer = *Mixer::create(layout(), {});
    const auto kPlay = *mixer->play(constant(0.8F, 48'000), {.bus = kMusic});
    static_cast<void>(render(*mixer, 256));
    // No fade asked: the shortest fade, 5 ms at 48 kHz, 240 frames.
    mixer->stop(kPlay);
    RAWFRAME_EXPECT(mixer->state(kPlay) == PlaybackState::Stopping);
    const auto kOut = render(*mixer, 512);
    float largestStep = 0;
    for (std::size_t index = 2; index < kOut.size(); index += 2) {
        largestStep = std::max(largestStep, std::abs(kOut[index] - kOut[index - 2]));
    }
    RAWFRAME_EXPECT(largestStep < 0.01F && kOut[2 * 100] > 0 && kOut[2 * 300] == 0);
    mixer->collect();
    RAWFRAME_EXPECT(mixer->state(kPlay) == PlaybackState::Finished);
}

RAWFRAME_TEST(SendsTapBeforeOrAfterTheFader) {
    auto mixer = *Mixer::create(layout(), {});
    RAWFRAME_EXPECT(mixer->play(constant(0.5F, 48'000, 2), {.bus = kEffects}).has_value());
    auto out = render(*mixer, 256);
    // Two channels at the centre are unchanged: the bus and its send, each 0.5.
    RAWFRAME_EXPECT(near(out[0], 1.0F) && near(out[1], 1.0F));
    // Muting the bus leaves its pre-fader send.
    mixer->setBusMuted(kEffects, true);
    static_cast<void>(render(*mixer, 256));
    out = render(*mixer, 256);
    RAWFRAME_EXPECT(near(out[0], 0.5F) && near(mixer->meter(kEchoes).peakLeft, 0.5F) &&
                    mixer->meter(kEffects).peakLeft == 0);
}

RAWFRAME_TEST(AFilterAndADelayDoWhatTheySay) {
    const Effect kLowPass{.type = EffectType::Filter, .shape = FilterShape::LowPass, .cutoff = 200, .slope = 24};
    auto mixer = *Mixer::create(layout({kLowPass}), {});
    RAWFRAME_EXPECT(mixer->play(sine(5000, 48'000), {.bus = kMusic}).has_value());
    const float kHigh = rmsLeft(render(*mixer, 4800), 480);
    auto low = *Mixer::create(layout({kLowPass}), {});
    RAWFRAME_EXPECT(low->play(sine(50, 48'000), {.bus = kMusic}).has_value());
    const float kLow = rmsLeft(render(*low, 4800), 2400);
    // A sine's RMS is 1/sqrt(2); centred mono is 1/sqrt(2) more.
    RAWFRAME_EXPECT(kHigh < 0.001F && kLow > 0.45F);

    // An impulse through an echo 10 ms later, at half level, fed back half.
    const Effect kDelay{.type = EffectType::Delay, .time = 0.01F, .feedback = 0.5F, .mix = 0.5F};
    auto echo = *Mixer::create(layout({}, {kDelay}), {});
    auto impulse = std::make_shared<Clip>();
    impulse->channels = 2;
    impulse->samples = {1.0F, 1.0F};
    RAWFRAME_EXPECT(echo->play(impulse, {.bus = kEchoes}).has_value());
    const auto kOut = render(*echo, 1200);
    RAWFRAME_EXPECT(near(kOut[0], 0.5F) && near(kOut[2 * 480], 0.5F) && near(kOut[2 * 960], 0.25F) &&
                    kOut[2 * 300] == 0);
}

RAWFRAME_TEST(VoicesAndTheQueueAreFinite) {
    auto mixer = *Mixer::create(layout(), {.voices = 2, .commandQueue = 4});
    const auto kClip = constant(0.1F, 100);
    RAWFRAME_EXPECT(mixer->play(kClip, {.bus = kMusic}).has_value() && mixer->play(kClip, {.bus = kMusic}).has_value());
    const auto kThird = mixer->play(kClip, {.bus = kMusic});
    RAWFRAME_EXPECT(!kThird.has_value() && kThird.error().code() == code(AudioError::NoVoice) &&
                    mixer->statistics().voicesExhausted == 1);
    // Finished and collected, the voices are free again.
    static_cast<void>(render(*mixer, 256));
    mixer->collect();
    RAWFRAME_EXPECT(mixer->play(kClip, {.bus = kMusic}).has_value());
    // Commands wait for a render: the queue of four fills.
    for (int change = 0; change < 8; ++change) {
        mixer->setBusVolume(kMusic, -1);
    }
    RAWFRAME_EXPECT(mixer->statistics().queueFull > 0);
    RAWFRAME_EXPECT(!mixer->play(kClip, {.bus = 9}).has_value() && !mixer->play(nullptr, {}).has_value());
}

RAWFRAME_TEST(TheMixThreadRendersWhileTheOwnerPlays) {
    auto mixer = *Mixer::create(layout(), {.voices = 16});
    std::atomic<bool> done{false};
    std::thread mix([&] {
        std::vector<float> out(512);
        while (!done.load(std::memory_order_acquire)) {
            mixer->render(out);
        }
    });
    const auto kClip = constant(0.01F, 480);
    int played = 0;
    std::vector<Playback> playing;
    // Until three hundred plays went through, however slowly a loaded
    // machine schedules the mix thread, within ten seconds.
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (int round = 0; played < 300 && std::chrono::steady_clock::now() < kDeadline; ++round) {
        mixer->collect();
        if (auto playback = mixer->play(kClip, {.bus = round % 2 == 0 ? kMusic : kEffects}); playback.has_value()) {
            ++played;
            playing.push_back(*playback);
        }
        if (round % 3 == 0 && !playing.empty()) {
            mixer->stop(playing.front(), 0.001F);
            playing.erase(playing.begin());
        }
        mixer->setBusVolume(kMusic, static_cast<float>(-(round % 12)));
        std::this_thread::yield();
    }
    // Everything ends: stop what is left and let the mix thread finish it.
    for (const Playback& playback : playing) {
        mixer->stop(playback);
    }
    while (std::chrono::steady_clock::now() < kDeadline + std::chrono::seconds(10)) {
        mixer->collect();
        bool any = false;
        for (const Playback& playback : playing) {
            any = any || mixer->state(playback) != PlaybackState::Finished;
        }
        if (!any) {
            break;
        }
        std::this_thread::yield();
    }
    done.store(true, std::memory_order_release);
    mix.join();
    mixer->collect();
    bool anyLeft = false;
    for (const Playback& playback : playing) {
        anyLeft = anyLeft || mixer->state(playback) != PlaybackState::Finished;
    }
    RAWFRAME_EXPECT(played > 100 && !anyLeft);
}
