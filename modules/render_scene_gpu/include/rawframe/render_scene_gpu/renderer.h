#pragma once

// The 3D scene's GPU half (SPEC-0024's scene contract, D284): the draws the
// queue stage built recorded on the one device. Each mesh they name is
// uploaded once, positions and normals together; each frame writes the
// draws' placements and the frame's view and light, and records a depth
// prepass, the models lit by the sun and sky into a scene-linear FP16
// target, pre-exposed (ADR-0047), over a reversed-Z depth (ADR-0051), and
// the picture: the scene target mapped for display by AgX into sRGB.
// Consecutive draws of one mesh are one instanced draw.
//
// Its own module, apart from the CPU half, as the canvas's is (D278): the
// device is not on every client yet. Client only.

#include "rawframe/mesh/mesh.h"
#include "rawframe/render/device.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace rawframe::render_scene_gpu {

/// The mesh a draw names; none for one that cannot be drawn.
using MeshSource = std::function<std::shared_ptr<const mesh::Mesh>(std::uint64_t id)>;

/// An image the scene draws into and nothing shows: for tests, captures,
/// and tools. Rows run top first, as every image's do (ADR-0046).
struct SceneTarget {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// Whether the frame's pixels are read back (`pixels`).
    bool readBack = false;
};

/// SPEC-0024's limit points as the scene's GPU half has them.
struct RendererLimits {
    /// Meshes held on the device at once; a draw naming one past them is
    /// left out.
    std::size_t maximumMeshes = 1024;
    /// SPEC-0024's `upload_bytes_per_frame`: the frame's placements first,
    /// then meshes; a mesh that would pass it waits for a later frame
    /// (`deferred`), its draws left out until then. Three quarters of what
    /// the device uploads in a frame; a mesh larger than what is left after
    /// the placements is never drawn.
    std::uint64_t uploadBytesPerFrame = render::kFrameUploadBytes / 4 * 3;
    /// A target's sides.
    std::uint32_t maximumSide = 8192;
};

struct RendererStatistics {
    /// Frames recorded and submitted.
    std::uint64_t frames = 0;
    /// Frames asked for before the device and pipelines were ready.
    std::uint64_t framesWaiting = 0;
    /// Models drawn, and the draw calls they took.
    std::uint64_t models = 0;
    std::uint64_t drawCalls = 0;
    /// Models left out: their mesh not given, deferred, or past the meshes
    /// held.
    std::uint64_t modelsLeftOut = 0;
    std::uint64_t meshesUploaded = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t uploadsDeferred = 0;
};

/// The bytes of one draw's placement as the scene pipeline reads it: the
/// model's rows, the normals' columns, and the color.
inline constexpr std::uint32_t kInstanceBytes = 100;

class SceneRenderer {
public:
    /// Makes the scene's pipelines on `device`, which must be ready and must
    /// outlive this. They are made as Maul RHI answers: frames asked for
    /// before then are not drawn.
    [[nodiscard]] static result::Result<std::unique_ptr<SceneRenderer>> create(render::Device& device,
                                                                               RendererLimits limits = {});

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;
    ~SceneRenderer();

    /// One frame of the scene into `target`, sRGB: the meshes `frame` names
    /// uploaded if they must be, then its draws. True once submitted; false
    /// while the pipelines are still being made. Never waits.
    [[nodiscard]] result::Result<bool>
    render(const render_scene::SceneFrame& frame, const MeshSource& meshes, const SceneTarget& target);

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
    explicit SceneRenderer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render_scene_gpu
