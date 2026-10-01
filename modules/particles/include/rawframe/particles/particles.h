#pragma once

// Particles, trails, and beams on a client (ADR-0053, D352, D354, D357):
// the component triad as C++ reads it, and what each view's frame draws of
// them: which emitters reach the view and what each spawns on the particle
// clock, and the ribbons trails and beams make. The scene and the canvas
// both draw through this, one accounting for 2D and 3D; a device draws
// what it gives. Client only: a server carries the components as plain
// values and links none of this.

#include "rawframe/world/entity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::particles {

using Vector = std::array<float, 3>;

/// `rawframe.model.ParticleEmitter` as C++ reads it (D352).
struct ParticleEmitter {
    std::uint64_t material = 0;
    float rate = 0;
    float lifetime = 0;
    float speed = 0;
    float spread = 0;
    float radius = 0;
    float sizeStart = 0;
    float sizeEnd = 0;
    std::uint32_t colorStart = 0xFFFFFFFF;
    std::uint32_t colorEnd = 0xFFFFFFFF;
    float accelerationX = 0;
    float accelerationY = 0;
    float accelerationZ = 0;
    float drag = 0;
    float variation = 0;
    std::uint32_t bursts = 0;
    std::uint32_t burstCount = 0;
    std::uint32_t seed = 0;
};

/// `rawframe.model.Trail` as C++ reads it (D354).
struct Trail {
    std::uint64_t material = 0;
    float lifetime = 0;
    float spacing = 0;
    float widthStart = 0;
    float widthEnd = 0;
    std::uint32_t colorStart = 0xFFFFFFFF;
    std::uint32_t colorEnd = 0xFFFFFFFF;
};

/// `rawframe.model.Beam` as C++ reads it (D354).
struct Beam {
    std::uint64_t material = 0;
    float toX = 0;
    float toY = 0;
    float toZ = 0;
    float bendX = 0;
    float bendY = 0;
    float bendZ = 0;
    std::uint32_t segments = 0;
    float widthStart = 0;
    float widthEnd = 0;
    std::uint32_t colorStart = 0xFFFFFFFF;
    std::uint32_t colorEnd = 0xFFFFFFFF;
    float textureLength = 0;
    float textureSpeed = 0;
};

/// An emitter, trail, or beam as an extract stage copies it out of the
/// World: its entity and which of the game's components of its kind it
/// is, its values, where its entity's pose puts it, and (an emitter's) the
/// way it emits, its pose's +Y.
struct EmitterInstance {
    world::EntityHandle entity;
    std::uint32_t component = 0;
    ParticleEmitter emitter;
    std::array<double, 3> position{};
    Vector way{0, 1, 0};
};
template <typename Ribbon> struct RibbonInstance {
    world::EntityHandle entity;
    std::uint32_t component = 0;
    Ribbon ribbon;
    std::array<double, 3> position{};
};
using TrailInstance = RibbonInstance<Trail>;
using BeamInstance = RibbonInstance<Beam>;

/// The seconds the particle clock runs before it wraps to nought (D352):
/// far past any particle's life, and short enough that a float keeps a
/// twentieth of a millisecond.
inline constexpr float kClockPeriod = 4096;

/// An emitter a frame draws (ADR-0053, D352), its particles moving in
/// closed form from their births: its identity across frames (its entity
/// and component), its material's place among the view's materials; its
/// anchor relative to the eye, where it spawns now relative to its anchor,
/// and the way it emits; its particles' life (seconds), speed (meters a
/// second), the cone's half angle about that way (radians), the sphere
/// about the spawn they start in (meters), their size at birth and death
/// (meters), their color and alpha at birth and death (linear), their
/// constant acceleration (meters a second squared), their drag (a second),
/// and how much each one's life, speed, and size vary (nought to one); its
/// ring of particles: its size, where this frame's spawn starts in it, how
/// many it spawns, the first `steady` born one `step` apart from `born` on
/// the particle clock and the rest (its bursts) at its now; its seed; and
/// how many times its ring has started anew, which a device's ring follows
/// (D353).
struct EmitterDraw {
    std::uint64_t key = 0;
    std::uint32_t material = 0;
    std::array<float, 3> anchor{};
    std::array<float, 3> origin{};
    std::array<float, 3> direction{0, 1, 0};
    float lifetime = 0;
    float speed = 0;
    float spread = 0;
    float radius = 0;
    float sizeStart = 0;
    float sizeEnd = 0;
    std::array<float, 4> colorStart{1, 1, 1, 1};
    std::array<float, 4> colorEnd{1, 1, 1, 1};
    std::array<float, 3> acceleration{};
    float drag = 0;
    float variation = 0;
    std::uint32_t capacity = 0;
    std::uint32_t first = 0;
    std::uint32_t spawned = 0;
    std::uint32_t steady = 0;
    float born = 0;
    float step = 0;
    std::uint32_t seed = 0;
    std::uint32_t ring = 0;
};

/// A point of a ribbon a frame draws (D354): where it is relative to the
/// eye, how wide it is there (meters), its color and alpha there (linear),
/// and its texture's coordinate along the ribbon.
struct RibbonPoint {
    std::array<float, 3> place{};
    float width = 0;
    std::array<float, 4> color{1, 1, 1, 1};
    float along = 0;
};

/// A ribbon a frame draws, a trail's or a beam's (D354): its material's
/// place among the view's materials, and its points among the frame's, at
/// least two.
struct Ribbon {
    std::uint32_t material = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

/// ADR-0053's limit points (D352, D354), profile values: the emitters a
/// view draws, the particles an emitter holds alive, the particles it
/// spawns a second, and the seconds one lives (a trail's points' too); the
/// trails and beams a view draws, the points a trail keeps, and the
/// segments a beam is cut into.
struct Limits {
    std::size_t maximumEmitters = 64;
    std::uint32_t maximumParticlesPerEmitter = 4096;
    float maximumParticleRate = 1000;
    float maximumParticleLifetime = 20;
    std::size_t maximumRibbons = 64;
    std::uint32_t maximumTrailPoints = 256;
    std::uint32_t maximumBeamSegments = 64;
};

/// What a view's frame draws of them: the emitters that reach the view,
/// farthest first, at most the limit; the particle clock now, seconds,
/// wrapping at `kClockPeriod`; and the emitters left out (not sound, or
/// past the limit), those held to a limit point (their rate, life, or
/// ring), and particles a spawn could not hold (a burst past the ring).
/// Then the trails and beams that reach the view as ribbons, farthest
/// first, at most the limit, and their points; those left out (not sound,
/// or past the limit); and those held to a limit point (a trail's points,
/// or a beam's segments).
struct Frame {
    std::vector<EmitterDraw> emitters;
    float clock = 0;
    std::size_t emittersLeftOut = 0;
    std::size_t emittersHeld = 0;
    std::size_t particlesLeftOut = 0;
    std::vector<Ribbon> ribbons;
    std::vector<RibbonPoint> ribbonPoints;
    std::size_t ribbonsLeftOut = 0;
    std::size_t ribbonsHeld = 0;
};

/// A view as the accounting sees it: where its eye is in the World, and
/// whether a sphere `radius` meters about a point relative to the eye may
/// be seen.
struct Viewer {
    std::array<double, 3> eye{};
    std::function<bool(const Vector& center, float radius)> sees;
};

/// One view's particles from frame to frame (D352, D354): the particle
/// clock, which the Host's timeline moves, and what each emitter and trail
/// keeps on the client, dropped once it is gone.
class Particles {
public:
    /// The frame's emitters and ribbons, `elapsed` seconds after the frame
    /// before (at most a quarter second): every emitter whose reach meets
    /// the view spawns steadily at its rate (one not drawn the frame
    /// before owes nothing for the frames it was not) and a burst each time
    /// its `bursts` moved; every trail leaves its points and lets go of
    /// the dead, seen or not; and the nearest of each kind are kept up to
    /// the limits, drawn farthest first. `materials` places each material
    /// identity among the view's; one it does not know is the first.
    void update(Frame& frame,
                std::span<const EmitterInstance> emitters,
                std::span<const TrailInstance> trails,
                std::span<const BeamInstance> beams,
                const Viewer& viewer,
                const std::map<std::uint64_t, std::uint32_t>& materials,
                const Limits& limits,
                float elapsed);

private:
    /// What an emitter keeps: its anchor in the World, the bursts it has
    /// seen, the particles it owes (a fraction), its ring's size and where
    /// its next spawn starts in it, the frame it was last drawn in, and how
    /// many times its ring has started anew (D353).
    struct EmitterHistory {
        std::array<double, 3> anchor{};
        std::uint32_t bursts = 0;
        double owed = 0;
        std::uint32_t capacity = 0;
        std::uint32_t next = 0;
        std::uint64_t drawn = 0;
        std::uint32_t ring = 0;
    };

    /// A point a trail has left: where in the World, and when on the
    /// particle clock (seconds, before wrapping).
    struct TrailPoint {
        std::array<double, 3> position{};
        double left = 0;
    };

    /// An emitter's or trail's identity across frames: its entity and which
    /// of the game's components of its kind it is.
    using Key = std::pair<world::EntityHandle, std::uint32_t>;

    void spawn(Frame& frame,
               std::span<const EmitterInstance> emitters,
               const Viewer& viewer,
               const std::map<std::uint64_t, std::uint32_t>& materials,
               const Limits& limits,
               float elapsed);
    void makeRibbons(Frame& frame,
                     std::span<const TrailInstance> trails,
                     std::span<const BeamInstance> beams,
                     const Viewer& viewer,
                     const std::map<std::uint64_t, std::uint32_t>& materials,
                     const Limits& limits);

    double clock_ = 0;
    std::uint64_t frames_ = 0;
    std::map<Key, EmitterHistory> emitters_;
    std::map<Key, std::deque<TrailPoint>> trails_;
};

} // namespace rawframe::particles
