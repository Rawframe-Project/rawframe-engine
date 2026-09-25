#include "attachments.h"

#include <algorithm>
#include <cmath>

namespace rawframe::physics2d {

namespace {

/// A rotation's cosine and sine made unit, all noughts (or nothing to make
/// unit) as none, as a pose's.
struct Turn {
    double c = 1;
    double s = 0;
};

[[nodiscard]] Turn unit(float c, float s) noexcept {
    const double kLength = std::sqrt((double{c} * c) + (double{s} * s));
    if (!(kLength > 0) || !std::isfinite(kLength)) {
        return Turn{};
    }
    return Turn{c / kLength, s / kLength};
}

/// The parent's pose with the offset in its frame.
[[nodiscard]] Pose2D placed(const Pose2D& parent, const Attach2D& attach) noexcept {
    const Turn kParent = unit(parent.c, parent.s);
    const Turn kOwn = unit(attach.c, attach.s);
    return Pose2D{.x = parent.x + (kParent.c * attach.x) - (kParent.s * attach.y),
                  .y = parent.y + (kParent.s * attach.x) + (kParent.c * attach.y),
                  .c = static_cast<float>((kParent.c * kOwn.c) - (kParent.s * kOwn.s)),
                  .s = static_cast<float>((kParent.s * kOwn.c) + (kParent.c * kOwn.s))};
}

} // namespace

result::Result<Attachments> Attachments::resolve(const schema::SchemaRegistry& registry) {
    Attachments made;
    RAWFRAME_TRY_ASSIGN(made.query_, (world::Query<world::Read<Attach2D>, world::Write<Pose2D>>::resolve(registry)));
    RAWFRAME_TRY_ASSIGN(made.pose_, registry.find(Pose2D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(made.body_, registry.find(Body2D::kComponentTypeId));
    return made;
}

void Attachments::follow(world::World& world, std::uint64_t& refused) {
    rows_.clear();
    query_->forEach(world, [this](world::EntityHandle entity, const Attach2D& attach, Pose2D& pose) {
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
                                        : static_cast<const Pose2D*>(world.getErased(row.attach->parent, pose_));
        if (row.depth > rows_.size() || kParent == nullptr || world.getErased(row.entity, body_) != nullptr) {
            refusedNow.insert(row.entity);
            refused += refused_.contains(row.entity) ? 0U : 1U;
            continue;
        }
        *row.pose = placed(*kParent, *row.attach);
    }
    refused_ = std::move(refusedNow);
}

} // namespace rawframe::physics2d
