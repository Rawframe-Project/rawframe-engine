#pragma once

// A headset as the render module draws for it (ADR-0081, D593): an XR
// session lends it, and the device and the frames take it where it is. The
// device is made by the headset's runtime (the `VulkanMaker` it is); each
// Host iteration that plans a frame begins the headset's frame, paced by
// the runtime, and the frame's picture is placed into the images it gives
// (`FrameTarget::images`); the headset's frame ends once the picture is
// made, or not. A process with a window and a headset draws both from the
// one picture: what the headset shows is mirrored on the window.

#include "rawframe/composition/participant.h"
#include "rawframe/render/device.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace rawframe::render {

class Headset : public VulkanMaker {
public:
    /// The images a headset's frame is drawn into, and the picture's size.
    struct Views {
        std::vector<std::uint64_t> images;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    /// Whether a runtime's headset answered: the device is made by it, and
    /// frames are drawn for it. Settled before the device is asked for.
    [[nodiscard]] virtual bool present() const noexcept = 0;
    /// Once a Host iteration, as a frame is planned on `device`, the device
    /// its runtime made: the headset's frame waited for, as the runtime
    /// paces it, begun, and the images its picture is placed into; none
    /// while it shows nothing. Views given are followed by one `end`.
    [[nodiscard]] virtual std::optional<Views> begin(Device& device) noexcept = 0;
    /// The frame `begin` gave views for is over: `drawn` when its picture was
    /// made and placed into them.
    virtual void end(bool drawn) noexcept = 0;
    /// The frames end, before the device does: what was made on the device
    /// for the headset ends with them.
    virtual void close() noexcept = 0;
};

inline constexpr composition::Capability<Headset> kHeadset{"rawframe.render.headset"};

} // namespace rawframe::render
