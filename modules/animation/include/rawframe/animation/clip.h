#pragma once

// Clips (SPEC-0035's `animation.clip`): tracks of keys over a duration,
// with events and sync markers, as a document in one canonical form (its
// arrays of numbers shown here on one line, where the form has one a line):
//
//   {
//     "formatVersion": 1,
//     "kind": "animation.clip",
//     "skeleton": "52771075251e7361deaecf4939c72e56",
//     "duration": 1,
//     "loop": "loop",
//     "tracks": [
//       {
//         "bone": "7e2b4d6f8091a3c5e7f9b1d3f5a7c9e1",
//         "channel": "rotation",
//         "keys": [
//           {
//             "time": 0,
//             "value": [0, 0, 0, 1]
//           },
//           {
//             "time": 0.5,
//             "value": [0, 0, 0.7071067811865476, 0.7071067811865476],
//             "interpolation": "step"
//           }
//         ]
//       }
//     ],
//     "events": [
//       {
//         "event": "5f3a0c2d9e81b746",
//         "name": "footstep",
//         "time": 0.25,
//         "relevance": "simulation"
//       }
//     ],
//     "syncMarkers": [
//       {
//         "name": "left_foot",
//         "time": 0.25
//       }
//     ]
//   }
//
// `skeleton` is the resource identity of the skeleton the clip binds to. A
// track binds a bone's channel: `translation` and `scale` are three
// numbers, `rotation` a unit quaternion (x, y, z, w). Its keys are in
// strictly increasing time within the duration, each interpolating into
// the next: `linear` when the member is left out, `step`, or, for
// `translation` and `scale`, `cubic` with the Hermite tangents `in` and
// `out` (glTF's CUBICSPLINE). A rotation turns by slerp and never by
// component, so it has no `cubic`. A looping clip wraps from its last key
// to its first, and has no key, event, or marker at its duration, which is
// its start again. A looping clip's translation or rotation track of its
// skeleton's root may declare a `drift` (D134): what one period moves it,
// added to a translation and turned onto a rotation, so the wrap runs from
// the last key to the first key moved by it. That is how a walk cycle
// carries the ground it covers to root motion while its keys stay one
// period's.
//
// A property track (D139) animates a component's field rather than a bone:
// `component` is the component's type identity (32 hex digits) and `field`
// the field's machine name, with channel `float`, for an f32 or f64 field,
// or `discrete`, for a whole-number field such as a flipbook's frame, whose
// keys are all `step` and whole numbers from nought to 4294967295. Its
// values are one number each. A clip of property tracks alone names no
// skeleton.
//
// An additive clip (D137) declares `additive`, the basis its values are
// differences from: `bind`, the skeleton's bind pose, or `first_frame`, its
// source animation's first frame, as the import that made it took them. A
// translation is an offset, a rotation the turn after the basis's, and a
// scale a factor; a bone it has no track for differs by nothing. It plays
// only as a layer of an additive node. A bone's channel has one track. Events and markers are
// in time order; events of one time fire in the order written. An event's
// `event` identifies its kind (16 lowercase hex digits) and `name` is its
// machine name, one to each other within the clip; `relevance` is
// `presentation` (clients only) or `simulation` (wherever it evaluates).

#include "rawframe/animation/skeleton.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::animation {

enum class Channel : std::uint8_t {
    Translation,
    Rotation,
    Scale,
    /// A property track's kinds.
    Float,
    Discrete,
};

/// A component's field a property track animates (SPEC-0035's property
/// binding).
struct PropertyBinding {
    /// The component's type identity.
    base::Bits128 component;
    /// The field's machine name.
    std::string field;

    friend auto operator<=>(const PropertyBinding&, const PropertyBinding&) = default;
};

enum class Interpolation : std::uint8_t {
    Linear,
    Step,
    Cubic,
};

/// A key's value and tangents use as many numbers as its channel has:
/// three, or four for a rotation.
struct Key {
    double time = 0.0;
    std::array<double, 4> value{};
    Interpolation interpolation = Interpolation::Linear;
    /// Cubic only.
    std::array<double, 4> in{};
    std::array<double, 4> out{};

    friend bool operator==(const Key&, const Key&) = default;
};

struct Track {
    /// A bone track's bone; nought for a property track.
    base::Bits128 bone;
    Channel channel = Channel::Translation;
    std::vector<Key> keys;
    /// What one period of a looping clip moves the track: a translation,
    /// or a unit rotation turned onto the first key. None is no drift.
    std::optional<std::array<double, 4>> drift;
    /// A property track's field; none for a bone track.
    std::optional<PropertyBinding> property;

    friend bool operator==(const Track&, const Track&) = default;
};

enum class Loop : std::uint8_t {
    Clamp,
    Loop,
};

/// What an additive clip's values are differences from.
enum class AdditiveBasis : std::uint8_t {
    Bind,
    FirstFrame,
};

enum class Relevance : std::uint8_t {
    Presentation,
    Simulation,
};

struct ClipEvent {
    std::uint64_t event = 0;
    std::string name;
    double time = 0.0;
    Relevance relevance = Relevance::Presentation;

    friend bool operator==(const ClipEvent&, const ClipEvent&) = default;
};

struct SyncMarker {
    std::string name;
    double time = 0.0;

    friend bool operator==(const SyncMarker&, const SyncMarker&) = default;
};

struct Clip {
    /// None for a clip that animates no bone.
    std::optional<base::Bits128> skeleton;
    double duration = 0.0;
    Loop loop = Loop::Clamp;
    /// None for a clip of poses.
    std::optional<AdditiveBasis> additive;
    std::vector<Track> tracks;
    std::vector<ClipEvent> events;
    std::vector<SyncMarker> syncMarkers;

    friend bool operator==(const Clip&, const Clip&) = default;
};

/// SPEC-0035's named limit points for clips; past one is `OverLimit`.
struct ClipLimits {
    std::size_t maximumTracks = 4096;
    std::size_t maximumKeys = 65536;
    /// Events and sync markers together.
    std::size_t maximumEvents = 4096;
};

/// How many numbers a channel's values have.
[[nodiscard]] constexpr std::size_t widthOf(Channel channel) noexcept {
    switch (channel) {
    case Channel::Rotation:
        return 4;
    case Channel::Float:
    case Channel::Discrete:
        return 1;
    case Channel::Translation:
    case Channel::Scale:
        break;
    }
    return 3;
}

/// Whether a channel is a property track's.
[[nodiscard]] constexpr bool propertyChannel(Channel channel) noexcept {
    return channel == Channel::Float || channel == Channel::Discrete;
}

/// Refuses (`ClipInvalid`) a clip out of its rules.
[[nodiscard]] result::Status validate(const Clip& clip, const ClipLimits& limits = {});

[[nodiscard]] result::Result<std::string> writeClip(const Clip& clip, const ClipLimits& limits = {});

/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<Clip> readClip(std::string_view text, const ClipLimits& limits = {});

} // namespace rawframe::animation
