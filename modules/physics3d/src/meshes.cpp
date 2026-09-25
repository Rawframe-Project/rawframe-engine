#include "meshes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rawframe::physics3d {

namespace {

struct Vector {
    double x = 0;
    double y = 0;
    double z = 0;
};

} // namespace

PreparedMesh prepare(const mesh::Mesh& source) {
    PreparedMesh out;
    std::vector<std::int32_t> local(source.positions.size(), -1);
    std::vector<std::uint32_t> used;
    PreparedMesh::Piece piece;
    const auto kClose = [&] {
        if (!piece.indices.empty()) {
            out.pieces.push_back(std::move(piece));
        }
        piece = {};
        for (const std::uint32_t kGlobal : used) {
            local[kGlobal] = -1;
        }
        used.clear();
    };
    for (std::size_t first = 0; first < source.indices.size(); first += 3) {
        const std::array<std::uint32_t, 3> kCorners = {
            source.indices[first], source.indices[first + 1], source.indices[first + 2]};
        const mesh::Vector3& kA = source.positions[kCorners[0]];
        const mesh::Vector3& kB = source.positions[kCorners[1]];
        const mesh::Vector3& kC = source.positions[kCorners[2]];
        const Vector kAb{double{kB[0]} - kA[0], double{kB[1]} - kA[1], double{kB[2]} - kA[2]};
        const Vector kAc{double{kC[0]} - kA[0], double{kC[1]} - kA[1], double{kC[2]} - kA[2]};
        const Vector kCross{
            (kAb.y * kAc.z) - (kAb.z * kAc.y), (kAb.z * kAc.x) - (kAb.x * kAc.z), (kAb.x * kAc.y) - (kAb.y * kAc.x)};
        if (kCross.x == 0 && kCross.y == 0 && kCross.z == 0) {
            continue;
        }
        // Three new vertices at most: closing a little early is harmless.
        if (piece.vertices.size() + 3 > kPieceMost || piece.indices.size() / 3 == kPieceMost) {
            kClose();
        }
        for (const std::uint32_t kGlobal : kCorners) {
            if (local[kGlobal] < 0) {
                local[kGlobal] = static_cast<std::int32_t>(piece.vertices.size());
                used.push_back(kGlobal);
                const mesh::Vector3& kPoint = source.positions[kGlobal];
                piece.vertices.push_back(m3Vec3{kPoint[0], kPoint[1], kPoint[2]});
            }
            piece.indices.push_back(static_cast<std::uint16_t>(local[kGlobal]));
        }
    }
    kClose();
    for (const mesh::Vector3& kPoint : source.positions) {
        const double kX = kPoint[0];
        const double kY = kPoint[1];
        const double kZ = kPoint[2];
        out.reach = std::max(out.reach, std::sqrt((kX * kX) + (kY * kY) + (kZ * kZ)));
    }
    return out;
}

} // namespace rawframe::physics3d
