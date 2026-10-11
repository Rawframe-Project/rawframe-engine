#pragma once

// A tracked source (ADR-0081 section 4, D663): a hand's joints as a runtime
// that tracks hands located them, never an action value, never bound, and
// never a second authority beside the mapper. Its shape is fixed: both
// hands, each OpenXR's 26 joints in its order (the palm, the wrist, then
// each finger from its root to its tip, the thumb first), in meters in the
// headset's local space as a pose action's place is (+Y up, the origin
// where the head was when its session began). The XR module tells it once
// a Host iteration; a game's sample reads it while it runs, and only the
// first local player's, who wears the headset. What reaches the World is
// what the sample writes into the game's input, nothing more.

#include <array>
#include <cstddef>

namespace rawframe::input {

inline constexpr std::size_t kHandJoints = 26;

struct HandJoint {
    std::array<float, 3> position{};
    /// A unit quaternion, x, y, z, w.
    std::array<float, 4> orientation{0, 0, 0, 1};
    /// The joint's radius, meters.
    float radius = 0;
    /// The runtime knows both its position and its orientation.
    bool located = false;
};

struct HandJoints {
    /// The runtime tracks the hand now; none of its joints is located
    /// otherwise.
    bool tracked = false;
    std::array<HandJoint, kHandJoints> joints{};
};

class TrackedHands {
public:
    TrackedHands() = default;
    TrackedHands(const TrackedHands&) = delete;
    TrackedHands& operator=(const TrackedHands&) = delete;

    /// The XR module's side, once a Host iteration: the left hand's joints,
    /// then the right's; neither tracked while the runtime tracks no hand.
    void tell(const std::array<HandJoints, 2>& hands) noexcept {
        hands_ = hands;
    }
    /// The sample's side: hand `hand`, 0 the left, 1 the right; an
    /// untracked one past them.
    [[nodiscard]] const HandJoints& hand(std::size_t hand) const noexcept {
        static constexpr HandJoints kNone{};
        return hand < hands_.size() ? hands_.at(hand) : kNone;
    }

private:
    std::array<HandJoints, 2> hands_{};
};

} // namespace rawframe::input
