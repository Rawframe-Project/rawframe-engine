#include "skinning.h"

#include <algorithm>
#include <cstddef>

namespace rawframe::render_scene {

namespace {

/// The bones' lists a scene keeps found joints for, at most: past it, all
/// are found again, as a game whose graphs come and go would otherwise
/// grow it without end.
constexpr std::size_t kMostJointMaps = 256;

} // namespace

Matrix matrixOf(const animation::Transform& transform) noexcept {
    const auto& [kX, kY, kZ, kW] = transform.rotation;
    // The rotation's columns, each then scaled by its axis's scale.
    const std::array<std::array<double, 3>, 3> kColumns = {{
        {1 - (2 * ((kY * kY) + (kZ * kZ))), 2 * ((kX * kY) + (kZ * kW)), 2 * ((kX * kZ) - (kY * kW))},
        {2 * ((kX * kY) - (kZ * kW)), 1 - (2 * ((kX * kX) + (kZ * kZ))), 2 * ((kY * kZ) + (kX * kW))},
        {2 * ((kX * kZ) + (kY * kW)), 2 * ((kY * kZ) - (kX * kW)), 1 - (2 * ((kX * kX) + (kY * kY)))},
    }};
    Matrix out{};
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            out[(column * 4) + row] = static_cast<float>(kColumns[column][row] * transform.scale[column]);
        }
        out[12 + column] = static_cast<float>(transform.translation[column]);
    }
    out[15] = 1;
    return out;
}

bool paletteInto(std::span<const Matrix> now,
                 std::span<const Matrix> before,
                 std::size_t room,
                 std::vector<Matrix>& palette,
                 SceneDraw& draw) {
    const bool kPosedBefore = before.size() == now.size();
    if (palette.size() + (now.size() * (kPosedBefore ? 2 : 1)) > room) {
        return false;
    }
    draw.palette = static_cast<std::uint32_t>(palette.size());
    draw.joints = static_cast<std::uint32_t>(now.size());
    draw.previousPalette = draw.palette;
    palette.insert(palette.end(), now.begin(), now.end());
    if (kPosedBefore) {
        draw.previousPalette = static_cast<std::uint32_t>(palette.size());
        palette.insert(palette.end(), before.begin(), before.end());
    }
    return true;
}

bool Skinning::pose(std::uint64_t mesh,
                    const mesh::Skin& skin,
                    std::span<const base::Bits128> bones,
                    const animation::Pose& pose,
                    std::vector<Matrix>& palette) {
    if (pose.bones.size() != bones.size()) {
        return false;
    }
    // Found once; checked each time, as another skeleton's bones may come
    // to lie where a list that went lay. A skin naming a bone the skeleton
    // lacks is found again each time, and drawn unposed.
    const std::pair kKey{mesh, bones.data()};
    auto found = joints_.find(kKey);
    const auto kStill = [&](const std::vector<std::uint32_t>& each) {
        if (each.size() != skin.joints.size()) {
            return false;
        }
        for (std::size_t joint = 0; joint < each.size(); ++joint) {
            if (each[joint] >= bones.size() || bones[each[joint]] != skin.joints[joint].bone) {
                return false;
            }
        }
        return true;
    };
    if (found == joints_.end() || !kStill(found->second)) {
        if (found == joints_.end() && joints_.size() == kMostJointMaps) {
            joints_.clear();
        }
        std::vector<std::uint32_t> made;
        made.reserve(skin.joints.size());
        for (const mesh::Joint& kJoint : skin.joints) {
            const auto kBone = std::ranges::find(bones, kJoint.bone);
            made.push_back(static_cast<std::uint32_t>(kBone - bones.begin()));
        }
        found = joints_.insert_or_assign(kKey, std::move(made)).first;
    }
    const std::vector<std::uint32_t>& kBones = found->second;
    if (std::ranges::any_of(kBones, [&](std::uint32_t bone) {
            return bone >= bones.size();
        })) {
        return false;
    }
    for (std::size_t joint = 0; joint < skin.joints.size(); ++joint) {
        const Matrix kPosed = matrixOf(pose.bones[kBones[joint]]);
        const std::array<float, 16>& kInverse = skin.joints[joint].inverseBind;
        Matrix made{};
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) {
                double sum = 0;
                for (std::size_t at = 0; at < 4; ++at) {
                    sum += static_cast<double>(kPosed[(at * 4) + row]) * kInverse[(column * 4) + at];
                }
                made[(column * 4) + row] = static_cast<float>(sum);
            }
        }
        palette.push_back(made);
    }
    return true;
}

} // namespace rawframe::render_scene
