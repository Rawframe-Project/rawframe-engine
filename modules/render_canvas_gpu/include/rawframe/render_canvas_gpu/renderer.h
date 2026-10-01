#pragma once

// The canvas's GPU half (SPEC-0024's canvas contract, D278): the draws the
// queue stage built recorded into the frame `render` owns (D285). Each
// texture they name is uploaded once as it was decoded, and again when a
// reload replaces it; the quads' vertices and indices are written each
// frame; and the draws are made in their order, each blending over what is
// behind it, into the frame's picture, after the scene's; then the
// particles, trails, and beams, over every sprite, through the device half
// the scene's share (D357).
//
// Its own module, apart from the CPU half, because the device is not on
// every client yet: the bots count draws without a device, so the CPU half
// builds everywhere and this only where Maul RHI does. Client only.

#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render/textures.h"
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
    /// The particles alive at once across every emitter (D357): an
    /// emitter whose ring would pass it is left out.
    std::uint32_t maximumParticles = 65536;
};

struct RendererStatistics {
    /// Frames it drew in, submitted.
    std::uint64_t frames = 0;
    /// Frames with a canvas to draw before its pipeline was ready.
    std::uint64_t framesWaiting = 0;
    std::uint64_t draws = 0;
    /// Draws left out: their texture not ready, in a format the device does
    /// not take, deferred, or past the textures held.
    std::uint64_t drawsLeftOut = 0;
    std::uint64_t texturesUploaded = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t uploadsDeferred = 0;
    /// Textures replaced by a reload's new revision.
    std::uint64_t texturesReplaced = 0;
    /// Emitters drawn (D357), those the pool could not hold, the particles
    /// they spawned, and the trails and beams drawn, over every frame.
    std::uint64_t emittersDrawn = 0;
    std::uint64_t emittersLeftOut = 0;
    std::uint64_t particlesSpawned = 0;
    std::uint64_t ribbonsDrawn = 0;
};

class CanvasRenderer final : public render::FrameRecorder {
public:
    /// Makes the sprite pipeline on `device`, which must be ready and must
    /// outlive this. The pipeline is made as Maul RHI answers: frames made
    /// before then draw no canvas.
    [[nodiscard]] static result::Result<std::unique_ptr<CanvasRenderer>> create(render::Device& device,
                                                                                RendererLimits limits = {});

    ~CanvasRenderer() override;

    /// What the next frame draws: `frame`'s draws, the textures they name
    /// uploaded if they must be; nothing for none; in `region` of the
    /// frame's picture (left, top, width, height in pixels: a local
    /// player's in split-screen, D364), or the whole for none, the picture
    /// cleared to `bars` (8-bit sRGB, D369) if this is the first to draw
    /// into it. All are held until the frame is made.
    void prepare(const render_canvas::CanvasFrame* frame,
                 TextureSource textures,
                 std::optional<std::array<std::uint32_t, 4>> region = std::nullopt,
                 std::array<std::uint8_t, 3> bars = {});

    [[nodiscard]] result::Status declare(render::Frame& frame) override;
    [[nodiscard]] result::Status record(render::Frame& frame) override;
    void ended(bool submitted) noexcept override;

    [[nodiscard]] const RendererStatistics& statistics() const noexcept;

    struct State;

private:
    explicit CanvasRenderer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render_canvas_gpu
