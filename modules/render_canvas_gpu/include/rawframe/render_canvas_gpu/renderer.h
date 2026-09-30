#pragma once

// The canvas's GPU half (SPEC-0024's canvas contract, D278): the draws the
// queue stage built recorded on the one device. Each texture they name is
// uploaded once as it was decoded, and again when a reload replaces it; the
// quads' vertices and indices are written each frame; and the draws are made
// in their order, each blending over what is behind it, into a target.
//
// Its own module, apart from the CPU half, because the device is not on
// every client yet: the web and macOS draw nothing, and the bots count
// draws without a device, so the CPU half builds everywhere and this only
// where Maul RHI does. Client only.

#include "rawframe/render/device.h"
#include "rawframe/render/display.h"
#include "rawframe/render_canvas/canvas.h"
#include "rawframe/result/result.h"
#include "rawframe/texture/texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace rawframe::render_canvas_gpu {

/// The texture the game names `id`, decoded; none while it is not ready.
using TextureSource = std::function<std::shared_ptr<const texture::Texture>(std::uint64_t id)>;

/// An image the canvas draws into and nothing shows: for tests, captures,
/// and tools. Rows run top first, as every image's do (ADR-0046).
struct OffscreenTarget {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// What is behind every sprite, in linear light, alpha last.
    std::array<float, 4> clear{0, 0, 0, 1};
    /// Whether the frame's pixels are read back (`pixels`).
    bool readBack = false;
};

/// A window's surface a frame's picture is shown on (D280): the picture is
/// drawn into its offscreen target of the surface's size, then `display`
/// draws it over the surface's image, which the frame presents.
struct ShownOn {
    render::Display* display = nullptr;
    std::uint64_t surface = 0;
};

/// SPEC-0024's limit points as the canvas's GPU half has them.
struct RendererLimits {
    /// Textures held on the device at once; a draw naming one past them is
    /// left out.
    std::size_t maximumTextures = 256;
    /// SPEC-0024's `upload_bytes_per_frame`: a texture that would pass it
    /// waits for a later frame (`deferred`), its draws left out until then.
    /// Three quarters of what the device uploads in a frame, the rest kept
    /// for the frame's corners; a texture larger than it is never drawn.
    std::uint64_t uploadBytesPerFrame = render::kFrameUploadBytes / 4 * 3;
    /// An offscreen target's sides.
    std::uint32_t maximumSide = 8192;
};

struct RendererStatistics {
    /// Frames recorded and submitted.
    std::uint64_t frames = 0;
    /// Frames asked for before the device and pipeline were ready.
    std::uint64_t framesWaiting = 0;
    std::uint64_t draws = 0;
    /// Draws left out: their texture not ready, in a format the device does
    /// not take, deferred, or past the textures held.
    std::uint64_t drawsLeftOut = 0;
    /// Frames shown on a window's surface, and those asked to be that were
    /// not: the surface had no image, or its pipeline was being made.
    std::uint64_t framesShown = 0;
    std::uint64_t framesNotShown = 0;
    std::uint64_t texturesUploaded = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t uploadsDeferred = 0;
    /// Textures replaced by a reload's new revision.
    std::uint64_t texturesReplaced = 0;
};

class CanvasRenderer {
public:
    /// Makes the sprite pipeline on `device`, which must be ready and must
    /// outlive this. The pipeline is made as Maul RHI answers: frames asked
    /// for before then are not drawn.
    [[nodiscard]] static result::Result<std::unique_ptr<CanvasRenderer>> create(render::Device& device,
                                                                                RendererLimits limits = {});

    CanvasRenderer(const CanvasRenderer&) = delete;
    CanvasRenderer& operator=(const CanvasRenderer&) = delete;
    ~CanvasRenderer();

    /// One frame of the canvas into `target`, sRGB: the textures `frame`
    /// names uploaded if they must be, then its draws in order. True once
    /// submitted; false while the pipeline is still being made. Never waits.
    [[nodiscard]] result::Result<bool>
    render(const render_canvas::CanvasFrame& frame, const TextureSource& textures, const OffscreenTarget& target);
    /// The same, and the picture shown on a window's surface.
    [[nodiscard]] result::Result<bool> render(const render_canvas::CanvasFrame& frame,
                                              const TextureSource& textures,
                                              const OffscreenTarget& target,
                                              const ShownOn& shown);

    /// Whether the last frame submitted is done on the GPU, its pixels
    /// ready if it read them back. Never waits.
    [[nodiscard]] result::Result<bool> done();

    /// Waits at most `nanoseconds` for the last frame submitted: for
    /// captures and tests, never on a frame's path.
    [[nodiscard]] result::Status finish(std::uint64_t nanoseconds);

    /// The last frame's pixels, RGBA8 sRGB, rows top first, once it is done;
    /// each frame's are given once.
    [[nodiscard]] std::optional<std::vector<std::byte>> pixels();

    [[nodiscard]] const RendererStatistics& statistics() const noexcept;

    struct State;

private:
    explicit CanvasRenderer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render_canvas_gpu
