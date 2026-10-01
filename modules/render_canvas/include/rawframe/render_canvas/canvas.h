#pragma once

// The 2D canvas's CPU half (SPEC-0024's canvas contract): entities with
// `rawframe.canvas` sprites, copied out of a World where their poses put
// them, then culled against a camera, put in draw order, and batched by
// texture into quads a device draws; and the particle emitters, trails,
// and beams of `rawframe.model` where their 2D poses put them, accounted
// for the view as the scene's are (ADR-0053's one grammar for 2D and 3D,
// D357). Client only; a server carries the same components as plain values
// and links none of this.

#include "rawframe/kest/program.h"
#include "rawframe/material/canvas.h"
#include "rawframe/particles/particles.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/view/view.h"
#include "rawframe/world/world.h"
#include "rawframe/world_kest/game_files.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::render_canvas {

/// `rawframe.canvas.Sprite` as C++ reads it; checked against the program's
/// layout when a game's canvas loads.
struct Sprite {
    std::uint64_t texture = 0;
    float u0 = 0;
    float v0 = 0;
    float u1 = 1;
    float v1 = 1;
    float width = 1;
    float height = 1;
    float pivotX = 0.5F;
    float pivotY = 0.5F;
    std::uint32_t color = 0xFFFFFFFF;
    std::int32_t layer = 0;
    /// The cell of a sprite sheet, counted along rows of `columns` (nought
    /// is one), the region the first.
    std::uint32_t frame = 0;
    std::uint32_t columns = 0;
    /// A game's canvas material, nought for none (D356).
    std::uint64_t material = 0;
};

/// `rawframe.canvas.Camera` as C++ reads it (D261): a client's view,
/// `height` meters tall, centered `offsetX`, `offsetY` meters from its
/// player's pose.
struct Camera {
    float offsetX = 0;
    float offsetY = 0;
    float height = 0;
};

/// A sprite as the extract stage copies it out of the World: owned values,
/// placed where its entity's `rawframe.physics2d.pose` puts it (the origin,
/// unturned, without one).
struct SpriteInstance {
    world::EntityHandle entity;
    /// Which of the game's sprite components it is, by its place.
    std::uint32_t component = 0;
    Sprite sprite;
    double x = 0;
    double y = 0;
    /// The pose's turn as its cosine and sine.
    float c = 1;
    float s = 0;
};

/// Generation 1's view: orthographic, `height` meters tall around (`x`,
/// `y`), `aspect` times as wide; and the seconds since the frame before,
/// which move the particle clock (D357).
struct CanvasCamera {
    double x = 0;
    double y = 0;
    float height = 10;
    float aspect = 16.0F / 9.0F;
    float elapsed = 0;
};

/// A camera's view geometry for picking (ADR-0052, D366): the World point
/// at the view's middle and how many meters it sees from top to bottom;
/// the view's size is the caller's.
[[nodiscard]] view::Orthographic orthographicOf(const CanvasCamera& camera) noexcept;

/// A quad's corner in clip space, x right and y up, each from minus one to
/// one across the view; its place in the texture; and its color, 0xRRGGBBAA.
struct CanvasVertex {
    float x = 0;
    float y = 0;
    float u = 0;
    float v = 0;
    std::uint32_t color = 0;
};

/// Consecutive quads of one texture and one material, drawn by one
/// command: the texture nought for white (a sprite with a material and no
/// texture), the material its place among the frame's, nought for none.
struct CanvasDraw {
    std::uint64_t texture = 0;
    std::uint32_t material = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

/// What the queue stage builds: four vertices and six indices a quad, in
/// draw order, and the draws that cover them. Drawing the draws in order is
/// drawing each sprite in order (SPEC-0024's order-equivalent batching).
struct CanvasFrame {
    std::vector<CanvasVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<CanvasDraw> draws;
    /// The game's canvas materials the draws name by place, the first none:
    /// white, over what is behind (D356).
    std::span<const material::CanvasMaterial> materials;
    /// This frame's sprites drawn, and those not: outside the view, drawing
    /// nothing (alpha nought, or texture nought without a material),
    /// malformed (a side not above nought, a value not finite), naming a
    /// texture or a material the game does not declare, or past a limit.
    std::size_t drawn = 0;
    /// Of those drawn, the ones showing a sheet's cell past its first.
    std::size_t animated = 0;
    std::size_t culled = 0;
    std::size_t hidden = 0;
    std::size_t malformed = 0;
    std::size_t unknownTextures = 0;
    std::size_t unknownMaterials = 0;
    std::size_t overLimit = 0;
    /// The particle emitters, trails, and beams that reach the view, and
    /// what they spawn and leave out (D357), a material named by its place
    /// among the frame's materials and drawn over every sprite; and the
    /// view's half width and half height, meters, nought where it sees
    /// nothing.
    rawframe::particles::Frame particles;
    std::array<float, 2> extent{};
};

/// SPEC-0024's limit points for the canvas: the sprites one frame queues,
/// and its `recorded_commands_per_frame`, the draws. Sprites past either are
/// left out from the last in draw order, counted as over the limit.
struct CanvasLimits {
    std::size_t maximumSprites = 16384;
    std::size_t maximumDraws = 1024;
    /// ADR-0053's particle, trail, and beam limit points (D357).
    rawframe::particles::Limits particles;
};

struct CanvasSettings {
    /// The game's components of `rawframe.canvas.Sprite`'s type, in its
    /// order: an entity may show one of each.
    std::vector<schema::ComponentTypeId> sprites;
    /// The textures the game declares, by identity.
    std::vector<std::uint64_t> textures;
    /// The game's canvas materials, by the identities its `material` lines
    /// give them (D356); one it declares but a client could not read is
    /// given as none.
    std::vector<std::pair<std::uint64_t, material::CanvasMaterial>> materials;
    /// The game's particle emitter, trail, and beam components (D357).
    std::vector<schema::ComponentTypeId> emitters;
    std::vector<schema::ComponentTypeId> trails;
    std::vector<schema::ComponentTypeId> beams;
    CanvasLimits limits;
};

class Canvas {
public:
    [[nodiscard]] static result::Result<std::unique_ptr<Canvas>> create(const schema::SchemaRegistry& registry,
                                                                        CanvasSettings settings);

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    ~Canvas();

    /// The extract stage, in `presentation_extract`: every sprite, emitter,
    /// trail, and beam of the World, copied with its pose; read only (the
    /// World is not const because its queries cache what they matched).
    /// Nothing of it is kept.
    void extract(world::World& world);

    /// The queue stage, in `present`: the extracted sprites seen through
    /// `camera`, drawn in the order of their layers, then of their entities,
    /// then of their components; and the particles' frame (D357).
    const CanvasFrame& queue(const CanvasCamera& camera);

    [[nodiscard]] std::span<const SpriteInstance> extracted() const noexcept;

private:
    struct State;
    explicit Canvas(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// A game's canvas, declared: its sprite components, its textures, its
/// camera component, if it has one, and its particle emitter, trail, and
/// beam components (D357).
struct GameCanvas {
    std::vector<schema::ComponentTypeId> sprites;
    std::vector<std::uint64_t> textures;
    std::optional<schema::ComponentTypeId> camera;
    std::vector<schema::ComponentTypeId> emitters;
    std::vector<schema::ComponentTypeId> trails;
    std::vector<schema::ComponentTypeId> beams;
};

/// Finds the game's components of `rawframe.canvas.Sprite`'s type, its one
/// of `rawframe.canvas.Camera`'s, and those of the particle triad's types,
/// whose layouts in `program` must be what this engine reads, and its
/// `texture` lines. Refuses (`NotFound`) a
/// game with no sprite component, and (`BadComponents`) one with two
/// cameras.
[[nodiscard]] result::Result<GameCanvas> loadGameCanvas(const world_kest::GameFiles& game,
                                                        const kest::Program& program);

} // namespace rawframe::render_canvas
