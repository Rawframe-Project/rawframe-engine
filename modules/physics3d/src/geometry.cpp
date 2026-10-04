#include "geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace rawframe::physics3d {

namespace {

/// Adds a point of the body's own frame, turned and moved into the World.
void addPoint(StaticGeometry& into, const m3Transform& frame, Turned local) {
    const Turned kWorld = toWorld(frame, local);
    into.vertices.push_back({kWorld.x, kWorld.y, kWorld.z});
}

/// A box of half sides `x`, `y`, and `z`: its eight corners, then its six
/// faces wound outward.
void addBox(StaticGeometry& into, const m3Transform& frame, double x, double y, double z) {
    const auto kFirst = static_cast<std::int32_t>(into.vertices.size());
    for (int corner = 0; corner < 8; ++corner) {
        addPoint(
            into, frame, Turned{(corner & 1) != 0 ? x : -x, (corner & 2) != 0 ? y : -y, (corner & 4) != 0 ? z : -z});
    }
    constexpr std::array<std::int32_t, 36> kFaces = {2, 6, 7, 2, 7, 3, 0, 1, 5, 0, 5, 4, 4, 5, 7, 4, 7, 6,
                                                     0, 2, 3, 0, 3, 1, 1, 3, 7, 1, 7, 5, 0, 4, 6, 0, 6, 2};
    for (const std::int32_t kCorner : kFaces) {
        into.indices.push_back(kFirst + kCorner);
    }
}

/// An upright prism of `sides` round a cylinder of `radius` and half height
/// `half`, as the physics makes it: its rim's points, top then bottom, its
/// caps fanned and its sides, wound outward.
void addPrism(StaticGeometry& into, const m3Transform& frame, double radius, double half, std::int32_t sides) {
    const auto kFirst = static_cast<std::int32_t>(into.vertices.size());
    for (const double kY : {half, -half}) {
        for (std::int32_t side = 0; side < sides; ++side) {
            const double kAngle = 2 * std::numbers::pi * side / sides;
            addPoint(into, frame, Turned{radius * std::cos(kAngle), kY, radius * std::sin(kAngle)});
        }
    }
    for (std::int32_t side = 1; side + 1 < sides; ++side) {
        // Seen from above the top runs clockwise in X and Z, which is
        // counterclockwise seen from +Y with Z toward the viewer.
        into.indices.insert(into.indices.end(), {kFirst, kFirst + side + 1, kFirst + side});
        into.indices.insert(into.indices.end(), {kFirst + sides, kFirst + sides + side, kFirst + sides + side + 1});
    }
    for (std::int32_t side = 0; side < sides; ++side) {
        const std::int32_t kNext = (side + 1) % sides;
        into.indices.insert(into.indices.end(), {kFirst + side, kFirst + kNext, kFirst + sides + kNext});
        into.indices.insert(into.indices.end(), {kFirst + side, kFirst + sides + kNext, kFirst + sides + side});
    }
}

} // namespace

void appendStatic(const std::map<world::EntityHandle, Mapped>& mapped,
                  const std::map<std::uint64_t, PreparedMesh>& meshes,
                  StaticGeometry& into) {
    for (const auto& [kEntity, kMapped] : mapped) {
        const Body3D& kBody = kMapped.made;
        if (kMapped.refused || kBody.motion != static_cast<std::uint8_t>(physics::Motion::Static) || kBody.sensor) {
            continue;
        }
        const m3Transform kFrame = transformOf(kMapped.pose);
        const auto kFirstVertex = into.vertices.size();
        const auto kFirstIndex = static_cast<std::uint32_t>(into.indices.size());
        const double kWidth = kBody.width;
        const double kHeight = kBody.height;
        const double kDepth = kBody.depth;
        switch (static_cast<Shape>(kBody.shape)) {
        case Shape::Box:
            addBox(into, kFrame, kWidth, kHeight, kDepth);
            break;
        case Shape::Sphere:
            addBox(into, kFrame, kWidth, kWidth, kWidth);
            break;
        case Shape::Capsule:
            addBox(into, kFrame, kWidth, kHeight + kWidth, kWidth);
            break;
        case Shape::Cylinder:
            addPrism(into, kFrame, kWidth, kHeight, kCylinderSides);
            break;
        case Shape::Mesh:
            if (const auto kMesh = meshes.find(kMapped.mesh); kMesh != meshes.end()) {
                for (const PreparedMesh::Piece& kPiece : kMesh->second.pieces) {
                    const auto kBase = static_cast<std::int32_t>(into.vertices.size());
                    for (const m3Vec3& kVertex : kPiece.vertices) {
                        addPoint(into, kFrame, Turned{kVertex.x, kVertex.y, kVertex.z});
                    }
                    for (const std::uint16_t kIndex : kPiece.indices) {
                        into.indices.push_back(kBase + kIndex);
                    }
                }
            }
            break;
        }
        if (into.vertices.size() == kFirstVertex) {
            continue;
        }
        StaticShape shape{.entity = kEntity,
                          .firstIndex = kFirstIndex,
                          .indexCount = static_cast<std::uint32_t>(into.indices.size()) - kFirstIndex,
                          .low = into.vertices[kFirstVertex],
                          .high = into.vertices[kFirstVertex]};
        for (std::size_t at = kFirstVertex; at < into.vertices.size(); ++at) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                shape.low[axis] = std::min(shape.low[axis], into.vertices[at][axis]);
                shape.high[axis] = std::max(shape.high[axis], into.vertices[at][axis]);
            }
        }
        into.shapes.push_back(shape);
    }
}

} // namespace rawframe::physics3d
