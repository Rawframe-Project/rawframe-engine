#include "rawframe/render_scene/scene.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace rawframe::render_scene {

namespace {

/// The meshes' sides are this many parts of a turn; a sphere has half as
/// many rings.
constexpr std::uint32_t kSegments = 32;

using mesh::Vector3;

Vector3 cross(const Vector3& a, const Vector3& b) noexcept {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

float dot(const Vector3& a, const Vector3& b) noexcept {
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

Vector3 minus(const Vector3& a, const Vector3& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

/// Adds a triangle counter-clockwise seen from where its vertices' normals
/// point: whichever way it was given, it faces out.
void triangle(mesh::Mesh& made, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const Vector3 kFacing =
        cross(minus(made.positions[b], made.positions[a]), minus(made.positions[c], made.positions[a]));
    const Vector3 kOut = {made.normals[a][0] + made.normals[b][0] + made.normals[c][0],
                          made.normals[a][1] + made.normals[b][1] + made.normals[c][1],
                          made.normals[a][2] + made.normals[b][2] + made.normals[c][2]};
    if (dot(kFacing, kOut) < 0) {
        std::swap(b, c);
    }
    made.indices.insert(made.indices.end(), {a, b, c});
}

void vertex(mesh::Mesh& made, const Vector3& position, const Vector3& normal, float u, float v) {
    made.positions.push_back(position);
    made.normals.push_back(normal);
    made.uvs.push_back({u, v});
}

/// Every face of a cube of half sides one, four corners each so its edges
/// are sharp.
mesh::Mesh box() {
    mesh::Mesh made;
    // Each face's normal and two directions along it.
    constexpr std::array<std::array<Vector3, 3>, 6> kFaces = {{
        {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}},
        {{{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}},
        {{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}},
        {{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}},
        {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}},
        {{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}},
    }};
    for (const auto& [kNormal, kU, kV] : kFaces) {
        const auto kFirst = static_cast<std::uint32_t>(made.positions.size());
        for (const auto& [kSu, kSv] : {std::pair{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}) {
            vertex(made,
                   {kNormal[0] + (kSu * kU[0]) + (kSv * kV[0]),
                    kNormal[1] + (kSu * kU[1]) + (kSv * kV[1]),
                    kNormal[2] + (kSu * kU[2]) + (kSv * kV[2])},
                   kNormal,
                   (kSu + 1) / 2,
                   (1 - kSv) / 2);
        }
        triangle(made, kFirst, kFirst + 1, kFirst + 2);
        triangle(made, kFirst + 2, kFirst + 3, kFirst);
    }
    return made;
}

/// A ring of latitude: its angle from the top, and how far its circle is
/// moved up or down.
struct Ring {
    float angle = 0;
    float lift = 0;
};

/// A closed surface of rings about +Y, each `kSegments` around, its
/// normals those of a sphere: a sphere, or a capsule whose two halves are
/// lifted apart.
mesh::Mesh rings(std::span<const Ring> made) {
    mesh::Mesh shape;
    for (std::size_t ring = 0; ring < made.size(); ++ring) {
        const float kSin = std::sin(made[ring].angle);
        const float kCos = std::cos(made[ring].angle);
        for (std::uint32_t segment = 0; segment <= kSegments; ++segment) {
            const float kAround = 2 * std::numbers::pi_v<float> * static_cast<float>(segment) / kSegments;
            const Vector3 kNormal = {kSin * std::sin(kAround), kCos, kSin * std::cos(kAround)};
            vertex(shape,
                   {kNormal[0], kNormal[1] + made[ring].lift, kNormal[2]},
                   kNormal,
                   static_cast<float>(segment) / kSegments,
                   static_cast<float>(ring) / static_cast<float>(made.size() - 1));
        }
    }
    constexpr std::uint32_t kAcross = kSegments + 1;
    for (std::uint32_t ring = 0; ring + 1 < made.size(); ++ring) {
        for (std::uint32_t segment = 0; segment < kSegments; ++segment) {
            const std::uint32_t kA = (ring * kAcross) + segment;
            const std::uint32_t kB = kA + kAcross;
            // The poles' rings are points: their degenerate halves are left
            // out.
            if (ring != 0) {
                triangle(shape, kA, kB, kA + 1);
            }
            if (ring + 2 != made.size()) {
                triangle(shape, kA + 1, kB, kB + 1);
            }
        }
    }
    return shape;
}

mesh::Mesh sphere() {
    std::vector<Ring> made;
    for (std::uint32_t ring = 0; ring <= kSegments / 2; ++ring) {
        made.push_back({.angle = std::numbers::pi_v<float> * static_cast<float>(ring) / (kSegments / 2)});
    }
    return rings(made);
}

mesh::Mesh capsule() {
    // The top half lifted one, the bottom half lowered one, and the equator
    // twice, so the band between is the capsule's side.
    std::vector<Ring> made;
    for (std::uint32_t ring = 0; ring <= kSegments / 4; ++ring) {
        made.push_back({.angle = std::numbers::pi_v<float> * static_cast<float>(ring) / (kSegments / 2), .lift = 1});
    }
    for (std::uint32_t ring = kSegments / 4; ring <= kSegments / 2; ++ring) {
        made.push_back({.angle = std::numbers::pi_v<float> * static_cast<float>(ring) / (kSegments / 2), .lift = -1});
    }
    return rings(made);
}

mesh::Mesh cylinder() {
    mesh::Mesh made;
    // The side, then each cap: a center and its rim.
    for (std::uint32_t segment = 0; segment <= kSegments; ++segment) {
        const float kAround = 2 * std::numbers::pi_v<float> * static_cast<float>(segment) / kSegments;
        const Vector3 kNormal = {std::sin(kAround), 0, std::cos(kAround)};
        const float kU = static_cast<float>(segment) / kSegments;
        vertex(made, {kNormal[0], 1, kNormal[2]}, kNormal, kU, 0);
        vertex(made, {kNormal[0], -1, kNormal[2]}, kNormal, kU, 1);
    }
    for (std::uint32_t segment = 0; segment < kSegments; ++segment) {
        const std::uint32_t kTop = segment * 2;
        triangle(made, kTop, kTop + 1, kTop + 2);
        triangle(made, kTop + 2, kTop + 1, kTop + 3);
    }
    for (const float kY : {1.0F, -1.0F}) {
        const auto kCenter = static_cast<std::uint32_t>(made.positions.size());
        vertex(made, {0, kY, 0}, {0, kY, 0}, 0.5F, 0.5F);
        for (std::uint32_t segment = 0; segment <= kSegments; ++segment) {
            const float kAround = 2 * std::numbers::pi_v<float> * static_cast<float>(segment) / kSegments;
            vertex(made,
                   {std::sin(kAround), kY, std::cos(kAround)},
                   {0, kY, 0},
                   (std::sin(kAround) + 1) / 2,
                   (1 - std::cos(kAround)) / 2);
        }
        for (std::uint32_t segment = 0; segment < kSegments; ++segment) {
            triangle(made, kCenter, kCenter + 1 + segment, kCenter + 2 + segment);
        }
    }
    return made;
}

std::shared_ptr<const mesh::Mesh> finished(mesh::Mesh made) {
    made.parts.push_back({.firstIndex = 0, .indexCount = static_cast<std::uint32_t>(made.indices.size())});
    return std::make_shared<const mesh::Mesh>(std::move(made));
}

} // namespace

std::shared_ptr<const mesh::Mesh> engineMesh(std::uint64_t id) {
    switch (id) {
    case kBox:
        return finished(box());
    case kSphere:
        return finished(sphere());
    case kCylinder:
        return finished(cylinder());
    case kCapsule:
        return finished(capsule());
    default:
        return nullptr;
    }
}

} // namespace rawframe::render_scene
