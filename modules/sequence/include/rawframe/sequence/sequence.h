#pragma once

// A sequence document: a timeline of keys,
// as text. Each track is one entity the game casts in it; a `play` key
// starts that entity on one of the game's animators, by the identity its
// `animator` line gives it, and an `event` key fires one of the
// sequence's events for the game to read. How an entity moves between
// keys is its animator's clips: a sequence
// holds no values of its own.
//
//     sequence 1
//     duration 12
//     loop
//     track door
//     track hero
//     play 0 hero 3f2a0c2d9e81b746
//     play 4.5 door 5a1e6c0d2b9f8374
//     event 6 5f3a0c2d9e81b746 opened
//
// `duration` is in seconds; a sequence that loops starts again at it, so it
// has no key there. `track` lines name the tracks, the first track nought.
// Keys are in time order, from nought; keys of one time fire in the order
// written. A `play` of animator nought stops the entity's animator. An
// event's identity is 16 lowercase hex digits and its name its machine
// name, both one to each other within the sequence. `#` begins a comment.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::sequence {

/// The resource a sequence is cooked into: its text, checked, as it
/// was written, which a game reads as `parseSequence` reads it.
inline constexpr base::Bits128 kSequenceType = base::parseBits128Hex("ad38f30cac629148c98553ed28b0a8b6").value;
inline constexpr std::string_view kSequenceRepresentation = "rawframe.sequence.text";

enum class KeyKind : std::uint8_t {
    Play,
    Event,
};

struct Key {
    double time = 0;
    KeyKind kind = KeyKind::Play;
    /// A play key's track, by its place, and the animator it starts.
    std::uint32_t track = 0;
    std::uint64_t animator = 0;
    /// An event key's identity and machine name.
    std::uint64_t event = 0;
    std::string name;

    friend bool operator==(const Key&, const Key&) = default;
};

struct Sequence {
    double duration = 0;
    bool loop = false;
    /// Each track's machine name, in order.
    std::vector<std::string> tracks;
    std::vector<Key> keys;

    friend bool operator==(const Sequence&, const Sequence&) = default;
};

/// The explicit limit profile of one sequence: its length in seconds, its
/// shortest, its tracks, and its keys.
struct SequenceLimits {
    double mostDuration = 86400;
    double leastDuration = 0.01;
    std::uint32_t mostTracks = 256;
    std::uint32_t mostKeys = 65536;
};

/// Refuses (`BadSource`, naming the line) what is not a sequence document
/// of this form within `limits`: a duration, tracks of distinct machine
/// names, keys in time order within the duration (before it, in a loop)
/// on tracks it names, and events one to each other.
[[nodiscard]] result::Result<Sequence> parseSequence(std::string_view text, const SequenceLimits& limits = {});

/// The document `sequence` is, in the form `parseSequence` reads back to
/// it.
[[nodiscard]] std::string writeSequence(const Sequence& sequence);

/// Playing on from `time` for `seconds`: where the play is then, whether it
/// has come to an end that does not loop (and stays there), and the keys it
/// passed, by their places, in the order they fire. A key fires when the
/// play reaches its time: one at nought as a play starts there, and one
/// passed again after a loop once for each pass, of at most 64 passes in
/// one advance.
struct Advance {
    double time = 0;
    bool ended = false;
    std::vector<std::size_t> keys;
};

/// A `time` before nought is nought, one past the end of a loop wraps into
/// it, and `seconds` below nought are none; a sequence that ended passes
/// no key.
[[nodiscard]] Advance advance(const Sequence& sequence, double time, double seconds);

} // namespace rawframe::sequence
