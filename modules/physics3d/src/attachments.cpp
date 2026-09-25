#include "attachments.h"

#include <algorithm>
#include <cmath>

namespace rawframe::physics3d {

namespace {

struct Quaternion {
    double x = 0;
    double y = 0;
    double z = 0;
    double w = 1;
};

/// Made unit, all noughts (or nothing to make unit) as none, as a pose's.
[[nodiscard]] Quaternion unit(float x, float y, float z, float w) noexcept {
    const double kLength = std::sqrt((double{x} * x) + (double{y} * y) + (double{z} * z) + (double{w} * w));
    if (!(kLength > 0) || !std::isfinite(kLength)) {
        return Quaternion{};
    }
    return Quaternion{x / kLength, y / kLength, z / kLength, w / kLength};
}

[[nodiscard]] Quaternion times(const Quaternion& a, const Quaternion& b) noexcept {
    return Quaternion{(a.w * b.x) + (a.x * b.w) + (a.y * b.z) - (a.z * b.y),
                      (a.w * b.y) - (a.x * b.z) + (a.y * b.w) + (a.z * b.x),
                      (a.w * b.z) + (a.x * b.y) - (a.y * b.x) + (a.z * b.w),
                      (a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z)};
}

/// The parent's pose with the offset in its frame.
[[nodiscard]] Pose3D placed(const Pose3D& parent, const Attach3D& attach) noexcept {
    const Quaternion kParent = unit(parent.qx, parent.qy, parent.qz, parent.qw);
    // v + 2w (q x v) + 2 q x (q x v)
    const double kX = attach.x;
    const double kY = attach.y;
    const double kZ = attach.z;
    const double kCx = (kParent.y * kZ) - (kParent.z * kY);
    const double kCy = (kParent.z * kX) - (kParent.x * kZ);
    const double kCz = (kParent.x * kY) - (kParent.y * kX);
    const Quaternion kRotation = times(kParent, unit(attach.qx, attach.qy, attach.qz, attach.qw));
    return Pose3D{.x = parent.x + kX + (2 * kParent.w * kCx) + (2 * ((kParent.y * kCz) - (kParent.z * kCy))),
                  .y = parent.y + kY + (2 * kParent.w * kCy) + (2 * ((kParent.z * kCx) - (kParent.x * kCz))),
                  .z = parent.z + kZ + (2 * kParent.w * kCz) + (2 * ((kParent.x * kCy) - (kParent.y * kCx))),
                  .qx = static_cast<float>(kRotation.x),
                  .qy = static_cast<float>(kRotation.y),
                  .qz = static_cast<float>(kRotation.z),
                  .qw = static_cast<float>(kRotation.w)};
}

} // namespace

result::Result<Attachments> Attachments::resolve(const schema::SchemaRegistry& registry) {
    Attachments made;
    RAWFRAME_TRY_ASSIGN(made.query_, (world::Query<world::Read<Attach3D>, world::Write<Pose3D>>::resolve(registry)));
    RAWFRAME_TRY_ASSIGN(made.pose_, registry.find(Pose3D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(made.body_, registry.find(Body3D::kComponentTypeId));
    return made;
}

void Attachments::follow(world::World& world, std::uint64_t& refused) {
    rows_.clear();
    query_->forEach(world, [this](world::EntityHandle entity, const Attach3D& attach, Pose3D& pose) {
        rows_.push_back(Row{.entity = entity, .attach = &attach, .pose = &pose});
    });
    std::ranges::sort(rows_, {}, &Row::entity);
    const auto kRowOf = [this](world::EntityHandle entity) -> Row* {
        const auto kFound = std::ranges::lower_bound(rows_, entity, {}, &Row::entity);
        return kFound != rows_.end() && kFound->entity == entity ? &*kFound : nullptr;
    };
    // How many attachments above each: a chain longer than there are
    // attachments has come round on itself.
    for (Row& row : rows_) {
        std::size_t depth = 0;
        for (const Row* at = &row; at != nullptr && depth <= rows_.size(); at = kRowOf(at->attach->parent)) {
            ++depth;
        }
        row.depth = depth;
    }
    std::ranges::stable_sort(rows_, {}, &Row::depth);
    std::set<world::EntityHandle> refusedNow;
    for (const Row& row : rows_) {
        const auto* const kParent = row.attach->parent.isNull()
                                        ? nullptr
                                        : static_cast<const Pose3D*>(world.getErased(row.attach->parent, pose_));
        if (row.depth > rows_.size() || kParent == nullptr || world.getErased(row.entity, body_) != nullptr) {
            refusedNow.insert(row.entity);
            refused += refused_.contains(row.entity) ? 0U : 1U;
            continue;
        }
        *row.pose = placed(*kParent, *row.attach);
    }
    refused_ = std::move(refusedNow);
}

} // namespace rawframe::physics3d
