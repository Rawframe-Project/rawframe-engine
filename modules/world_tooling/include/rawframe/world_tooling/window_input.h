#pragma once

// A window's input as data: the presses, the pointer, the wheel,
// and the keys an author gives a preview an editor shows in its own
// viewport. The editor's window host hands over what lands on the
// viewport; `tooling.input` carries it to the preview's Runtime, whose
// window host takes it as its own window's. An agent drives a preview's
// game the same way, with no editor. Here, with the protocol, since a
// dedicated server, which serves the protocol, may know no client input
// or window. Hostile input: read whole or refused.

#include "rawframe/document/json.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::world_tooling {

struct WindowInput {
    enum class Kind : std::uint8_t {
        Move,
        Press,
        Release,
        Wheel,
        KeyDown,
        KeyUp,
        Text,
        Leave,
    };
    Kind kind = Kind::Move;
    /// Where the pointer is, logical pixels from the picture's top left.
    float x = 0;
    float y = 0;
    /// A press's or a release's button: 0 left, 1 right, 2 middle.
    std::uint8_t button = 0;
    /// The wheel's detents, positive y away from the author.
    float deltaX = 0;
    float deltaY = 0;
    /// A key's USB HID usage, and whether it repeats.
    std::uint16_t usage = 0;
    bool repeat = false;
    /// The modifier bits held, as the window's: Shift 1, Control 2, Alt 4,
    /// Meta 8.
    std::uint16_t modifiers = 0;
    /// Typed text, valid UTF-8 of at most `kMostText` bytes.
    std::string text;

    friend bool operator==(const WindowInput&, const WindowInput&) = default;
};

/// The most events one record carries, and the most text one holds.
inline constexpr std::size_t kMostWindowInputs = 64;
inline constexpr std::size_t kMostText = 64;

/// `events`, an array of `{"type": ...}` objects, as input; none for one
/// that does not read: an unknown type, a point or a delta that is not a
/// finite number within 16384 pixels, a button past 2, a usage past
/// 65535, text past its limit or not UTF-8, or more than the most events.
[[nodiscard]] std::optional<std::vector<WindowInput>> windowInputsOf(const document::Value& events);

/// `events` as `windowInputsOf` reads them.
[[nodiscard]] document::Value windowInputsValue(const std::vector<WindowInput>& events);

} // namespace rawframe::world_tooling
