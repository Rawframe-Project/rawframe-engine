#pragma once

// A headset's eyes as the scene draws them (ADR-0052's third target kind,
// the XR view pair; ADR-0081, D595): each eye's place and turn in the
// headset's local space (meters, +Y up, its origin where the head was when
// the session began), the angles of its field of view from its axis, as a
// headset's lenses make them (left and down negative), and the size of the
// image it is shown in. The XR module tells them once a Host iteration,
// before the presentation draws, as its runtime located them for the
// frame's display time, and lends them as `rawframe.view.headset_eyes`;
// the scene draws the first local player's view once for each, the
// player's camera the play space's origin and heading. Presentation data
// only: never the World's, nor a record's (ADR-0081, section 3).

#include "rawframe/composition/participant.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::view {

struct HeadsetEye {
    std::array<float, 3> position{};
    /// A unit quaternion, x, y, z, w.
    std::array<float, 4> orientation{0, 0, 0, 1};
    float angleLeft = 0;
    float angleRight = 0;
    float angleUp = 0;
    float angleDown = 0;
    /// The image the eye is shown in, in pixels.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class HeadsetEyes {
public:
    HeadsetEyes() = default;
    HeadsetEyes(const HeadsetEyes&) = delete;
    HeadsetEyes& operator=(const HeadsetEyes&) = delete;

    /// The XR module's side, once a Host iteration: this iteration's eyes,
    /// the left first; none while the headset shows nothing.
    void tell(std::span<const HeadsetEye> eyes) {
        eyes_.assign(eyes.begin(), eyes.end());
    }
    /// The scene's side: this iteration's eyes; empty while the headset
    /// shows nothing.
    [[nodiscard]] std::span<const HeadsetEye> eyes() const noexcept {
        return eyes_;
    }

private:
    std::vector<HeadsetEye> eyes_;
};

inline constexpr composition::Capability<HeadsetEyes> kHeadsetEyes{"rawframe.view.headset_eyes"};

/// Where a hand is at the frame's display time (D676), in the eyes' local
/// space: its controller's grip where one is located, else its tracked
/// palm; none where neither is.
struct HeadsetHand {
    std::array<float, 3> position{};
    /// A unit quaternion, x, y, z, w.
    std::array<float, 4> orientation{0, 0, 0, 1};
};

/// The hands as the scene draws a model held in them each frame
/// (`rawframe.model.Tracked`, ADR-0081 section 4's pose resolved at
/// presentation time), told with the eyes and lent as
/// `rawframe.view.headset_hands`. Presentation data only, as the eyes are.
class HeadsetHands {
public:
    HeadsetHands() = default;
    HeadsetHands(const HeadsetHands&) = delete;
    HeadsetHands& operator=(const HeadsetHands&) = delete;

    /// The XR module's side, once a Host iteration: the left hand, then
    /// the right; neither while the headset shows nothing.
    void tell(const std::array<std::optional<HeadsetHand>, 2>& hands) {
        hands_ = hands;
    }
    /// The scene's side: the left hand, then the right.
    [[nodiscard]] const std::array<std::optional<HeadsetHand>, 2>& hands() const noexcept {
        return hands_;
    }

private:
    std::array<std::optional<HeadsetHand>, 2> hands_;
};

inline constexpr composition::Capability<HeadsetHands> kHeadsetHands{"rawframe.view.headset_hands"};

/// Whether a headset the client shows on shows the person nothing now
/// (D679): its session made and neither visible nor focused, as when it is
/// taken off, or before the runtime first shows it. SPEC-0025's
/// `suspending` in the runtime's words (ADR-0081, section 2): the program
/// that runs the Host lends it as `rawframe.view.headset_away`, the XR
/// module tells it as its session's state changes, and the program counts
/// its frames suspended while it is, as it does from the platform's
/// `suspending` until `resumed` (D565). Main thread only.
class HeadsetAway {
public:
    HeadsetAway() = default;
    HeadsetAway(const HeadsetAway&) = delete;
    HeadsetAway& operator=(const HeadsetAway&) = delete;

    /// The XR module's side.
    void tell(bool away) noexcept {
        away_ = away;
    }
    /// The program's side.
    [[nodiscard]] bool away() const noexcept {
        return away_;
    }

private:
    bool away_ = false;
};

inline constexpr composition::Capability<HeadsetAway> kHeadsetAway{"rawframe.view.headset_away"};

} // namespace rawframe::view
