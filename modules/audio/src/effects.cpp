#include "effects.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rawframe::audio {

namespace {

/// The reverb's tail after Freeverb (Jezar at Dreampoint, public domain):
/// eight combs and four all-passes a channel, tuned at 44.1 kHz, the right
/// channel's a little longer so the two differ.
constexpr std::array<std::size_t, 8> kCombTuning = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<std::size_t, 4> kAllPassTuning = {556, 441, 341, 225};
constexpr std::size_t kStereoSpread = 23;
constexpr float kTuningRate = 44'100;
/// What goes into the combs, and what comes out of the tail, so an impulse's
/// tail sits near the level of what made it.
constexpr float kTailInput = 0.015F;
constexpr float kTailOutput = 3.0F;
/// Freeverb's widest damping and an all-pass's strongest feedback.
constexpr float kMostDamping = 0.4F;
constexpr float kMostDiffusion = 0.7F;
/// Early reflections: six taps a channel, in milliseconds after the
/// pre-delay, falling away.
constexpr std::array<std::array<float, 6>, 2> kTapTimes = {{
    {7.1F, 11.3F, 17.9F, 23.3F, 31.7F, 41.3F},
    {8.3F, 13.7F, 19.1F, 27.1F, 35.3F, 44.9F},
}};
constexpr std::array<std::array<float, 6>, 2> kTapGains = {{
    {0.84F, 0.71F, 0.58F, 0.47F, 0.38F, 0.30F},
    {0.80F, 0.66F, 0.55F, 0.44F, 0.36F, 0.28F},
}};

/// Levels below this, in decibels, are silence to a dynamics processor.
constexpr float kFloor = -100;
/// An upwards compressor never raises what lies below 16-bit sound's floor,
/// nor by more than the loudest level a layout may set.
constexpr float kQuietest = -96;
constexpr float kMostBoost = 24;

/// Nought for a value too small to hear, so a dying tail never runs on in
/// denormal numbers, which cost many times a normal one's work.
float flushed(float value) noexcept {
    return std::abs(value) < 1e-15F ? 0.0F : value;
}

float decibelsOf(float gain) noexcept {
    return std::max(kFloor, 20.0F * std::log10(std::max(gain, 1e-5F)));
}

Biquad normalized(Biquad made, float a0) noexcept {
    made.b0 /= a0;
    made.b1 /= a0;
    made.b2 /= a0;
    made.a1 /= a0;
    made.a2 /= a0;
    return made;
}

/// Robert Bristow-Johnson's cookbook sections.
Biquad designFilter(const Effect& effect, std::uint32_t rate) noexcept {
    const float kCutoff = std::min(effect.cutoff, 0.45F * static_cast<float>(rate));
    const float kW0 = 2.0F * std::numbers::pi_v<float> * kCutoff / static_cast<float>(rate);
    const float kCos = std::cos(kW0);
    const float kAlpha = std::sin(kW0) / (2.0F * effect.resonance);
    Biquad made;
    switch (effect.shape) {
    case FilterShape::LowPass:
        made.b0 = (1 - kCos) / 2;
        made.b1 = 1 - kCos;
        made.b2 = (1 - kCos) / 2;
        break;
    case FilterShape::HighPass:
        made.b0 = (1 + kCos) / 2;
        made.b1 = -(1 + kCos);
        made.b2 = (1 + kCos) / 2;
        break;
    case FilterShape::BandPass:
        made.b0 = kAlpha;
        made.b1 = 0;
        made.b2 = -kAlpha;
        break;
    }
    made.a1 = -2 * kCos;
    made.a2 = 1 - kAlpha;
    return normalized(made, 1 + kAlpha);
}

Biquad designBand(const EqBand& band, std::uint32_t rate) noexcept {
    const float kFrequency = std::min(band.frequency, 0.45F * static_cast<float>(rate));
    const float kW0 = 2.0F * std::numbers::pi_v<float> * kFrequency / static_cast<float>(rate);
    const float kCos = std::cos(kW0);
    const float kAlpha = std::sin(kW0) / (2.0F * band.q);
    const float kA = std::pow(10.0F, band.gain / 40.0F);
    const float kShelf = 2.0F * std::sqrt(kA) * kAlpha;
    Biquad made;
    switch (band.shape) {
    case BandShape::Peak:
        made.b0 = 1 + (kAlpha * kA);
        made.b1 = -2 * kCos;
        made.b2 = 1 - (kAlpha * kA);
        made.a1 = -2 * kCos;
        made.a2 = 1 - (kAlpha / kA);
        return normalized(made, 1 + (kAlpha / kA));
    case BandShape::Notch:
        made.b0 = 1;
        made.b1 = -2 * kCos;
        made.b2 = 1;
        made.a1 = -2 * kCos;
        made.a2 = 1 - kAlpha;
        return normalized(made, 1 + kAlpha);
    case BandShape::LowShelf:
        made.b0 = kA * ((kA + 1) - ((kA - 1) * kCos) + kShelf);
        made.b1 = 2 * kA * ((kA - 1) - ((kA + 1) * kCos));
        made.b2 = kA * ((kA + 1) - ((kA - 1) * kCos) - kShelf);
        made.a1 = -2 * ((kA - 1) + ((kA + 1) * kCos));
        made.a2 = (kA + 1) + ((kA - 1) * kCos) - kShelf;
        return normalized(made, (kA + 1) + ((kA - 1) * kCos) + kShelf);
    case BandShape::HighShelf:
        made.b0 = kA * ((kA + 1) + ((kA - 1) * kCos) + kShelf);
        made.b1 = -2 * kA * ((kA - 1) + ((kA + 1) * kCos));
        made.b2 = kA * ((kA + 1) + ((kA - 1) * kCos) - kShelf);
        made.a1 = 2 * ((kA - 1) - ((kA + 1) * kCos));
        made.a2 = (kA + 1) - ((kA - 1) * kCos) - kShelf;
        return normalized(made, (kA + 1) - ((kA - 1) * kCos) + kShelf);
    }
    return made;
}

/// The change, in decibels, a static curve makes to a level of `level`
/// decibels: slope `slope` above the threshold, or below it, with a
/// quadratic knee `knee` decibels wide centred on it.
float above(float level, float threshold, float knee, float slope) noexcept {
    const float kOver = level - threshold;
    if (2 * kOver < -knee) {
        return 0;
    }
    if (knee > 0 && 2 * std::abs(kOver) <= knee) {
        const float kInto = kOver + (knee / 2);
        return (slope - 1) * kInto * kInto / (2 * knee);
    }
    return (slope - 1) * kOver;
}

float below(float level, float threshold, float knee, float slope) noexcept {
    const float kOver = level - threshold;
    if (2 * kOver > knee) {
        return 0;
    }
    if (knee > 0 && 2 * std::abs(kOver) <= knee) {
        const float kInto = kOver - (knee / 2);
        return -(slope - 1) * kInto * kInto / (2 * knee);
    }
    return (slope - 1) * kOver;
}

float coefficient(float seconds, std::uint32_t rate) noexcept {
    return std::exp(-1.0F / (seconds * static_cast<float>(rate)));
}

std::size_t framesOf(float seconds, std::uint32_t rate) noexcept {
    return static_cast<std::size_t>(std::lround(seconds * static_cast<float>(rate)));
}

} // namespace

EffectRuntime::EffectRuntime(const Effect& effect, std::uint32_t rate, float shortestFade) : effect_(effect) {
    switch (effect.type) {
    case EffectType::Gain:
        break;
    case EffectType::Filter:
        sections_.assign(effect.slope == 24 ? 2 : 1, designFilter(effect, rate));
        break;
    case EffectType::Delay:
        delayFrames_ = {framesOf(effect.time, rate), framesOf(effect.time + effect.offset, rate)};
        for (std::vector<float>& line : lines_) {
            line.assign(framesOf(effect.time + effect.offset, rate) + 1, 0.0F);
        }
        break;
    case EffectType::ParametricEq:
        for (const EqBand& band : effect.bands) {
            sections_.push_back(designBand(band, rate));
        }
        break;
    case EffectType::Dynamics:
        attack_ = coefficient(effect.dynamics.attack, rate);
        release_ = coefficient(effect.dynamics.release, rate);
        gateStep_ = 1.0F - coefficient(shortestFade, rate);
        break;
    case EffectType::Reverb: {
        const Reverb& reverb = effect.reverb;
        const float kScale = static_cast<float>(rate) / kTuningRate;
        const float kSpacing = 2.0F - reverb.density;
        std::size_t longestTap = 0;
        for (std::size_t channel = 0; channel < 2; ++channel) {
            for (std::size_t index = 0; index < kCombTuning.size(); ++index) {
                Comb& comb = combs_[channel][index];
                const auto kLength = std::max<std::size_t>(
                    1,
                    static_cast<std::size_t>(std::lround(
                        static_cast<float>(kCombTuning[index] + (channel * kStereoSpread)) * kScale * kSpacing)));
                comb.line.assign(kLength, 0.0F);
                // Each comb falls 60 dB in the decay time, whatever its length.
                comb.feedback =
                    std::pow(10.0F, -3.0F * static_cast<float>(kLength) / (static_cast<float>(rate) * reverb.decay));
            }
            for (std::size_t index = 0; index < kAllPassTuning.size(); ++index) {
                const auto kLength = std::max<std::size_t>(
                    1,
                    static_cast<std::size_t>(
                        std::lround(static_cast<float>(kAllPassTuning[index] + (channel * kStereoSpread)) * kScale)));
                allPasses_[channel][index].line.assign(kLength, 0.0F);
            }
            for (std::size_t tap = 0; tap < kTapTimes[channel].size(); ++tap) {
                tapFrames_[channel][tap] = framesOf(kTapTimes[channel][tap] / 1000.0F, rate);
                tapGains_[channel][tap] = kTapGains[channel][tap] * gainOf(reverb.early);
                longestTap = std::max(longestTap, tapFrames_[channel][tap]);
            }
        }
        preDelayFrames_ = framesOf(reverb.preDelay, rate);
        preDelay_.assign(preDelayFrames_ + longestTap + 1, 0.0F);
        damping_ = kMostDamping * reverb.damping;
        diffusion_ = kMostDiffusion * reverb.diffusion;
        lateGain_ = kTailOutput * gainOf(reverb.late);
        break;
    }
    }
}

void EffectRuntime::run(float* buffer, const float* key, std::size_t frames) noexcept {
    const Effect& effect = effect_;
    if (effect.bypass) {
        return;
    }
    switch (effect.type) {
    case EffectType::Gain: {
        const float kGain = gainOf(effect.level);
        for (std::size_t index = 0; index < frames * 2; ++index) {
            buffer[index] *= kGain;
        }
        break;
    }
    case EffectType::Filter:
    case EffectType::ParametricEq:
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                float value = buffer[(frame * 2) + channel];
                for (Biquad& section : sections_) {
                    value = section.step(value, channel);
                }
                buffer[(frame * 2) + channel] = value;
            }
        }
        break;
    case EffectType::Delay: {
        const std::size_t kLength = lines_[0].size();
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                std::vector<float>& line = lines_[channel];
                const std::size_t kRead = (written_ + kLength - delayFrames_[channel]) % kLength;
                const float kDelayed = line[kRead];
                const float kDry = buffer[(frame * 2) + channel];
                line[written_] = kDry + (kDelayed * effect.feedback);
                buffer[(frame * 2) + channel] = (kDry * (1 - effect.mix)) + (kDelayed * effect.mix);
            }
            written_ = (written_ + 1) % kLength;
        }
        break;
    }
    case EffectType::Dynamics:
        runDynamics(buffer, key, frames);
        break;
    case EffectType::Reverb:
        runReverb(buffer, frames);
        break;
    }
}

void EffectRuntime::runDynamics(float* buffer, const float* key, std::size_t frames) noexcept {
    const Dynamics& dynamics = effect_.dynamics;
    const float* const kHeard = key != nullptr ? key : buffer;
    const float kMakeup = gainOf(dynamics.makeup);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        // Both channels' peak, so the two move together, through a smooth
        // decoupled peak detector (Giannoulis, Massberg, and Reiss, 2012):
        // the peak is held and falls at the release, and the level follows
        // it at the attack, so a steady tone reads at its peak.
        const float kPeak = std::max(std::abs(kHeard[frame * 2]), std::abs(kHeard[(frame * 2) + 1]));
        held_ = std::max(kPeak, (release_ * held_) + ((1 - release_) * kPeak));
        envelope_ = (attack_ * envelope_) + ((1 - attack_) * held_);
        const float kLevel = decibelsOf(envelope_);
        float gain = kMakeup;
        switch (dynamics.processor) {
        case Processor::Compressor:
            gain *= gainOf(above(kLevel, dynamics.threshold, dynamics.knee, 1.0F / dynamics.ratio));
            break;
        case Processor::Limiter:
            gain *= gainOf(above(kLevel, dynamics.threshold, dynamics.knee, 0.0F));
            break;
        case Processor::Expander:
            gain *= gainOf(below(kLevel, dynamics.threshold, dynamics.knee, dynamics.ratio));
            break;
        case Processor::UpwardsCompressor: {
            const float kBoost = below(kLevel, dynamics.threshold, dynamics.knee, 1.0F / dynamics.ratio);
            gain *= gainOf(std::min({kBoost, kMostBoost, std::max(0.0F, kLevel - kQuietest)}));
            break;
        }
        case Processor::Gate: {
            // Open or shut, moving over the mixer's shortest fade so it
            // never clicks.
            const float kOpen = kLevel >= dynamics.threshold ? 1.0F : 0.0F;
            gate_ += (kOpen - gate_) * gateStep_;
            gain *= gate_;
            break;
        }
        }
        buffer[frame * 2] *= gain;
        buffer[(frame * 2) + 1] *= gain;
    }
}

void EffectRuntime::runReverb(float* buffer, std::size_t frames) noexcept {
    const Reverb& reverb = effect_.reverb;
    const std::size_t kLength = preDelay_.size();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        preDelay_[written_] = (buffer[frame * 2] + buffer[(frame * 2) + 1]) * 0.5F;
        const auto kAgo = [this, kLength](std::size_t delay) {
            return preDelay_[(written_ + kLength - delay) % kLength];
        };
        const float kInput = kAgo(preDelayFrames_) * kTailInput;
        for (std::size_t channel = 0; channel < 2; ++channel) {
            float early = 0;
            for (std::size_t tap = 0; tap < tapFrames_[channel].size(); ++tap) {
                early += tapGains_[channel][tap] * kAgo(preDelayFrames_ + tapFrames_[channel][tap]);
            }
            float late = 0;
            for (Comb& comb : combs_[channel]) {
                const float kOut = comb.line[comb.at];
                comb.held = flushed((kOut * (1 - damping_)) + (comb.held * damping_));
                comb.line[comb.at] = kInput + (comb.held * comb.feedback);
                comb.at = (comb.at + 1) % comb.line.size();
                late += kOut;
            }
            for (AllPass& pass : allPasses_[channel]) {
                const float kHeld = pass.line[pass.at];
                pass.line[pass.at] = flushed(late + (kHeld * diffusion_));
                pass.at = (pass.at + 1) % pass.line.size();
                late = kHeld - late;
            }
            float& sample = buffer[(frame * 2) + channel];
            sample = (sample * (1 - reverb.mix)) + ((early + (late * lateGain_)) * reverb.mix);
        }
        written_ = (written_ + 1) % kLength;
    }
}

} // namespace rawframe::audio
