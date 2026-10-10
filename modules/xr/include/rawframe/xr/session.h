#pragma once

// A session on a runtime's head-mounted system, bound to the render
// module's one device (ADR-0081, D591). The runtime's session states are
// SPEC-0025's surface lifecycle in the runtime's words (ADR-0081, section
// 2): the session is begun when the runtime says it is ready and ended
// when it says it is stopping. Each view has a swapchain, whose images the
// runtime owns and the device adopts as textures to draw into (D592).
// While the session runs, each presentation frame is waited for as the
// runtime paces it, begun, its views located and their images acquired;
// the frame's picture is placed into them; and the frame is ended, its
// views submitted as a projection layer. Waiting blocks, so a session is
// driven only from the presentation path, never from an executor the
// simulation, replication, or the network depend on, and the display time
// it predicts never enters the World or a record (ADR-0081, section 3).
// The session reads both hands' controllers through actions of its own
// (D596): each hand's select and menu buttons and its grip and aim poses,
// suggested for the simple controller every runtime binds
// (`khr/simple_controller`, ADR-0081's floor) and bound to whatever
// controller the runtime has, by its own binding. A session may show a
// panel too, the UI apart from the world (D597): its own swapchain, its
// image acquired with the views' and submitted as a quad layer over them.
// Main thread only; the runtime and the device outlive it.

#include "rawframe/render/device.h"
#include "rawframe/result/result.h"
#include "rawframe/xr/runtime.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rawframe::xr {

/// OpenXR's session states, in its order.
enum class SessionState : std::uint8_t {
    Unknown,
    Idle,
    Ready,
    Synchronized,
    Visible,
    Focused,
    Stopping,
    LossPending,
    Exiting,
};

/// Where a view was seen from at a frame's display time, in the session's
/// local space (meters; its origin where the head was when it began, +Y
/// up): OpenXR's pose and field of view.
struct ViewPose {
    std::array<float, 3> position{};
    /// A unit quaternion, x, y, z, w.
    std::array<float, 4> orientation{0, 0, 0, 1};
    /// The field of view's angles from the view's axis, in radians: left
    /// and down negative.
    float angleLeft = 0;
    float angleRight = 0;
    float angleUp = 0;
    float angleDown = 0;
    /// Whether the runtime knows both the position and the orientation
    /// (OpenXR's valid bits), and whether it tracks both now rather than
    /// infers them (its tracked bits; a simulated headset tracks nothing).
    bool located = false;
    bool tracked = false;
};

/// Where a controller's grip or aim was at a frame's display time, in the
/// session's local space, as its views are.
struct SpacePose {
    std::array<float, 3> position{};
    /// A unit quaternion, x, y, z, w.
    std::array<float, 4> orientation{0, 0, 0, 1};
    bool located = false;
    bool tracked = false;
};

/// A hand's controller as the runtime's binding read it at a frame.
struct Hand {
    /// The runtime binds a controller to the hand.
    bool active = false;
    bool select = false;
    bool menu = false;
    /// Where it is held, and where it points from (OpenXR's grip and aim).
    SpacePose grip;
    SpacePose aim;
};

/// A panel the session shows before the eyes, the UI apart from the
/// world (D597): an image of `width` by `height` pixels, drawn with clear
/// where nothing is, shown as a quad `meters` wide, upright, `distance`
/// meters ahead of where the head was when the session began. A side of
/// nought shows none.
struct PanelSettings {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float meters = 1.6F;
    float distance = 1.5F;
};

/// Where `aim` points on `panel` as a session shows it (D598): the panel's
/// pixels from its top left, where the aim's forward ray (its -Z) meets the
/// panel inside it; none where the aim is not located, points away, or
/// misses it.
[[nodiscard]] std::optional<std::array<float, 2>> pointOnPanel(const SpacePose& aim,
                                                               const PanelSettings& panel) noexcept;

/// A presentation frame, from `begin` to `end`.
struct SessionFrame {
    /// A frame was waited for and begun: `end` follows.
    bool begun = false;
    /// The runtime shows it: its views are located and their images
    /// acquired.
    bool shown = false;
    /// The views at the frame's display time, the left eye's first, when
    /// shown.
    std::vector<ViewPose> views;
    /// Each view's image this frame, adopted on the device: what the
    /// frame's picture is placed into (`render::FrameTarget::images`)
    /// between `begin` and `end`, when shown.
    std::vector<std::uint64_t> images;
    /// The panel's image this frame, when shown and the session has one.
    std::optional<std::uint64_t> panel;
    /// The left hand's controller, then the right's, at the frame's
    /// display time; read while the session has the input focus, else
    /// neither active (the runtime keeps the input for itself).
    std::array<Hand, 2> hands;
};

struct SessionStatistics {
    std::uint64_t framesEnded = 0;
    std::uint64_t framesShown = 0;
    /// Frames shown whose every view was located, and tracked.
    std::uint64_t framesLocated = 0;
    std::uint64_t framesTracked = 0;
    /// Frames ended with their views drawn and submitted, and those with
    /// the panel submitted over them.
    std::uint64_t framesSubmitted = 0;
    std::uint64_t framesPanelSubmitted = 0;
    /// Frames begun with the input focus, whose hands were read, and those
    /// in which a hand's grip was located.
    std::uint64_t framesHandsRead = 0;
    std::uint64_t framesHandLocated = 0;
};

class Session {
public:
    /// A session on `runtime`'s system, bound to `device`, ready (`State`
    /// otherwise), whose Vulkan objects `runtime` made, with `panel`'s
    /// panel.
    [[nodiscard]] static result::Result<std::unique_ptr<Session>>
    create(Runtime& runtime, render::Device& device, const PanelSettings& panel = {});

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session();

    /// Once a presentation frame: the runtime's events taken, the session
    /// begun or ended as they say, and, while it runs, one frame waited
    /// for, begun, its views located, and their images acquired. Not
    /// running, it returns at once with nothing begun.
    [[nodiscard]] result::Result<SessionFrame> begin();
    /// The frame `begin` began, ended: its images given back, and its views
    /// submitted when `drawn` says the picture was placed into them (else
    /// nothing is shown this frame).
    [[nodiscard]] result::Status end(bool drawn);
    /// The format and size of each view's images.
    [[nodiscard]] const std::vector<ViewSize>& images() const noexcept;
    /// The size of the panel's images; none without a panel.
    [[nodiscard]] std::optional<ViewSize> panel() const noexcept;
    /// Asks the runtime to end the session: it stops, then exits, through
    /// the frames that follow.
    [[nodiscard]] result::Status requestExit();

    [[nodiscard]] SessionState state() const noexcept;
    /// The runtime ended it, or is losing it: no frame comes again.
    [[nodiscard]] bool over() const noexcept;
    [[nodiscard]] const SessionStatistics& statistics() const noexcept;

    struct State;

private:
    explicit Session(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::xr
