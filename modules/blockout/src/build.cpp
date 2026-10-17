#include "rawframe/blockout/blockout.h"
#include "rawframe/mesh/errors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

namespace rawframe::blockout {

namespace {

using Point = std::array<double, 3>;

/// Closer than this to a plane is on it.
constexpr double kOnPlane = 1e-5;

Point minus(const Point& a, const Point& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

double dot(const Point& a, const Point& b) noexcept {
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

Point lerp(const Point& a, const Point& b, double t) noexcept {
    return {a[0] + ((b[0] - a[0]) * t), a[1] + ((b[1] - a[1]) * t), a[2] + ((b[2] - a[2]) * t)};
}

struct Plane {
    Point normal{};
    double w = 0;
};

/// A convex face, its corners counter-clockwise seen from outside, and the
/// plane they lie on.
struct Polygon {
    std::vector<Point> corners;
    Plane plane;
};

/// The plane of `corners` by Newell's method, its normal of unit length;
/// none for corners that bound no area.
std::optional<Plane> planeOf(const std::vector<Point>& corners) {
    Point normal{};
    for (std::size_t each = 0; each < corners.size(); ++each) {
        const Point& kA = corners[each];
        const Point& kB = corners[(each + 1) % corners.size()];
        normal[0] += (kA[1] - kB[1]) * (kA[2] + kB[2]);
        normal[1] += (kA[2] - kB[2]) * (kA[0] + kB[0]);
        normal[2] += (kA[0] - kB[0]) * (kA[1] + kB[1]);
    }
    const double kLength = std::sqrt(dot(normal, normal));
    if (!(kLength > 1e-12)) {
        return std::nullopt;
    }
    normal = {normal[0] / kLength, normal[1] / kLength, normal[2] / kLength};
    return Plane{.normal = normal, .w = dot(normal, corners[0])};
}

void flip(Polygon& polygon) noexcept {
    std::ranges::reverse(polygon.corners);
    polygon.plane.normal = {-polygon.plane.normal[0], -polygon.plane.normal[1], -polygon.plane.normal[2]};
    polygon.plane.w = -polygon.plane.w;
}

/// `polygon` split by `plane` (csg.js's splitPolygon): on it, to the
/// front or back by its facing; else in front, behind, or both.
void split(const Plane& plane,
           const Polygon& polygon,
           std::vector<Polygon>& coplanarFront,
           std::vector<Polygon>& coplanarBack,
           std::vector<Polygon>& front,
           std::vector<Polygon>& back) {
    enum : std::uint8_t {
        kOn = 0,
        kFront = 1,
        kBack = 2,
        kSpanning = 3
    };
    std::uint8_t kind = kOn;
    std::vector<std::uint8_t> sides;
    sides.reserve(polygon.corners.size());
    for (const Point& kCorner : polygon.corners) {
        const double kDistance = dot(plane.normal, kCorner) - plane.w;
        const std::uint8_t kSide = kDistance < -kOnPlane ? kBack : kDistance > kOnPlane ? kFront : kOn;
        kind |= kSide;
        sides.push_back(kSide);
    }
    if (kind == kOn) {
        (dot(plane.normal, polygon.plane.normal) > 0 ? coplanarFront : coplanarBack).push_back(polygon);
    } else if (kind == kFront) {
        front.push_back(polygon);
    } else if (kind == kBack) {
        back.push_back(polygon);
    } else {
        Polygon ahead{.plane = polygon.plane};
        Polygon behind{.plane = polygon.plane};
        for (std::size_t each = 0; each < polygon.corners.size(); ++each) {
            const std::size_t kNext = (each + 1) % polygon.corners.size();
            const std::uint8_t kThis = sides[each];
            const std::uint8_t kThat = sides[kNext];
            const Point& kA = polygon.corners[each];
            const Point& kB = polygon.corners[kNext];
            if (kThis != kBack) {
                ahead.corners.push_back(kA);
            }
            if (kThis != kFront) {
                behind.corners.push_back(kA);
            }
            if ((kThis | kThat) == kSpanning) {
                const double kT = (plane.w - dot(plane.normal, kA)) / dot(plane.normal, minus(kB, kA));
                const Point kAt = lerp(kA, kB, kT);
                ahead.corners.push_back(kAt);
                behind.corners.push_back(kAt);
            }
        }
        if (ahead.corners.size() >= 3) {
            front.push_back(std::move(ahead));
        }
        if (behind.corners.size() >= 3) {
            back.push_back(std::move(behind));
        }
    }
}

/// A BSP tree of polygons (csg.js's Node), its nodes in one list so it is
/// built and walked without recursion: a crowded solid would otherwise go
/// as deep as its polygons.
class Tree {
public:
    explicit Tree(std::vector<Polygon> polygons) {
        nodes_.emplace_back();
        build(std::move(polygons));
    }

    void build(std::vector<Polygon> polygons) {
        std::vector<std::pair<std::size_t, std::vector<Polygon>>> work;
        work.emplace_back(0, std::move(polygons));
        while (!work.empty()) {
            auto [at, held] = std::move(work.back());
            work.pop_back();
            if (held.empty()) {
                continue;
            }
            if (!nodes_[at].plane.has_value()) {
                nodes_[at].plane = held.front().plane;
            }
            std::vector<Polygon> front;
            std::vector<Polygon> back;
            const Plane kPlane = *nodes_[at].plane;
            for (const Polygon& kPolygon : held) {
                split(kPlane, kPolygon, nodes_[at].polygons, nodes_[at].polygons, front, back);
            }
            if (!front.empty()) {
                if (!nodes_[at].front.has_value()) {
                    nodes_[at].front = nodes_.size();
                    nodes_.emplace_back();
                }
                work.emplace_back(*nodes_[at].front, std::move(front));
            }
            if (!back.empty()) {
                if (!nodes_[at].back.has_value()) {
                    nodes_[at].back = nodes_.size();
                    nodes_.emplace_back();
                }
                work.emplace_back(*nodes_[at].back, std::move(back));
            }
        }
    }

    /// Solid space and empty space swapped.
    void invert() {
        for (Node& node : nodes_) {
            for (Polygon& polygon : node.polygons) {
                flip(polygon);
            }
            if (node.plane.has_value()) {
                node.plane->normal = {-node.plane->normal[0], -node.plane->normal[1], -node.plane->normal[2]};
                node.plane->w = -node.plane->w;
            }
            std::swap(node.front, node.back);
        }
    }

    /// Of `polygons`, what lies outside this tree's solid.
    std::vector<Polygon> clip(std::vector<Polygon> polygons) const {
        std::vector<Polygon> kept;
        std::vector<std::pair<std::size_t, std::vector<Polygon>>> work;
        work.emplace_back(0, std::move(polygons));
        while (!work.empty()) {
            auto [at, held] = std::move(work.back());
            work.pop_back();
            const Node& kNode = nodes_[at];
            if (!kNode.plane.has_value()) {
                std::ranges::move(held, std::back_inserter(kept));
                continue;
            }
            std::vector<Polygon> front;
            std::vector<Polygon> back;
            for (const Polygon& kPolygon : held) {
                split(*kNode.plane, kPolygon, front, back, front, back);
            }
            if (kNode.front.has_value()) {
                work.emplace_back(*kNode.front, std::move(front));
            } else {
                std::ranges::move(front, std::back_inserter(kept));
            }
            if (kNode.back.has_value()) {
                work.emplace_back(*kNode.back, std::move(back));
            }
        }
        return kept;
    }

    /// Every polygon of this tree with what lies inside `other` taken away.
    void clipTo(const Tree& other) {
        for (Node& node : nodes_) {
            node.polygons = other.clip(std::move(node.polygons));
        }
    }

    [[nodiscard]] std::vector<Polygon> polygons() const {
        std::vector<Polygon> made;
        for (const Node& kNode : nodes_) {
            made.insert(made.end(), kNode.polygons.begin(), kNode.polygons.end());
        }
        return made;
    }

private:
    struct Node {
        std::optional<Plane> plane;
        std::optional<std::size_t> front;
        std::optional<std::size_t> back;
        std::vector<Polygon> polygons;
    };
    std::vector<Node> nodes_;
};

/// `solid` less `cutter` (csg.js's subtract).
std::vector<Polygon> subtract(std::vector<Polygon> solid, std::vector<Polygon> cutter) {
    if (solid.empty() || cutter.empty()) {
        return solid;
    }
    Tree a{std::move(solid)};
    Tree b{std::move(cutter)};
    a.invert();
    a.clipTo(b);
    b.clipTo(a);
    b.invert();
    b.clipTo(a);
    b.invert();
    a.build(b.polygons());
    a.invert();
    return a.polygons();
}

/// The faces of a solid, each a loop of its corners: placed, turned, and
/// facing out from the solid's middle, which a convex solid's faces do.
std::vector<Polygon> facesOf(const Solid& solid) {
    // Corners about the base's middle, before the turn.
    std::vector<Point> corners;
    std::vector<std::vector<std::size_t>> loops;
    const double kHalfX = solid.size[0] / 2;
    const double kHeight = solid.size[1];
    const double kHalfZ = solid.size[2] / 2;
    const auto kCorner = [&](int x, int y, int z) {
        return Point{x != 0 ? kHalfX : -kHalfX, y != 0 ? kHeight : 0, z != 0 ? kHalfZ : -kHalfZ};
    };
    if (solid.shape == Shape::Box || solid.shape == Shape::Ramp) {
        // Corner `i` has x, y, z bits 4, 2, 1.
        for (int each = 0; each < 8; ++each) {
            corners.push_back(kCorner(each & 4, each & 2, each & 1));
        }
        if (solid.shape == Shape::Box) {
            loops = {{0, 2, 3, 1}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 5, 7, 3}};
        } else {
            // Rising from the -Z edge (y nought) to the +Z edge (the top).
            loops = {{0, 4, 5, 1}, {1, 5, 7, 3}, {0, 4, 7, 3}, {0, 1, 3}, {4, 5, 7}};
        }
    } else {
        // A cylinder's rim or an extrusion's base, then the same raised.
        std::vector<std::array<double, 2>> ring = solid.points;
        if (solid.shape == Shape::Cylinder) {
            ring.clear();
            for (std::uint32_t each = 0; each < solid.sides; ++each) {
                const double kAngle = 2 * std::numbers::pi * each / solid.sides;
                ring.push_back({kHalfX * std::cos(kAngle), kHalfZ * std::sin(kAngle)});
            }
        }
        const std::size_t kCount = ring.size();
        for (const auto& kPoint : ring) {
            corners.push_back({kPoint[0], 0, kPoint[1]});
        }
        for (const auto& kPoint : ring) {
            corners.push_back({kPoint[0], kHeight, kPoint[1]});
        }
        std::vector<std::size_t> bottom;
        std::vector<std::size_t> top;
        for (std::size_t each = 0; each < kCount; ++each) {
            bottom.push_back(each);
            top.push_back(kCount + each);
            const std::size_t kNext = (each + 1) % kCount;
            loops.push_back({each, kNext, kCount + kNext, kCount + each});
        }
        loops.push_back(bottom);
        loops.push_back(top);
    }
    // Turned about +Y, then placed.
    const double kTurn = solid.turn * std::numbers::pi / 180;
    const double kCos = std::cos(kTurn);
    const double kSin = std::sin(kTurn);
    for (Point& corner : corners) {
        corner = {(corner[0] * kCos) + (corner[2] * kSin) + solid.at[0],
                  corner[1] + solid.at[1],
                  (-corner[0] * kSin) + (corner[2] * kCos) + solid.at[2]};
    }
    // The middle of the corners its faces use: a ramp leaves two of the
    // box's out, and the box's middle lies on its slope.
    std::vector<bool> used(corners.size(), false);
    for (const std::vector<std::size_t>& kLoop : loops) {
        for (const std::size_t kAt : kLoop) {
            used[kAt] = true;
        }
    }
    Point middle{};
    double counted = 0;
    for (std::size_t each = 0; each < corners.size(); ++each) {
        if (used[each]) {
            middle = {middle[0] + corners[each][0], middle[1] + corners[each][1], middle[2] + corners[each][2]};
            ++counted;
        }
    }
    middle = {middle[0] / counted, middle[1] / counted, middle[2] / counted};
    std::vector<Polygon> faces;
    for (const std::vector<std::size_t>& kLoop : loops) {
        Polygon face;
        for (const std::size_t kAt : kLoop) {
            face.corners.push_back(corners[kAt]);
        }
        const std::optional<Plane> kPlane = planeOf(face.corners);
        if (!kPlane.has_value()) {
            continue;
        }
        face.plane = *kPlane;
        if (dot(face.plane.normal, minus(face.corners[0], middle)) < 0) {
            flip(face);
        }
        faces.push_back(std::move(face));
    }
    return faces;
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(result::ErrorClass::ResourceExhausted,
                        mesh::kMeshDomain,
                        mesh::code(mesh::MeshError::OverLimit),
                        std::string{why});
}

} // namespace

Footprint footprintOf(const Solid& solid) {
    // The corners its faces use, as they are made, so the plan and the
    // mesh agree on where a solid is.
    Footprint made{.minX = std::numeric_limits<double>::infinity(),
                   .minZ = std::numeric_limits<double>::infinity(),
                   .maxX = -std::numeric_limits<double>::infinity(),
                   .maxZ = -std::numeric_limits<double>::infinity()};
    for (const Polygon& kFace : facesOf(solid)) {
        for (const Point& kCorner : kFace.corners) {
            made = {.minX = std::min(made.minX, kCorner[0]),
                    .minZ = std::min(made.minZ, kCorner[2]),
                    .maxX = std::max(made.maxX, kCorner[0]),
                    .maxZ = std::max(made.maxZ, kCorner[2])};
        }
    }
    return made;
}

result::Result<mesh::Mesh> meshOf(const Blockout& blockout, const BlockoutLimits& limits) {
    // Each solid's faces; a cut takes itself out of every solid above it.
    std::vector<std::vector<Polygon>> kept;
    for (const Solid& kSolid : blockout.solids) {
        std::vector<Polygon> faces = facesOf(kSolid);
        if (!kSolid.cut) {
            kept.push_back(std::move(faces));
            continue;
        }
        for (std::vector<Polygon>& solid : kept) {
            solid = subtract(std::move(solid), faces);
        }
    }
    std::size_t triangles = 0;
    for (const auto& kSolid : kept) {
        for (const Polygon& kFace : kSolid) {
            triangles += kFace.corners.size() - 2;
        }
    }
    if (triangles > limits.triangles) {
        return overLimit("a blockout makes " + std::to_string(triangles) + " triangles, past the limit of " +
                         std::to_string(limits.triangles));
    }
    // Each face fanned into triangles, its corners its own so its normal is
    // flat, its coordinates taken on the axis it faces most.
    mesh::Mesh made;
    const auto kVertex = [&made](const Point& at, const Point& normal) {
        const double kX = std::abs(normal[0]);
        const double kY = std::abs(normal[1]);
        const double kZ = std::abs(normal[2]);
        const std::array<double, 2> kUv = kX >= kY && kX >= kZ ? std::array{at[2], -at[1]}
                                          : kY >= kZ           ? std::array{at[0], at[2]}
                                                               : std::array{at[0], -at[1]};
        made.positions.push_back({static_cast<float>(at[0]), static_cast<float>(at[1]), static_cast<float>(at[2])});
        made.normals.push_back(
            {static_cast<float>(normal[0]), static_cast<float>(normal[1]), static_cast<float>(normal[2])});
        made.uvs.push_back({static_cast<float>(kUv[0]), static_cast<float>(kUv[1])});
        return static_cast<std::uint32_t>(made.positions.size() - 1);
    };
    for (const auto& kSolid : kept) {
        for (const Polygon& kFace : kSolid) {
            const std::uint32_t kFirst = kVertex(kFace.corners[0], kFace.plane.normal);
            std::uint32_t previous = kVertex(kFace.corners[1], kFace.plane.normal);
            for (std::size_t each = 2; each < kFace.corners.size(); ++each) {
                const std::uint32_t kNext = kVertex(kFace.corners[each], kFace.plane.normal);
                made.indices.insert(made.indices.end(), {kFirst, previous, kNext});
                previous = kNext;
            }
        }
    }
    made.parts.push_back(mesh::Part{.indexCount = static_cast<std::uint32_t>(made.indices.size())});
    return made;
}

} // namespace rawframe::blockout
