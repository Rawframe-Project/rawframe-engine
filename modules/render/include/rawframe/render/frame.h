#pragma once

// SPEC-0024's frame, owned by the device's module (D285): one frame a Host
// iteration, which every bridge (the scene, the canvas) records into, then
// submitted and displayed once. The frame begins, the view's picture is
// declared, and each recorder declares its resources and passes in its
// order (SPEC-0024's per-view composition: the scene, then the canvas);
// the picture is shown on a window's surface or read back; the frame
// compiles, each recorder records what it declared, and the frame is
// submitted. One frame is on the GPU at a time: a frame asked for while
// the last still runs is not made.

#include "rawframe/composition/participant.h"
#include "rawframe/render/device.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::render {

/// The open frame as a recorder sees it (SPEC-0024's `record`).
struct Frame {
    Device* device = nullptr;
    /// The view's size in pixels.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// The view's picture, a texture of the frame named as `requestKey`
    /// names ids: RGBA8 sRGB, blended in linear light, what is shown or
    /// read back.
    std::uint64_t picture = 0;
    /// Whether a pass drawing into the picture clears it (to black): the
    /// first to ask does, and those after keep what is there. Asked once
    /// for each such pass, as it is declared.
    [[nodiscard]] bool clearsPicture() noexcept {
        return !std::exchange(drawn, true);
    }
    bool drawn = false;
    /// What drew into the picture first, under all after it, told by the
    /// recorder `clearsPicture` answered true if it cares: the canvas's
    /// local players learn from it whether theirs is the views' ground
    /// (D369).
    const void* ground = nullptr;
};

/// An 8-bit sRGB channel in linear light, what a pass clearing an sRGB
/// target to that color is given (a constrained view's bars, D369).
[[nodiscard]] float linearOf(std::uint8_t encoded) noexcept;

/// A bridge's part of each frame (the scene's, the canvas's).
class FrameRecorder {
public:
    FrameRecorder() = default;
    FrameRecorder(const FrameRecorder&) = delete;
    FrameRecorder& operator=(const FrameRecorder&) = delete;
    virtual ~FrameRecorder() = default;

    /// Before the frame compiles: what it records, declared (resources,
    /// then passes in order), its draws into the picture after those of
    /// the recorders before it; nothing for a frame it has nothing in. An
    /// error drops the frame.
    [[nodiscard]] virtual result::Status declare(Frame& frame) = 0;
    /// After the frame compiled: what it declared, recorded.
    [[nodiscard]] virtual result::Status record(Frame& frame) = 0;
    /// The frame it declared into was submitted, or dropped (`false`):
    /// what it uploaded is there only once submitted.
    virtual void ended(bool submitted) noexcept = 0;
};

/// Where a frame's picture goes.
struct FrameTarget {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// Whether the picture's pixels are read back (`Framer::pixels`).
    bool readBack = false;
    /// The prepared surface it is shown on; none for a picture nothing
    /// shows.
    std::optional<std::uint64_t> surface;
};

struct FramerStatistics {
    /// Frames submitted, those shown on a surface and those that were to be
    /// and were not (no image, or its pipeline being made).
    std::uint64_t frames = 0;
    std::uint64_t framesShown = 0;
    std::uint64_t framesNotShown = 0;
    /// Frames asked for while the last was still on the GPU.
    std::uint64_t framesBusy = 0;
};

/// Makes the frames: the frame owner's machinery, apart from composition
/// so a test drives it.
class Framer {
public:
    /// On a ready `device`, which must outlive it.
    [[nodiscard]] static result::Result<std::unique_ptr<Framer>> create(Device& device);

    Framer(const Framer&) = delete;
    Framer& operator=(const Framer&) = delete;
    ~Framer();

    /// One frame of `recorders`, in the order given, into `target`: true
    /// once submitted, false while the last frame is still on the GPU.
    /// Never waits.
    [[nodiscard]] result::Result<bool> make(std::span<FrameRecorder* const> recorders, const FrameTarget& target);

    /// Whether the last frame submitted is done on the GPU, its pixels
    /// ready if it read them back. Never waits.
    [[nodiscard]] result::Result<bool> done();
    /// Waits at most `nanoseconds` for the last frame submitted: for
    /// captures, tests, and stopping, never on a frame's path.
    [[nodiscard]] result::Status finish(std::uint64_t nanoseconds);
    /// The last frame's pixels, RGBA8 sRGB, rows top first, once it is done;
    /// each frame's are given once.
    [[nodiscard]] std::optional<std::vector<std::byte>> pixels();

    [[nodiscard]] const FramerStatistics& statistics() const noexcept;

    struct State;

private:
    explicit Framer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// The frames as the bridges reach them (`rawframe.render.frames`): the
/// frame participant plans one at the start of each `present` and makes it
/// once every recorder that joined is ready.
class Frames {
public:
    Frames() = default;
    Frames(const Frames&) = delete;
    Frames& operator=(const Frames&) = delete;
    virtual ~Frames() = default;

    /// The device frames are made on, ready; none while there is none.
    [[nodiscard]] virtual Device* device() noexcept = 0;
    /// The size of the frame this Host iteration makes; none when it makes
    /// none (no device, the last frame still running, a window showing
    /// nothing).
    [[nodiscard]] virtual std::optional<std::pair<std::uint32_t, std::uint32_t>> planned() const noexcept = 0;
    /// Joins every frame from the next ready on, recorded in `order`'s
    /// order: the scene is 0, the canvas 1, and the scene's post processes
    /// over the composed picture 2 (D351).
    virtual void join(FrameRecorder& recorder, std::uint32_t order) = 0;
    /// Leaves the frames, before the recorder ends.
    virtual void leave(FrameRecorder& recorder) noexcept = 0;
    /// The recorder has prepared what it records in the planned frame, or
    /// has nothing: once every one that joined is ready, the frame is made.
    virtual void ready(FrameRecorder& recorder) noexcept = 0;
};

inline constexpr composition::Capability<Frames> kFrames{"rawframe.render.frames"};

} // namespace rawframe::render
