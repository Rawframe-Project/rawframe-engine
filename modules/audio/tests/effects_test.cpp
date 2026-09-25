// The effects against what SPEC-0036 says they do, measured through the
// mixer: an equalizer's bands at their frequencies and nowhere else, each
// dynamics processor's static curve with its knee, makeup, and a key from
// another bus, and a reverb's pre-delay, early taps, and decay time.

#include "rawframe/audio/errors.h"
#include "rawframe/audio/mixer.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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

RAWFRAME_TEST(ParametersWrittenWhilePlayingTakeEffect) {
    // A low pass opened while a 2 kHz tone plays through it.
    const Effect kLowPass{.type = EffectType::Filter, .cutoff = 200};
    auto mixer = *Mixer::create(master({kLowPass, eq({{.shape = BandShape::Peak, .frequency = 2000, .q = 1}})}), {});
    static_cast<void>(mixer->play(sine(2000, 0.5F, 2.0F), {.bus = 0}));
    const float kShut = peakAtEnd(render(*mixer, 0.2F), 0.1F);
    RAWFRAME_EXPECT(mixer->setEffectParameter(0, 0, EffectParameter::Cutoff, 20000).has_value());
    const float kOpen = peakAtEnd(render(*mixer, 0.2F), 0.1F);
    RAWFRAME_EXPECT(kShut < decibels(0.5F) - 20 && near(kOpen, decibels(0.5F), 1));
    // The equalizer's band raised by 12 dB.
    RAWFRAME_EXPECT(mixer->setEffectParameter(0, 1, EffectParameter::BandGain, 12, 0).has_value());
    RAWFRAME_EXPECT(near(peakAtEnd(render(*mixer, 0.2F), 0.1F) - kOpen, 12, 1));

    // A compressor's threshold lowered under a 0 dB tone.
    auto compressed = *Mixer::create(master({dynamics({.processor = Processor::Compressor, .ratio = 4})}), {});
    static_cast<void>(compressed->play(sine(1000, 1.0F, 2.0F), {.bus = 0}));
    RAWFRAME_EXPECT(near(peakAtEnd(render(*compressed, 0.3F), 0.1F), 0, 0.5F));
    RAWFRAME_EXPECT(compressed->setEffectParameter(0, 0, EffectParameter::Threshold, -20).has_value());
    RAWFRAME_EXPECT(near(peakAtEnd(render(*compressed, 0.5F), 0.1F), -15, 1));

    // A reverb turned dry.
    auto room = *Mixer::create(master({reverb({.decay = 2, .mix = 1})}), {});
    static_cast<void>(room->play(sine(440, 0.5F, 2.0F), {.bus = 0}));
    static_cast<void>(render(*room, 0.3F));
    RAWFRAME_EXPECT(room->setEffectParameter(0, 0, EffectParameter::Mix, 0).has_value());
    RAWFRAME_EXPECT(near(peakAtEnd(render(*room, 0.2F), 0.1F), decibels(0.5F), 0.1F));

    // A gain's level moves across one block, never in one step.
    auto gain = *Mixer::create(master({Effect{.type = EffectType::Gain}}), {});
    auto level = std::make_shared<Clip>();
    level->samples.assign(48'000, 0.5F);
    static_cast<void>(gain->play(level, {.bus = 0}));
    const std::vector<float> kBefore = render(*gain, 0.01F);
    RAWFRAME_EXPECT(gain->setEffectParameter(0, 0, EffectParameter::Level, -20).has_value());
    const std::vector<float> kRamp = render(*gain, 0.02F);
    float largestStep = std::abs(kRamp[0] - kBefore[kBefore.size() - 2]);
    for (std::size_t index = 2; index < kRamp.size(); index += 2) {
        largestStep = std::max(largestStep, std::abs(kRamp[index] - kRamp[index - 2]));
    }
    RAWFRAME_EXPECT(largestStep < 0.01F && near(kRamp.back(), 0.5F * 0.1F * std::numbers::sqrt2_v<float> / 2, 1e-3F));
}

RAWFRAME_TEST(ParameterWritesAreRefusedOutsideWhatAnEffectHas) {
    Layout layout = master({Effect{.type = EffectType::Filter, .cutoff = 200},
                            eq({{.shape = BandShape::Notch, .frequency = 50}}),
                            dynamics({.processor = Processor::Limiter}),
                            reverb({})});
    auto mixer = *Mixer::create(layout, {});
    const auto kRefused =
        [&](std::size_t bus, std::size_t effect, EffectParameter parameter, float value, std::size_t band = 0) {
            const result::Status kWritten = mixer->setEffectParameter(bus, effect, parameter, value, band);
            return !kWritten.has_value() && kWritten.error().code() == code(AudioError::BadParameter);
        };
    RAWFRAME_EXPECT(kRefused(1, 0, EffectParameter::Cutoff, 1000));
    RAWFRAME_EXPECT(kRefused(0, 4, EffectParameter::Bypass, 1));
    RAWFRAME_EXPECT(kRefused(0, 0, EffectParameter::Decay, 1));
    RAWFRAME_EXPECT(kRefused(0, 0, EffectParameter::Cutoff, 5));
    RAWFRAME_EXPECT(kRefused(0, 0, EffectParameter::Cutoff, std::numeric_limits<float>::quiet_NaN()));
    RAWFRAME_EXPECT(kRefused(0, 1, EffectParameter::BandGain, 3));
    RAWFRAME_EXPECT(kRefused(0, 1, EffectParameter::BandQ, 2, 1));
    RAWFRAME_EXPECT(kRefused(0, 2, EffectParameter::Ratio, 4));
    RAWFRAME_EXPECT(kRefused(0, 3, EffectParameter::Mix, 1.5F));
    RAWFRAME_EXPECT(mixer->setEffectParameter(0, 1, EffectParameter::BandQ, 2).has_value());
    RAWFRAME_EXPECT(mixer->setEffectParameter(0, 3, EffectParameter::Decay, 4).has_value());
    RAWFRAME_EXPECT(mixer->setEffectParameter(0, 2, EffectParameter::Bypass, 1).has_value());
    // The ranges are the documents': a delay's feedback stops short of one.
    RAWFRAME_EXPECT(rangeOf(EffectType::Delay, EffectParameter::Feedback)->highest < 1);
    RAWFRAME_EXPECT(!rangeOf(EffectType::Reverb, EffectParameter::Cutoff).has_value());
}
