// The effects against what SPEC-0036 says they do, measured through the
// mixer: an equalizer's bands at their frequencies and nowhere else, each
// dynamics processor's static curve with its knee, makeup, and a key from
// another bus, and a reverb's pre-delay, early taps, and decay time.

#include "rawframe/audio/mixer.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace rawframe;
using namespace rawframe::audio;

namespace {

constexpr std::uint32_t kRate = 48'000;

Layout master(std::vector<Effect> effects) {
    Layout made;
    made.buses.push_back(
        Bus{.id = 1, .name = "master", .role = Role::Master, .effects = std::move(effects), .parent = 0});
    return made;
}

/// A stereo clip, both channels alike, so a centred play is heard as is.
std::shared_ptr<const Clip> sine(float hertz, float amplitude, float seconds) {
    auto clip = std::make_shared<Clip>();
    clip->channels = 2;
    const auto kFrames = static_cast<std::size_t>(seconds * kRate);
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        const float kValue = amplitude * std::sin(2.0F * std::numbers::pi_v<float> * hertz * static_cast<float>(frame) /
                                                  static_cast<float>(kRate));
        clip->samples.push_back(kValue);
        clip->samples.push_back(kValue);
    }
    return clip;
}

std::shared_ptr<const Clip> impulse() {
    auto clip = std::make_shared<Clip>();
    clip->channels = 2;
    clip->samples = {1.0F, 1.0F, 0.0F, 0.0F};
    return clip;
}

std::vector<float> render(Mixer& mixer, float seconds) {
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate) * 2);
    mixer.render(out);
    return out;
}

float decibels(float value) {
    return 20.0F * std::log10(std::max(value, 1e-9F));
}

/// The left channel's peak over the last `seconds` of `out`, in decibels.
float peakAtEnd(const std::vector<float>& out, float seconds) {
    const std::size_t kFrom = out.size() - (static_cast<std::size_t>(seconds * kRate) * 2);
    float peak = 0;
    for (std::size_t index = kFrom; index < out.size(); index += 2) {
        peak = std::max(peak, std::abs(out[index]));
    }
    return decibels(peak);
}

/// The left channel's root mean square from `from` to `to` seconds.
float rmsBetween(const std::vector<float>& out, float from, float to) {
    double power = 0;
    std::size_t count = 0;
    for (auto frame = static_cast<std::size_t>(from * kRate); frame < static_cast<std::size_t>(to * kRate); ++frame) {
        power += static_cast<double>(out[frame * 2]) * out[frame * 2];
        ++count;
    }
    return decibels(static_cast<float>(std::sqrt(power / static_cast<double>(count))));
}

/// What `effects` do to a sine of `hertz` at `amplitude`, in decibels, once
/// settled.
float change(const std::vector<Effect>& effects, float hertz, float amplitude = 0.25F) {
    const auto kMeasure = [&](std::vector<Effect> chain) {
        auto mixer = *Mixer::create(master(std::move(chain)), {});
        static_cast<void>(mixer->play(sine(hertz, amplitude, 1.0F), {.bus = 0}));
        return peakAtEnd(render(*mixer, 0.6F), 0.2F);
    };
    return kMeasure(effects) - kMeasure({});
}

bool near(float value, float expected, float tolerance) {
    const bool kNear = std::abs(value - expected) <= tolerance;
    if (!kNear) {
        std::fprintf(stderr, "  %.3f, expected %.3f within %.3f\n", value, expected, tolerance);
    }
    return kNear;
}

Effect eq(std::vector<EqBand> bands) {
    return Effect{.type = EffectType::ParametricEq, .bands = std::move(bands)};
}

Effect dynamics(Dynamics parameters) {
    return Effect{.type = EffectType::Dynamics, .dynamics = parameters};
}

Effect reverb(Reverb parameters) {
    return Effect{.type = EffectType::Reverb, .reverb = parameters};
}

} // namespace

RAWFRAME_TEST(EqualizerBandsActAtTheirFrequencies) {
    const Effect kPeak = eq({{.shape = BandShape::Peak, .frequency = 1000, .gain = 12, .q = 1}});
    RAWFRAME_EXPECT(near(change({kPeak}, 1000), 12, 0.3F));
    RAWFRAME_EXPECT(near(change({kPeak}, 60), 0, 0.5F));
    const Effect kLow = eq({{.shape = BandShape::LowShelf, .frequency = 200, .gain = -12}});
    RAWFRAME_EXPECT(near(change({kLow}, 30), -12, 1));
    RAWFRAME_EXPECT(near(change({kLow}, 5000), 0, 0.3F));
    const Effect kHigh = eq({{.shape = BandShape::HighShelf, .frequency = 4000, .gain = 6}});
    RAWFRAME_EXPECT(near(change({kHigh}, 16000), 6, 1));
    RAWFRAME_EXPECT(near(change({kHigh}, 200), 0, 0.3F));
    const Effect kNotch = eq({{.shape = BandShape::Notch, .frequency = 1000, .q = 4}});
    RAWFRAME_EXPECT(change({kNotch}, 1000) < -30);
    RAWFRAME_EXPECT(near(change({kNotch}, 250), 0, 0.3F));
    // Bands add up, in order.
    const Effect kBoth = eq({{.shape = BandShape::Peak, .frequency = 1000, .gain = 6, .q = 1},
                             {.shape = BandShape::Peak, .frequency = 1000, .gain = -6, .q = 1}});
    RAWFRAME_EXPECT(near(change({kBoth}, 1000), 0, 0.2F));
    // A bypassed equalizer does nothing.
    Effect bypassed = kPeak;
    bypassed.bypass = true;
    RAWFRAME_EXPECT(near(change({bypassed}, 1000), 0, 0.01F));
}

RAWFRAME_TEST(DynamicsFollowTheirCurves) {
    // A 0 dB sine through a 4:1 compressor at -20 dB comes out at -15 dB.
    RAWFRAME_EXPECT(near(
        change({dynamics({.processor = Processor::Compressor, .threshold = -20, .ratio = 4})}, 1000, 1.0F), -15, 1));
    // Below its threshold a compressor leaves a sound alone, but for makeup.
    RAWFRAME_EXPECT(near(
        change({dynamics({.processor = Processor::Compressor, .threshold = -20, .makeup = 6})}, 1000, 0.03F), 6, 0.3F));
    // A limiter holds a 0 dB sine at its threshold.
    const float kLimited = change({dynamics({.processor = Processor::Limiter, .threshold = -6})}, 1000, 1.0F);
    RAWFRAME_EXPECT(near(kLimited, -6, 1));
    // A knee twelve decibels wide takes a quarter of its half-width off a
    // sound at the threshold, as the quadratic has it.
    const float kKneed =
        change({dynamics({.processor = Processor::Limiter, .threshold = -12, .knee = 12})}, 1000, 0.2512F);
    RAWFRAME_EXPECT(near(kKneed, -1.5F, 0.6F));
    // A 2:1 expander at -20 dB takes a -30 dB sine to -40 dB.
    RAWFRAME_EXPECT(
        near(change({dynamics({.processor = Processor::Expander, .threshold = -20, .ratio = 2})}, 1000, 0.0316F),
             -10,
             1.5F));
    // A gate at -40 dB shuts on -50 dB and passes -30 dB.
    const Effect kGate = dynamics({.processor = Processor::Gate, .threshold = -40});
    RAWFRAME_EXPECT(change({kGate}, 1000, 0.00316F) < -60);
    RAWFRAME_EXPECT(near(change({kGate}, 1000, 0.0316F), 0, 0.3F));
    // A 2:1 upwards compressor at -20 dB raises -40 dB to -30 dB.
    RAWFRAME_EXPECT(
        near(change({dynamics({.processor = Processor::UpwardsCompressor, .threshold = -20, .ratio = 2})}, 1000, 0.01F),
             10,
             1.5F));
}

RAWFRAME_TEST(AKeyFromAnotherBusDucksABus) {
    // The music is compressed by the voice's level: quiet music under no
    // voice is left alone, and falls when the voice speaks.
    Layout layout = master({});
    layout.buses.push_back(
        Bus{.id = 2,
            .name = "music",
            .role = Role::Music,
            .effects = {dynamics({.processor = Processor::Compressor, .threshold = -30, .ratio = 10, .key = 2})},
            .parent = 0});
    layout.buses.push_back(Bus{.id = 3, .name = "voice", .role = Role::Voice, .parent = 0});
    auto mixer = *Mixer::create(layout, {});
    static_cast<void>(mixer->play(sine(500, 0.1F, 2.0F), {.bus = 1}));
    static_cast<void>(render(*mixer, 0.3F));
    const float kAlone = decibels(mixer->meter(1).peakLeft);
    static_cast<void>(mixer->play(sine(1000, 1.0F, 1.0F), {.bus = 2}));
    static_cast<void>(render(*mixer, 0.3F));
    const float kDucked = decibels(mixer->meter(1).peakLeft);
    RAWFRAME_EXPECT(near(kAlone, -20, 0.3F));
    // 0 dB is 30 over the threshold, less a tenth: 27 dB down.
    RAWFRAME_EXPECT(near(kDucked - kAlone, -27, 1.5F));
}

RAWFRAME_TEST(AReverbWaitsReflectsAndDecays) {
    const auto kImpulse = [](Reverb parameters) {
        auto mixer = *Mixer::create(master({reverb(parameters)}), {});
        static_cast<void>(mixer->play(impulse(), {.bus = 0}));
        return render(*mixer, 1.5F);
    };
    // Wet only, no early reflections: nothing before the pre-delay, then a
    // tail falling 60 dB a decay time, here a second.
    const std::vector<float> kTail = kImpulse({.decay = 1, .preDelay = 0.05F, .early = -96, .damping = 0, .mix = 1});
    const auto kBefore = static_cast<std::size_t>(0.05F * kRate) * 2;
    RAWFRAME_EXPECT(std::all_of(kTail.begin(), kTail.begin() + static_cast<std::ptrdiff_t>(kBefore), [](float value) {
        return std::abs(value) < 1e-6F;
    }));
    RAWFRAME_EXPECT(std::ranges::all_of(kTail, [](float value) {
        return std::isfinite(value);
    }));
    RAWFRAME_EXPECT(near(rmsBetween(kTail, 0.7F, 0.8F) - rmsBetween(kTail, 0.2F, 0.3F), -30, 6));
    // Twice the decay falls half as fast.
    const std::vector<float> kLonger = kImpulse({.decay = 2, .preDelay = 0.05F, .early = -96, .damping = 0, .mix = 1});
    RAWFRAME_EXPECT(near(rmsBetween(kLonger, 0.7F, 0.8F) - rmsBetween(kLonger, 0.2F, 0.3F), -15, 4));
    // The two channels' tails differ.
    bool differ = false;
    for (std::size_t index = kBefore; index + 1 < kTail.size(); index += 2) {
        differ = differ || kTail[index] != kTail[index + 1];
    }
    RAWFRAME_EXPECT(differ);

    // Early reflections only: the first sound on the left is its first tap,
    // 7.1 ms after the pre-delay.
    const std::vector<float> kEarly = kImpulse({.decay = 1, .preDelay = 0.02F, .early = 0, .late = -96, .mix = 1});
    std::size_t first = 0;
    while (first < kEarly.size() / 2 && std::abs(kEarly[first * 2]) < 0.01F) {
        ++first;
    }
    RAWFRAME_EXPECT(first == static_cast<std::size_t>(std::lround(0.02F * kRate)) +
                                 static_cast<std::size_t>(std::lround(0.0071F * kRate)));
    RAWFRAME_EXPECT(near(kEarly[first * 2], 0.84F, 0.01F));

    // Dry only, it is not there.
    RAWFRAME_EXPECT(near(change({reverb({.decay = 3, .mix = 0})}, 440), 0, 0.001F));
}
