#pragma once

// The 3D scene's GPU half (SPEC-0024's scene contract, D284): the draws the
// queue stage built recorded into the frame `render` owns (D285), first. Each mesh they name is
// uploaded once, positions and normals together; each frame writes the
// draws' placements and the frame's view and light, and records a depth
// prepass, the models lit by the sun and sky into a scene-linear FP16
// target, pre-exposed (ADR-0047), over a reversed-Z depth (ADR-0051), and
// the frame's picture: the scene target mapped for display by AgX into
// sRGB, every pixel of it.
// Consecutive draws of one mesh are one instanced draw.
//
// Its own module, apart from the CPU half, as the canvas's is (D278): the
// device is not on every client yet. Client only.

#include "rawframe/mesh/mesh.h"
#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"
#include "rawframe/texture/texture.h"

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
/// The texture a material samples, decoded; none while it is not ready
/// (D309).
using TextureSource = std::function<std::shared_ptr<const texture::Texture>(std::uint64_t id)>;

/// SPEC-0024's limit points as the scene's GPU half has them.
struct RendererLimits {
    /// Meshes held on the device at once; a draw naming one past them is
    /// left out.
    std::size_t maximumMeshes = 1024;
    /// Textures held at once; a material sampling one past them samples
    /// white (D309).
    std::size_t maximumTextures = 256;
    /// SPEC-0024's `upload_bytes_per_frame`: the frame's placements first,
    /// then meshes, then the materials' textures; a mesh that would pass it
    /// waits for a later frame (`deferred`), its draws left out until then,
    /// and a texture waits sampled as white. Three quarters of what the
    /// device uploads in a frame; a mesh or texture larger than what is left
    /// after the placements is never drawn.
    std::uint64_t uploadBytesPerFrame = render::kFrameUploadBytes / 4 * 3;
    /// ADR-0053's live particles a view holds (D353): the pool every
    /// emitter's ring is found in, 48 bytes a particle; an emitter whose
    /// ring it has no room for is left out.
    std::uint32_t maximumParticles = 65536;
};

struct RendererStatistics {
    /// Frames it drew in, submitted.
    std::uint64_t frames = 0;
    /// Frames with a scene to draw before its pipelines were ready.
    std::uint64_t framesWaiting = 0;
    /// Models drawn, and the draw calls they took.
    std::uint64_t models = 0;
    std::uint64_t drawCalls = 0;
    /// Models left out: their mesh not given, deferred, or past the meshes
    /// held.
    std::uint64_t modelsLeftOut = 0;
    std::uint64_t meshesUploaded = 0;
    /// The materials' textures uploaded (D309), and their bytes, apart
    /// from the meshes'.
    std::uint64_t texturesUploaded = 0;
    std::uint64_t textureBytes = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t uploadsDeferred = 0;
    /// Frames antialiased over time, and those of them that reused the
    /// picture before (D291).
    std::uint64_t framesResolved = 0;
    std::uint64_t historyReused = 0;
    /// Frames whose exposure was metered from what they saw (D293).
    std::uint64_t framesMetered = 0;
    /// Frames antialiased by FXAA (D296).
    std::uint64_t framesSmoothed = 0;
    /// Frames with ambient occlusion (D327), with bloom (D328), with
    /// screen-space reflections (D331), with motion blur (D334), with depth
    /// of field (D336), and with contact shadows (D338).
    std::uint64_t framesOccluded = 0;
    std::uint64_t framesBloomed = 0;
    std::uint64_t framesReflected = 0;
    std::uint64_t framesMotionBlurred = 0;
    std::uint64_t framesFocused = 0;
    std::uint64_t framesContactShadowed = 0;
    /// Decals drawn, summed over the frames (D339).
    std::uint64_t decalsDrawn = 0;
    /// Reflection probes drawn, summed over the frames (D340).
    std::uint64_t probesDrawn = 0;
    /// Frames whose models' passes were multisampled (D343).
    std::uint64_t framesMultisampled = 0;
    /// Post processes run, summed over the frames, and those left out: by
    /// the scene's stage, or over a composed picture no recorder drew
    /// (`final_output`, D350, D351).
    std::uint64_t postProcessesRun = 0;
    std::uint64_t postProcessesLeftOut = 0;
    /// Particle emitters drawn, summed over the frames; those left out for
    /// want of room in the pool; and the particles they spawned (D353).
    std::uint64_t emittersDrawn = 0;
    std::uint64_t emittersLeftOut = 0;
    std::uint64_t particlesSpawned = 0;
};

/// The bytes of one draw's placement as the scene pipeline reads it: the
/// model's rows, the normals' columns, the color, the model's rows the
/// frame before (D291), and its material's place among the frame's (D303).
inline constexpr std::uint32_t kInstanceBytes = 152;

/// A frame's light as its scene target held it before the tonemapper
/// (D326): linear Rec. 709 in candela per square meter, red, green, and
/// blue for each pixel, rows top first. Divided by the exposure the frame
/// names: a metering camera's is off by what its metering moved.
struct LightCapture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> light;
};

class SceneRenderer final : public render::FrameRecorder {
public:
    /// Makes the scene's pipelines on `device`, which must be ready and must
    /// outlive this. They are made as Maul RHI answers: frames made before
    /// then draw no scene.
    [[nodiscard]] static result::Result<std::unique_ptr<SceneRenderer>> create(render::Device& device,
                                                                               RendererLimits limits = {});

    ~SceneRenderer() override;

    /// What the next frame draws: `frame`'s draws, the meshes they name
    /// and the textures their materials sample uploaded if they must be;
    /// nothing for none. All are held until the frame is made.
    void prepare(const render_scene::SceneFrame* frame, MeshSource meshes, TextureSource textures = {});

    [[nodiscard]] result::Status declare(render::Frame& frame) override;
    [[nodiscard]] result::Status record(render::Frame& frame) override;
    void ended(bool submitted) noexcept override;

    [[nodiscard]] const RendererStatistics& statistics() const noexcept;

    /// What the scene draws over the composed picture (`final_output` post
    /// processes, D351): a recorder of its own, to be recorded after every
    /// other that draws the picture (the canvas), drawing only what the
    /// scene's frame in the same frame asks. Without it, those are left
    /// out and counted.
    [[nodiscard]] render::FrameRecorder& composed() noexcept;

    /// Reads back the light of the next frame submitted with a scene, one
    /// the device reads back at once (`render::kReadbackBytes`), for a
    /// tool (D326).
    void capture() noexcept;
    /// The light captured, once the device finished its frame; given once.
    /// Never waits.
    [[nodiscard]] std::optional<LightCapture> captured();

    struct State;

private:
    explicit SceneRenderer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render_scene_gpu
