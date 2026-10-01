#pragma once

// The particles, trails, and beams on the device (ADR-0053's one GPU
// simulation, D353, D354, D357): what a view's accounting gives
// (`rawframe.particles`) drawn the same way by the scene and the canvas.
// One pool kept from frame to frame, in which each emitter a frame draws
// holds a ring of its own, cleared by a compute pass when it starts; a
// compute pass writing the frame's births into the rings; and a pass
// drawing the ribbons, then every slot of each emitter's ring, over the
// host's picture, each with its material, hidden and faded by the depth
// the host gives. Its shaders and pipelines are asked for the first frame
// that draws any, in the host's picture's format; the frames until the
// device answers draw none.
//
// The host names the open frame's resources as `render::requestKey` names
// ids, so Maul RHI stays behind the rendering cluster's sources.

#include "rawframe/particles/particles.h"
#include "rawframe/render/device.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace rawframe::particles_gpu {

/// The view as the shaders read it (std140, D357): its projection of the
/// World relative to the eye (column-major); the eye's right and up in the
/// World's axes, with the particle clock now and its period; and the near
/// plane, and one where the view is flat, facing straight down its forward
/// (a canvas's).
struct ViewBlock {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> right{};
    std::array<float, 4> up{};
    std::array<float, 4> lens{};
};
static_assert(sizeof(ViewBlock) == 112, "the particles' shaders read the view as 112 bytes");

/// A material as the shaders read it (std140, D357): its color, a constant
/// plus a constant times its texture's color (its alpha in the fourth);
/// its emission, a constant plus a constant times its emission texture's
/// color; each texture's scale and offset; and its flags.
struct MaterialBlock {
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 4> colorTexture{};
    std::array<float, 4> emission{};
    std::array<float, 4> emissionTexture{};
    std::array<float, 4> baseMap{1, 1, 0, 0};
    std::array<float, 4> emissionMap{1, 1, 0, 0};
    std::array<std::uint32_t, 4> flags{};
};
static_assert(sizeof(MaterialBlock) == 112, "the particles' shaders read a material as 112 bytes");

/// A material's flags: its texture's alpha shapes it (else a particle is
/// a soft disc, a ribbon soft along its sides); it multiplies what is
/// behind.
inline constexpr std::uint32_t kShapedByTexture = 1;
inline constexpr std::uint32_t kMultiplies = 2;

/// How a material blends over what is behind, as SPEC-0026's canvas set
/// has it: over it, its coverage hiding it; added to it; or multiplying
/// it. The scene's are all over.
enum class Blend : std::uint8_t {
    Over,
    Add,
    Multiply
};
inline constexpr std::size_t kBlends = 3;

/// A texture as the host holds it this frame: the texture and its
/// sampler, each named as `render::requestKey` names ids.
struct BoundTexture {
    std::uint64_t texture = 0;
    std::uint64_t sampler = 0;
};

/// A material as the device draws it: its block, its blend, and its
/// textures for its color and its emission.
struct Material {
    MaterialBlock block;
    Blend blend = Blend::Over;
    BoundTexture color;
    BoundTexture emission;
};

/// The picture the host draws into: the scene's light, in half floats, or
/// the canvas's picture, eight sRGB bits a channel.
enum class Target : std::uint8_t {
    Light,
    Picture
};

/// What a frame's draws take from the host, each named as
/// `render::requestKey` names ids: the picture they draw over, kept; the
/// exposure (a buffer whose second number multiplies the emission), or
/// none for one; and the depth they are hidden by and fade into
/// (reversed-Z, its depth aspect sampled), or none for nought, hiding
/// nothing.
struct Drawing {
    std::uint64_t picture = 0;
    std::optional<std::uint64_t> exposure;
    std::optional<std::uint64_t> depth;
};

/// The particles, trails, and beams on the device (D353, D354, D357). An
/// emitter whose ring the pool cannot hold is left out and counted; a ring
/// is let go once its emitter is not drawn.
class Particles {
public:
    /// On `device`, which must be ready and outlive this, drawing into a
    /// picture of `target`'s format, with a pool of `capacity` particles,
    /// made the first time a frame draws an emitter.
    [[nodiscard]] static result::Result<std::unique_ptr<Particles>>
    create(render::Device& device, Target target, std::uint32_t capacity);
    /// On `device`, which must be `sharing`'s, drawing into a picture of
    /// its format with its shaders and pipelines (D361), so the device
    /// compiles them once, with a pool of its own.
    [[nodiscard]] static result::Result<std::unique_ptr<Particles>>
    create(render::Device& device, const Particles& sharing, std::uint32_t capacity);

    Particles(const Particles&) = delete;
    Particles& operator=(const Particles&) = delete;
    ~Particles();

    /// Joins the open frame when `frame` draws emitters or ribbons and the
    /// pipelines their materials blend with are made (asked for here the
    /// first time): the pool made or imported, each emitter's ring found,
    /// and its passes added, in order: an upload of what it writes, the
    /// new rings cleared, the births, a depth of nought where `with` gives
    /// none, and the drawing over `with.picture`, reading `materials` (by
    /// their places, a place past them drawing as the first), `view`, and
    /// what `with` gives.
    result::Status declare(const particles::Frame& frame,
                           std::span<const Material> materials,
                           const ViewBlock& view,
                           const Drawing& with);

    /// Its passes recorded, in the order they were added.
    result::Status record();

    /// Whether the open frame draws any.
    [[nodiscard]] bool enabled() const noexcept;

    /// The frame ended: a ring a frame that was not submitted would have
    /// cleared is found again.
    void ended(bool submitted) noexcept;

    /// The open frame's emitters drawn, those left out, the particles
    /// spawned, and the ribbons drawn.
    [[nodiscard]] std::size_t emittersDrawn() const noexcept;
    [[nodiscard]] std::size_t emittersLeftOut() const noexcept;
    [[nodiscard]] std::uint64_t spawned() const noexcept;
    [[nodiscard]] std::size_t ribbonsDrawn() const noexcept;

    struct State;

private:
    explicit Particles(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::particles_gpu
