#pragma once

// A touch screen as virtual controls (D387), the model ADR-0037 left open:
// a window split down the middle, each half a floating stick and a button.
// A touch that begins in a half takes it while no other holds it: the
// half's button is held, and its stick is the touch's way from where it
// began, a full tilt at kStickRadius pixels, up positive as a gamepad's; the
// stick rests and the button lets go when the touch ends. `pointer` is the
// newest touch's place. What the game draws for them is its own UI. Nothing
// here knows a window system: a host tells it touches in logical pixels.

#include "rawframe/input/feed.h"

#include <array>
#include <cstdint>
#include <optional>

namespace rawframe::input {

class TouchControls {
public:
    /// A full tilt, in logical pixels.
    static constexpr float kStickRadius = 64;

    /// Events go to `feed` as `device`'s, which is connected as a touch
    /// screen with the first touch.
    TouchControls(Feed& feed, DeviceId device) noexcept;

    /// The window is `width` logical pixels wide: its middle splits the
    /// halves. Until told, every touch begins in the left.
    void resize(float width) noexcept;
    void down(std::uint64_t touch, float x, float y);
    void move(std::uint64_t touch, float x, float y);
    /// Ended or cancelled.
    void up(std::uint64_t touch);
    /// Every touch forgotten: the feed was told to let go of everything.
    void forget() noexcept;

private:
    struct Half {
        std::optional<std::uint64_t> touch;
        float x = 0;
        float y = 0;
    };

    void stick(std::size_t half, float x, float y);

    Feed* feed_;
    DeviceId device_;
    bool connected_ = false;
    float width_ = 0;
    std::array<Half, 2> halves_{};
};

} // namespace rawframe::input
