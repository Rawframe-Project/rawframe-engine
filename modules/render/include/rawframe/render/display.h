#pragma once

// SPEC-0024's `display` stage (D280): a frame's finished picture shown on a
// window's surface. The picture is drawn in an sRGB texture of the frame,
// blended in linear light; a surface's images are unorm (Maul RHI's
// surfaces are, and a pass cannot target a texture through its sRGB twin),
// so a last pass copies the picture's encoded bytes into the surface's
// image through the picture's linear view, whichever the image's channel
// order. The same pass is where a picture of another size is scaled; and,
// through its sRGB view into a rectangle of another sRGB picture, where a
// view drawn at its render scale is placed at its region's size (D373). For
// the rendering cluster, which records frames.

#include "rawframe/render/device.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace rawframe::render {

class Display {
public:
    /// Made on a ready `device`, which must outlive it.
    [[nodiscard]] static result::Result<std::unique_ptr<Display>> create(Device& device);

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    ~Display();

    /// Before the open frame compiles: `surface`'s next image acquired and
    /// a pass drawing `picture` over it, the pass's key given. `picture`
    /// is a texture of the frame (named as `requestKey` names ids), sRGB,
    /// with its linear twin among its view formats. None when the surface
    /// has no image this frame, and while the pipeline for its format is
    /// still being made.
    [[nodiscard]] result::Result<std::optional<std::uint64_t>> add(std::uint64_t surface, std::uint64_t picture);

    /// Before the open frame compiles: a pass drawing `from`, a texture of
    /// the frame (sRGB, sampled in linear light), over `rectangle` (left,
    /// top, width, height in pixels) of `into`, another (sRGB, kept around
    /// the rectangle), scaled to it; the pass's key given, recorded by
    /// `record`. None while the pipeline is still being made.
    [[nodiscard]] result::Result<std::optional<std::uint64_t>>
    place(std::uint64_t from, std::uint64_t into, std::array<std::uint32_t, 4> rectangle);

    /// After the frame compiled: the pass `add` gave, recorded.
    [[nodiscard]] result::Status record(std::uint64_t pass);

    struct State;

private:
    explicit Display(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render
