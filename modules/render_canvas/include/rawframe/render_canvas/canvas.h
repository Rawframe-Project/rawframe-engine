#pragma once

// The 2D canvas's CPU half (SPEC-0024's canvas contract): entities with
// `rawframe.canvas` sprites, copied out of a World where their poses put
// them, then culled against a camera, put in draw order, and batched by
// texture into quads a device draws. Client only; a server carries the same
// components as plain values and links none of this.

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/world.h"
#include "rawframe/world_kest/game_files.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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
};

/// A sprite as the extract stage copies it out of the World: owned values,
/// placed where its entity's `rawframe.physics2d.pose` puts it (the origin,
/// unturned, without one).
struct SpriteInstance {
    world::EntityHandle entity;
    Sprite sprite;
    double x = 0;
    double y = 0;
    /// The pose's turn as its cosine and sine.
    float c = 1;
    float s = 0;
};

/// Generation 1's view: orthographic, `height` meters tall around (`x`,
/// `y`), `aspect` times as wide.
struct CanvasCamera {
    double x = 0;
    double y = 0;
    float height = 10;
    float aspect = 16.0F / 9.0F;
};

/// A quad's corner in clip space, x right and y up, each from minus one to
/// one across the view; its place in the texture; and its color, 0xRRGGBBAA.
struct CanvasVertex {
    float x = 0;
    float y = 0;
    float u = 0;
    float v = 0;
    std::uint32_t color = 0;
};

/// Consecutive quads of one texture, drawn by one command.
struct CanvasDraw {
    std::uint64_t texture = 0;
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
    /// This frame's sprites drawn, and those not: outside the view, drawing
    /// nothing (texture or alpha nought), malformed (a side not above
    /// nought, a value not finite), naming a texture the game does not
    /// declare, or past a limit.
    std::size_t drawn = 0;
    std::size_t culled = 0;
    std::size_t hidden = 0;
    std::size_t malformed = 0;
    std::size_t unknownTextures = 0;
    std::size_t overLimit = 0;
};

/// SPEC-0024's limit points for the canvas: the sprites one frame queues,
/// and its `recorded_commands_per_frame`, the draws. Sprites past either are
/// left out from the last in draw order, counted as over the limit.
struct CanvasLimits {
    std::size_t maximumSprites = 16384;
    std::size_t maximumDraws = 1024;
};

struct CanvasSettings {
    /// The game's component of `rawframe.canvas.Sprite`'s type.
    schema::ComponentTypeId sprite;
    /// The textures the game declares, by identity.
    std::vector<std::uint64_t> textures;
    CanvasLimits limits;
};

class Canvas {
public:
    [[nodiscard]] static result::Result<std::unique_ptr<Canvas>> create(const schema::SchemaRegistry& registry,
                                                                        CanvasSettings settings);

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    ~Canvas();

    /// The extract stage, in `presentation_extract`: every sprite of the
    /// World, copied with its pose; read only (the World is not const
    /// because its queries cache what they matched). Nothing of it is kept.
    void extract(world::World& world);

    /// The queue stage, in `present`: the extracted sprites seen through
    /// `camera`, drawn in the order of their layers, then of their entities.
    const CanvasFrame& queue(const CanvasCamera& camera);

    [[nodiscard]] std::span<const SpriteInstance> extracted() const noexcept;

private:
    struct State;
    explicit Canvas(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// A game's canvas, declared: its sprite component, its textures, and its
/// camera's height.
struct GameCanvas {
    schema::ComponentTypeId sprite;
    std::vector<std::uint64_t> textures;
    float cameraHeight = 10;
};

/// Finds the game's component of `rawframe.canvas.Sprite`'s type, whose
/// layout in `program` must be what this module reads, and its `texture`
/// and `camera` lines. Refuses (`NotFound`) a game with no sprite component.
[[nodiscard]] result::Result<GameCanvas> loadGameCanvas(const world_kest::GameFiles& game,
                                                        const kest::Program& program);

} // namespace rawframe::render_canvas
