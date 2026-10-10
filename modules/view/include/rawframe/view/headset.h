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

} // namespace rawframe::view
