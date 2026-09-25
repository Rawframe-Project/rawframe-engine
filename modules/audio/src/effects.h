#pragma once

// A bus's effects as the mix thread runs them: made with every buffer they
// will need on the owner's thread, then run without allocating, locking, or
// blocking (ADR-0038).

#include "rawframe/audio/layout.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rawframe::audio {

/// One second-order section, transposed direct form II, per channel.
struct Biquad {
    float b0 = 1;
    float b1 = 0;
    float b2 = 0;
    float a1 = 0;
    float a2 = 0;
    std::array<float, 2> z1{};
    std::array<float, 2> z2{};

    float step(float x, std::size_t channel) noexcept {
        const float kY = (b0 * x) + z1[channel];
        z1[channel] = (b1 * x) - (a1 * kY) + z2[channel];
        z2[channel] = (b2 * x) - (a2 * kY);
        return kY;
    }
};

/// A feedback comb with a low pass in its loop, one channel's.
struct Comb {
    std::vector<float> line;
    std::size_t at = 0;
    float feedback = 0;
    float held = 0;
};

/// A Schroeder all-pass, one channel's.
struct AllPass {
    std::vector<float> line;
    std::size_t at = 0;
};

class EffectRuntime {
public:
    /// Everything the effect will hold, allocated now.
    EffectRuntime(const Effect& effect, std::uint32_t rate, float shortestFade);

    /// Runs over `frames` frames of interleaved stereo in `buffer`; `key` is
    /// the keying bus's output for a dynamics effect keyed by one.
    void run(float* buffer, const float* key, std::size_t frames) noexcept;

    /// Writes a parameter the owner checked, recomputing what depends on
    /// it; allocates nothing.
    void set(EffectParameter parameter, std::size_t band, float value) noexcept;

    /// The bus a dynamics effect reads its level from, if not its own.
    [[nodiscard]] std::optional<std::size_t> key() const noexcept {
        return effect_.type == EffectType::Dynamics ? effect_.dynamics.key : std::nullopt;
    }

private:
    void runDynamics(float* buffer, const float* key, std::size_t frames) noexcept;
    void runReverb(float* buffer, std::size_t frames) noexcept;
    /// A reverb's feedbacks, tap gains, damping, diffusion, and tail level
    /// from its parameters.
    void tune() noexcept;

    Effect effect_;
    std::uint32_t rate_ = 0;
    /// A gain effect's gain as last applied, which moves to its level.
    float appliedGain_ = 1;
    /// A filter's sections, one for 12 dB an octave and two for 24; an
    /// equalizer's, one a band.
    std::vector<Biquad> sections_;
    /// A delay's lines, one a channel, and where the next sample goes.
    std::array<std::vector<float>, 2> lines_;
    std::size_t written_ = 0;
    std::array<std::size_t, 2> delayFrames_{};
    /// Dynamics: the peak held, the level followed, the gate's gain, and
    /// the coefficients.
    float held_ = 0;
    float envelope_ = 0;
    float gate_ = 0;
    float attack_ = 0;
    float release_ = 0;
    float gateStep_ = 0;
    /// Reverb: the pre-delay line (mono), its early taps, and the tail.
    std::vector<float> preDelay_;
    std::size_t preDelayFrames_ = 0;
    std::array<std::array<std::size_t, 6>, 2> tapFrames_{};
    std::array<std::array<float, 6>, 2> tapGains_{};
    std::array<std::array<Comb, 8>, 2> combs_;
    std::array<std::array<AllPass, 4>, 2> allPasses_;
    float damping_ = 0;
    float diffusion_ = 0;
    float lateGain_ = 1;
};

} // namespace rawframe::audio
