#pragma once

// A sound declaration (SPEC-0036's `audio.sound` document): what plays when
// a game asks for a sound, its variants and how one is picked, the ranges a
// play draws its volume and pitch from, how it loops, where it routes, the
// voices it shares, and how it fades with distance. Parsed and checked,
// never executed.

#include "rawframe/audio/layout.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::audio {

enum class Selection : std::uint8_t {
    Sequential,
    Random,
    RandomNoImmediateRepeat
};

/// SPEC-0036's distance falloff presets, gain over distance between the
/// minimum distance (full) and the maximum (culled).
enum class Falloff : std::uint8_t {
    Inverse,
    InverseSquare,
    Linear,
    Logarithmic
};

/// What a spatial sound does with no listener to hear it.
enum class NoListener : std::uint8_t {
    Silent,
    FlatFallback
};

enum class Virtualization : std::uint8_t {
    Disabled,
    TrackPosition,
    Restart
};

/// What becomes of a playing sound whose owner goes away.
enum class Despawn : std::uint8_t {
    Stop,
    FadeOut,
    DetachToCompletion
};

struct Variant {
    /// The clip, by its path beside the declaration.
    std::string clip;
    std::uint32_t weight = 1;
};

struct Attenuation {
    float minimumDistance = 1;
    float maximumDistance = 50;
    Falloff falloff = Falloff::Inverse;
    NoListener noListener = NoListener::Silent;
};

struct SoundDeclaration {
    std::vector<Variant> variants;
    Selection selection = Selection::Sequential;
    /// Decibels and pitch ratios a play draws from, inclusive.
    float volumeMinimum = 0;
    float volumeMaximum = 0;
    float pitchMinimum = 1;
    float pitchMaximum = 1;
    bool loop = false;
    /// Loop points in seconds; both absent loops the whole variant.
    std::optional<float> loopStart;
    std::optional<float> loopEnd;
    /// The bus it plays into by default, by index in the layout.
    std::size_t bus = 0;
    /// The concurrency set it counts in, by index, if any.
    std::optional<std::size_t> concurrency;
    std::int32_t priority = 0;
    /// Absent for a flat sound: spatial ones attenuate.
    std::optional<Attenuation> attenuation;
    Virtualization virtualization = Virtualization::Disabled;
    Despawn despawn = Despawn::Stop;
};

struct SoundLimits {
    std::size_t maximumVariants = 32;
    std::int32_t maximumPriority = 1000;
};

/// Reads an `audio.sound` document of format version 1 against `layout`,
/// which its bus and concurrency set must be in. Refusals are
/// `rawframe.document` errors naming the field by its path.
[[nodiscard]] result::Result<SoundDeclaration>
readSound(std::string_view text, const Layout& layout, const SoundLimits& limits = {});

/// A spatial sound's gain at `distance` meters: one within the minimum
/// distance, nought beyond the maximum, and the falloff between.
[[nodiscard]] float attenuate(const Attenuation& attenuation, float distance) noexcept;

} // namespace rawframe::audio
