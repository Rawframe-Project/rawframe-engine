#include "rawframe/audio/errors.h"
#include "rawframe/audio_import/import.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <vector>

namespace rawframe::audio_import {

namespace {

/// The filter: a Kaiser-windowed sinc reaching this many zero crossings of
/// its cutoff each side, with a window shape that holds its sidelobes near
/// -90 dB, cutting at this share of the lower rate's Nyquist.
constexpr double kZeroCrossings = 32;
constexpr double kKaiserBeta = 9.0;
constexpr double kPassband = 0.95;
/// The most phases the filter keeps: rates whose ratio needs more are
/// refused rather than approximated.
constexpr std::uint64_t kMostPhases = 48'000;

/// The zeroth-order modified Bessel function, by its series.
double besselZero(double x) noexcept {
    double sum = 1;
    double term = 1;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-17) {
            break;
        }
    }
    return sum;
}

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, audio::kAudioDomain, code(audio::AudioError::BadSound), why)
            .error()};
}

} // namespace

result::Result<audio::Clip> resample(const audio::Clip& clip, std::uint32_t rate) {
    if (clip.rate < 8'000 || clip.rate > 192'000 || rate < 8'000 || rate > 192'000 || clip.channels == 0) {
        return refuse("resampling is between rates of 8 to 192 kHz");
    }
    if (clip.rate == rate) {
        return clip;
    }
    // Output frame n falls at input frame n * step / phases exactly: a
    // rational ratio, so every phase's taps are computed once.
    const std::uint64_t kCommon = std::gcd<std::uint64_t>(clip.rate, rate);
    const std::uint64_t kPhases = rate / kCommon;
    const std::uint64_t kStep = clip.rate / kCommon;
    if (kPhases > kMostPhases) {
        return refuse("the rates' ratio needs more filter phases than allowed");
    }
    const double kIn = clip.rate;
    // The cutoff, in cycles per input frame, and the window's half-length
    // in input frames.
    const double kCutoff = 0.5 * kPassband * std::min<double>(clip.rate, rate) / kIn;
    const double kHalf = kZeroCrossings / (2.0 * kCutoff);
    const auto kReach = static_cast<std::int64_t>(std::ceil(kHalf));
    const auto kTaps = static_cast<std::size_t>(2 * kReach);
    const double kWindowScale = besselZero(kKaiserBeta);

    // Taps a phase, for input frames base - reach + 1 to base + reach, each
    // phase's taps summing to one so a constant stays constant.
    std::vector<float> taps(static_cast<std::size_t>(kPhases) * kTaps);
    for (std::uint64_t phase = 0; phase < kPhases; ++phase) {
        const double kOffset = static_cast<double>(phase) / static_cast<double>(kPhases);
        double sum = 0;
        std::vector<double> row(kTaps);
        for (std::size_t tap = 0; tap < kTaps; ++tap) {
            // Distance from the output's instant to this input frame.
            const double kDistance = kOffset + static_cast<double>(kReach - 1) - static_cast<double>(tap);
            const double kRatio = kDistance / kHalf;
            if (std::abs(kRatio) >= 1) {
                continue;
            }
            const double kArgument = 2.0 * kCutoff * kDistance;
            const double kSinc =
                kArgument == 0 ? 1.0 : std::sin(std::numbers::pi * kArgument) / (std::numbers::pi * kArgument);
            const double kWindow = besselZero(kKaiserBeta * std::sqrt(1.0 - (kRatio * kRatio))) / kWindowScale;
            row[tap] = 2.0 * kCutoff * kSinc * kWindow;
            sum += row[tap];
        }
        for (std::size_t tap = 0; tap < kTaps; ++tap) {
            taps[(phase * kTaps) + tap] = static_cast<float>(row[tap] / sum);
        }
    }

    const auto kFrames = static_cast<std::int64_t>(clip.frames());
    const std::uint64_t kOut = ((static_cast<std::uint64_t>(kFrames) * kPhases) + kStep - 1) / kStep;
    audio::Clip made;
    made.channels = clip.channels;
    made.rate = rate;
    made.samples.assign(static_cast<std::size_t>(kOut) * clip.channels, 0.0F);
    for (std::uint64_t frame = 0; frame < kOut; ++frame) {
        const std::uint64_t kPosition = frame * kStep;
        const auto kBase = static_cast<std::int64_t>(kPosition / kPhases);
        const float* const kRow = &taps[(kPosition % kPhases) * kTaps];
        const std::int64_t kFirst = kBase - kReach + 1;
        for (std::uint32_t channel = 0; channel < clip.channels; ++channel) {
            double sum = 0;
            for (std::size_t tap = 0; tap < kTaps; ++tap) {
                const std::int64_t kInput = kFirst + static_cast<std::int64_t>(tap);
                if (kInput >= 0 && kInput < kFrames) {
                    sum += static_cast<double>(kRow[tap]) *
                           clip.samples[(static_cast<std::size_t>(kInput) * clip.channels) + channel];
                }
            }
            made.samples[(static_cast<std::size_t>(frame) * clip.channels) + channel] = static_cast<float>(sum);
        }
    }
    return made;
}

} // namespace rawframe::audio_import
