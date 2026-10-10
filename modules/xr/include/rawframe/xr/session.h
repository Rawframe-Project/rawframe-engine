#pragma once

// A session on a runtime's head-mounted system, bound to the render
// module's one device (ADR-0081, D591). The runtime's session states are
// SPEC-0025's surface lifecycle in the runtime's words (ADR-0081, section
// 2): the session is begun when the runtime says it is ready and ended
// when it says it is stopping. While it runs, each presentation frame is
// waited for as the runtime paces it, begun, and ended; waiting blocks, so
// a session is driven only from the presentation path, never from an
// executor the simulation, replication, or the network depend on, and the
// display time it predicts never enters the World or a record (ADR-0081,
// section 3). Main thread only; the runtime and the device outlive it.

#include "rawframe/render/device.h"
#include "rawframe/result/result.h"
#include "rawframe/xr/runtime.h"

#include <array>
#include <cstdint>
#include <memory>
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

/// What one presentation frame did.
struct SessionFrame {
    /// A frame was waited for, begun, and ended.
    bool ended = false;
    /// The runtime shows it: its views are located and drawn.
    bool shown = false;
    /// The views at the frame's display time, the left eye's first, when
    /// shown.
    std::vector<ViewPose> views;
};

struct SessionStatistics {
    std::uint64_t framesEnded = 0;
    std::uint64_t framesShown = 0;
    /// Frames shown whose every view was located, and tracked.
    std::uint64_t framesLocated = 0;
    std::uint64_t framesTracked = 0;
};

class Session {
public:
    /// A session on `runtime`'s system, bound to `device`, ready (`State`
    /// otherwise), whose Vulkan objects `runtime` made.
    [[nodiscard]] static result::Result<std::unique_ptr<Session>> create(Runtime& runtime, render::Device& device);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session();

    /// Once a presentation frame: the runtime's events taken, the session
    /// begun or ended as they say, and, while it runs, one frame waited
    /// for, begun, its views located, and ended. Not running, it returns at
    /// once with nothing ended.
    [[nodiscard]] result::Result<SessionFrame> frame();
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
