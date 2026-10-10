#pragma once

// A headset as the render module draws for it (ADR-0081, D593, D595): an
// XR session lends it, and the device and the frames take it where it is.
// The device is made by the headset's runtime (the `VulkanMaker` it is).
// The headset begins its own frame each Host iteration, before the
// presentation draws, as its runtime paces it; the frame planned in that
// iteration is drawn into the images it gives (`FrameTarget::images`), and
// the headset's frame ends once the frame is made, or at the iteration's
// end undrawn. A process with a window and a headset draws both from the
// one frame: what the headset shows is mirrored on the window. A headset
// may show the UI apart, on a panel of its own (D597): the UI is then laid
// out and drawn for it, and the window mirrors the eyes' world alone.

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
        /// Whether the last of `images` is a panel the UI is shown on
        /// apart, at its own size, as the headset places it before the eyes
        /// (D597).
        bool panel = false;
    };

    /// Whether a runtime's headset answered: the device is made by it, and
    /// frames are drawn for it. Settled before the device is asked for.
    [[nodiscard]] virtual bool present() const noexcept = 0;
    /// As a frame is planned on `device`, the device its runtime made: the
    /// images the headset's frame of this Host iteration is drawn into;
    /// none while it shows nothing. The first asking makes its session on
    /// the device. Views given are followed by one `end`, or by the
    /// iteration's end.
    [[nodiscard]] virtual std::optional<Views> views(Device& device) noexcept = 0;
    /// The frame `views` gave images for is over: `drawn` when its picture
    /// was made and placed into them.
    virtual void end(bool drawn) noexcept = 0;
    /// The frames end, before the device does: what was made on the device
    /// for the headset ends with them.
    virtual void close() noexcept = 0;
};

inline constexpr composition::Capability<Headset> kHeadset{"rawframe.render.headset"};

} // namespace rawframe::render
