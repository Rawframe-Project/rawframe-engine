#pragma once

// A mixer layout (SPEC-0036's `audio.mixer` document): the one tree of buses
// with sends that routes all sound, and the concurrency sets sounds share
// voices in. Parsed and checked, never executed.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::audio {

/// The closed set of roles a bus carries, so that volume settings bind to a
/// role and never to a name.
enum class Role : std::uint8_t {
    None,
    Master,
    Music,
    Sfx,
    Voice,
    Ui,
    Ambience
};

enum class FilterShape : std::uint8_t {
    LowPass,
    HighPass,
    BandPass
};

enum class EffectType : std::uint8_t {
    Gain,
    Filter,
    Delay
};

/// One effect of a bus's chain, with the parameters of its type.
struct Effect {
    EffectType type = EffectType::Gain;
    bool bypass = false;
    /// gain: its level in decibels.
    float level = 0;
    /// filter: shape, cutoff in hertz, resonance as Q, slope 12 or 24 dB an
    /// octave.
    FilterShape shape = FilterShape::LowPass;
    float cutoff = 0;
    float resonance = 0.70710678F;
    std::uint8_t slope = 12;
    /// delay: time in seconds, feedback, wet share (0 dry, 1 wet), and the
    /// right channel's extra time.
    float time = 0;
    float feedback = 0;
    float mix = 0.5F;
    float offset = 0;
};

enum class SendPosition : std::uint8_t {
    PostFader,
    PreFader
};

struct Send {
    /// The index of the target bus.
    std::size_t target = 0;
    float level = 0;
    SendPosition position = SendPosition::PostFader;
};

struct Bus {
    std::uint64_t id = 0;
    std::string name;
    Role role = Role::None;
    /// Its fader, in decibels.
    float volume = 0;
    bool muted = false;
    std::vector<Effect> effects;
    std::vector<Send> sends;
    /// The index of its parent; the master's is its own.
    std::size_t parent = 0;
};

/// Who yields when a concurrency set is full (SPEC-0036's closed set).
enum class Resolution : std::uint8_t {
    StopFarthestThenOldest,
    PreventNew,
    StopOldest,
    StopQuietest,
    StopLowestPriorityThenOldest,
};

struct ConcurrencySet {
    std::string name;
    std::size_t maximumInstances = 1;
    Resolution resolution = Resolution::StopFarthestThenOldest;
};

struct Layout {
    /// Every bus, the master first and each parent before its children.
    std::vector<Bus> buses;
    std::vector<ConcurrencySet> concurrency;

    [[nodiscard]] std::optional<std::size_t> busWithId(std::uint64_t id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> busWithRole(Role role) const noexcept;
    /// An order to mix the buses in: every bus after each bus that feeds
    /// it, through its children or their sends.
    [[nodiscard]] std::vector<std::size_t> mixOrder() const;
};

/// SPEC-0036's named limit points for a layout. The product's to set; these
/// are generation 1's.
struct LayoutLimits {
    std::size_t maximumBuses = 64;
    std::size_t maximumDepth = 16;
    std::size_t maximumEffectsPerBus = 8;
    std::size_t maximumSendsPerBus = 8;
    std::size_t maximumConcurrencySets = 64;
    std::size_t maximumNameLength = 64;
};

/// Reads an `audio.mixer` document of format version 1 in SPEC-0036's
/// order. Refusals are `rawframe.document` errors naming the field by its
/// path.
[[nodiscard]] result::Result<Layout> readLayout(std::string_view text, const LayoutLimits& limits = {});

/// Decibels as a linear gain.
[[nodiscard]] float gainOf(float decibels) noexcept;

} // namespace rawframe::audio
