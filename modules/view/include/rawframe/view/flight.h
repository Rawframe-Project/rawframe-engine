#pragma once

// An editor's viewport camera flown (D913), as Unity's and Unreal's are:
// with the right button held the pointer turns the view and W A S D Q E
// fly it, Shift faster, the wheel then setting the speed; the middle
// button pans; Alt with the left orbits about the point ahead; the wheel
// alone moves the view toward that point or away. Done where the view is
// drawn, each frame, so it moves at the frame's pace.

#include "rawframe/view/view.h"

#include <array>

namespace rawframe::view {

/// What the input asked of the view since it was last flown: the keys
/// held now, and the pointer's motion in pixels and the wheel's detents
/// (toward the author positive) gathered since.
struct Steering {
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool down = false;
    bool up = false;
    bool faster = false;
    std::array<double, 2> turn{};
    std::array<double, 2> pan{};
    std::array<double, 2> orbit{};
    double wheel = 0;
    double speedWheel = 0;
};

/// How far ahead the point orbited about lies, in meters, and how fast
/// the keys fly, meters a second: kept between frames.
struct Flight {
    double pivot = 10;
    double speed = 6;
};

/// `view` after `seconds` of `steering`, its window `height` pixels high;
/// `flight` follows. A view that would not be finite stays as it was.
[[nodiscard]] Perspective
flown(const Perspective& view, Flight& flight, const Steering& steering, double seconds, double height) noexcept;

/// Whether `steering` asks anything of the view.
[[nodiscard]] bool steers(const Steering& steering) noexcept;

/// The point the view orbits about, `flight.pivot` ahead of its eye.
[[nodiscard]] std::array<double, 3> pivotOf(const Perspective& view, const Flight& flight) noexcept;

} // namespace rawframe::view
