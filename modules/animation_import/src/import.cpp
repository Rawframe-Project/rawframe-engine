#include "rawframe/animation_import/import.h"

#include "rawframe/animation/errors.h"

#include <algorithm>
#include <array>
#include <cgltf.h>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::animation_import {

namespace {

using animation::AnimationError;
using animation::Transform;

constexpr std::array<std::string_view, 1> kSupportedExtensions = {"KHR_mesh_quantization"};

/// Names what the glTF's buffer paths resolve against: with no directory in
/// it, a path comes to `read` as the glTF wrote it, decoded.
constexpr const char* kSourceName = "source";

/// How long an animation of one instant lasts.
constexpr double kInstant = 1.0 / 30.0;

std::unexpected<result::Error> badSource(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument,
                        animation::kAnimationDomain,
                        animation::code(AnimationError::BadSource),
                        why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(result::ErrorClass::ResourceExhausted,
                        animation::kAnimationDomain,
                        animation::code(AnimationError::OverLimit),
                        why);
}

struct Buffers {
    const ReadFile* read = nullptr;
    std::string refused;
};

cgltf_result readBuffer(const cgltf_memory_options* /*memory*/,
                        const cgltf_file_options* options,
                        const char* path,
                        cgltf_size* size,
                        void** data) {
    auto* buffers = static_cast<Buffers*>(options->user_data);
    const auto kBytes = (*buffers->read)(path);
    if (!kBytes.has_value()) {
        buffers->refused = path;
        return cgltf_result_file_not_found;
    }
    if (kBytes->empty() || kBytes->size() < *size) {
        buffers->refused = path;
        return cgltf_result_data_too_short;
    }
    *size = kBytes->size();
    // cgltf only reads a buffer, and `read` keeps it alive, so it is lent
    // rather than copied and releasing it does nothing.
    *data = const_cast<std::byte*>(kBytes->data());
    return cgltf_result_success;
}

void releaseBuffer(const cgltf_memory_options* /*memory*/, const cgltf_file_options* /*options*/, void* /*data*/) {
}

struct Free {
    void operator()(cgltf_data* data) const noexcept {
        cgltf_free(data);
    }
};

using Vector = std::array<double, 3>;
using Quaternion = std::array<double, 4>;

Quaternion multiply(const Quaternion& a, const Quaternion& b) noexcept {
    return {(a[3] * b[0]) + (a[0] * b[3]) + (a[1] * b[2]) - (a[2] * b[1]),
            (a[3] * b[1]) - (a[0] * b[2]) + (a[1] * b[3]) + (a[2] * b[0]),
            (a[3] * b[2]) + (a[0] * b[1]) - (a[1] * b[0]) + (a[2] * b[3]),
            (a[3] * b[3]) - (a[0] * b[0]) - (a[1] * b[1]) - (a[2] * b[2])};
}

Vector rotate(const Quaternion& q, const Vector& v) noexcept {
    const Quaternion kTurned = multiply(multiply(q, {v[0], v[1], v[2], 0.0}), {-q[0], -q[1], -q[2], q[3]});
    return {kTurned[0], kTurned[1], kTurned[2]};
}

Quaternion normalized(Quaternion q) noexcept {
    const double kLength = std::sqrt((q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]));
    if (!(kLength > 0.0)) {
        return {0.0, 0.0, 0.0, 1.0};
    }
    for (double& each : q) {
        each /= kLength;
    }
    return q;
}

bool uniform(const Vector& scale) noexcept {
    const double kTolerance = 1e-6 * std::max({std::abs(scale[0]), std::abs(scale[1]), std::abs(scale[2])});
    return std::abs(scale[0] - scale[1]) <= kTolerance && std::abs(scale[0] - scale[2]) <= kTolerance;
}

/// A node's transform relative to its parent: its TRS, or its matrix taken
/// apart. Refused for a matrix that mirrors or shears.
result::Result<Transform> localOf(const cgltf_node& node) {
    Transform made;
    if (node.has_matrix == 0) {
        if (node.has_translation != 0) {
            made.translation = {node.translation[0], node.translation[1], node.translation[2]};
        }
        if (node.has_rotation != 0) {
            made.rotation = normalized({node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]});
        }
        if (node.has_scale != 0) {
            made.scale = {node.scale[0], node.scale[1], node.scale[2]};
        }
        return made;
    }
    const auto kM = [&node](std::size_t row, std::size_t column) {
        return static_cast<double>(node.matrix[(column * 4) + row]);
    };
    made.translation = {kM(0, 3), kM(1, 3), kM(2, 3)};
    std::array<Vector, 3> axes{};
    for (std::size_t column = 0; column < 3; ++column) {
        axes[column] = {kM(0, column), kM(1, column), kM(2, column)};
        made.scale[column] = std::sqrt((axes[column][0] * axes[column][0]) + (axes[column][1] * axes[column][1]) +
                                       (axes[column][2] * axes[column][2]));
        if (!(made.scale[column] > 0.0)) {
            return badSource("a node's matrix flattens an axis");
        }
        for (double& each : axes[column]) {
            each /= made.scale[column];
        }
    }
    const auto kDot = [](const Vector& a, const Vector& b) {
        return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
    };
    if (std::abs(kDot(axes[0], axes[1])) > 1e-4 || std::abs(kDot(axes[0], axes[2])) > 1e-4 ||
        std::abs(kDot(axes[1], axes[2])) > 1e-4) {
        return badSource("a node's matrix shears, which a transform cannot hold");
    }
    const Vector kCross{(axes[0][1] * axes[1][2]) - (axes[0][2] * axes[1][1]),
                        (axes[0][2] * axes[1][0]) - (axes[0][0] * axes[1][2]),
                        (axes[0][0] * axes[1][1]) - (axes[0][1] * axes[1][0])};
    if (kDot(kCross, axes[2]) < 0.0) {
        return badSource("a node's matrix mirrors, which a transform cannot hold");
    }
    // The rotation of an orthonormal basis (Shepperd's method).
    const double kXx = axes[0][0];
    const double kYy = axes[1][1];
    const double kZz = axes[2][2];
    const double kTrace = kXx + kYy + kZz;
    Quaternion q{};
    if (kTrace > 0.0) {
        const double kS = std::sqrt(kTrace + 1.0) * 2.0;
        q = {(axes[1][2] - axes[2][1]) / kS, (axes[2][0] - axes[0][2]) / kS, (axes[0][1] - axes[1][0]) / kS, kS / 4.0};
    } else if (kXx > kYy && kXx > kZz) {
        const double kS = std::sqrt(1.0 + kXx - kYy - kZz) * 2.0;
        q = {kS / 4.0, (axes[1][0] + axes[0][1]) / kS, (axes[2][0] + axes[0][2]) / kS, (axes[1][2] - axes[2][1]) / kS};
    } else if (kYy > kZz) {
        const double kS = std::sqrt(1.0 + kYy - kXx - kZz) * 2.0;
        q = {(axes[1][0] + axes[0][1]) / kS, kS / 4.0, (axes[2][1] + axes[1][2]) / kS, (axes[2][0] - axes[0][2]) / kS};
    } else {
        const double kS = std::sqrt(1.0 + kZz - kXx - kYy) * 2.0;
        q = {(axes[2][0] + axes[0][2]) / kS, (axes[2][1] + axes[1][2]) / kS, kS / 4.0, (axes[0][1] - axes[1][0]) / kS};
    }
    made.rotation = normalized(q);
    return made;
}

/// What places the root: every parent above it, outermost first, as one
/// transform. Its scale must be even, so it folds into the root's own.
result::Result<Transform> placementOf(const cgltf_node& root) {
    std::vector<const cgltf_node*> above;
    for (const cgltf_node* parent = root.parent; parent != nullptr; parent = parent->parent) {
        above.push_back(parent);
    }
    Transform placed;
    for (auto node = above.rbegin(); node != above.rend(); ++node) {
        RAWFRAME_TRY_ASSIGN(const Transform kLocal, localOf(**node));
        if (!uniform(kLocal.scale)) {
            return badSource("what places the skeleton's root scales unevenly");
        }
        const Vector kScaled{placed.scale[0] * kLocal.translation[0],
                             placed.scale[0] * kLocal.translation[1],
                             placed.scale[0] * kLocal.translation[2]};
        const Vector kMoved = rotate(placed.rotation, kScaled);
        placed.translation = {
            placed.translation[0] + kMoved[0], placed.translation[1] + kMoved[1], placed.translation[2] + kMoved[2]};
        placed.rotation = normalized(multiply(placed.rotation, kLocal.rotation));
        placed.scale = {
            placed.scale[0] * kLocal.scale[0], placed.scale[1] * kLocal.scale[1], placed.scale[2] * kLocal.scale[2]};
    }
    return placed;
}

/// Moves a root value of `channel` from the frame above the root into the
/// skeleton's: a translation placed, a rotation turned, a scale scaled.
/// Tangents move as values do, without the placement's offset.
std::array<double, 4>
folded(const Transform& placement, animation::Channel channel, const std::array<double, 4>& value, bool offset) {
    switch (channel) {
    case animation::Channel::Translation: {
        const Vector kMoved =
            rotate(placement.rotation,
                   {placement.scale[0] * value[0], placement.scale[0] * value[1], placement.scale[0] * value[2]});
        const double kShift = offset ? 1.0 : 0.0;
        return {kMoved[0] + (kShift * placement.translation[0]),
                kMoved[1] + (kShift * placement.translation[1]),
                kMoved[2] + (kShift * placement.translation[2]),
                0.0};
    }
    case animation::Channel::Rotation:
        return multiply(placement.rotation, value);
    case animation::Channel::Scale:
        return {placement.scale[0] * value[0], placement.scale[0] * value[1], placement.scale[0] * value[2], 0.0};
    }
    return value;
}

result::Status requireSupported(const cgltf_data& data) {
    for (cgltf_size i = 0; i < data.extensions_required_count; ++i) {
        const std::string_view kName = data.extensions_required[i];
        if (!std::ranges::contains(kSupportedExtensions, kName)) {
            return result::fail(result::ErrorClass::Unsupported,
                                animation::kAnimationDomain,
                                animation::code(AnimationError::UnsupportedExtension),
                                "the glTF requires the extension " + std::string{kName} + ", which is not supported");
        }
    }
    return {};
}

/// The skin's joints as bones, parents first, and the node of each.
struct Rig {
    animation::Skeleton skeleton;
    std::map<const cgltf_node*, std::size_t> bones;
    Transform placement;
};

result::Result<Rig> rigOf(const cgltf_skin& skin) {
    std::map<const cgltf_node*, std::size_t> joints;
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        joints.emplace(skin.joints[i], i);
    }
    const cgltf_node* root = nullptr;
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        const cgltf_node* kJoint = skin.joints[i];
        if (kJoint->parent == nullptr || !joints.contains(kJoint->parent)) {
            if (root != nullptr) {
                return badSource("a skin with more than one root joint; a skeleton has one root");
            }
            root = kJoint;
        }
    }
    if (root == nullptr) {
        return badSource("a skin with no root joint");
    }
    Rig rig;
    RAWFRAME_TRY_ASSIGN(rig.placement, placementOf(*root));
    // Depth first from the root, children in order, each bone's name path
    // beside it.
    std::vector<std::pair<const cgltf_node*, std::optional<std::size_t>>> pending{{root, std::nullopt}};
    std::vector<std::vector<std::string>> paths;
    while (!pending.empty()) {
        const auto [kNode, kParent] = pending.back();
        pending.pop_back();
        const std::size_t kBone = rig.skeleton.bones.size();
        std::string name = kNode->name != nullptr && kNode->name[0] != '\0'
                               ? std::string{kNode->name}
                               : "joint" + std::to_string(joints.at(kNode));
        std::vector<std::string> path = kParent.has_value() ? paths[*kParent] : std::vector<std::string>{};
        path.push_back(name);
        std::vector<std::string_view> views(path.begin(), path.end());
        RAWFRAME_TRY_ASSIGN(Transform bind, localOf(*kNode));
        if (!kParent.has_value()) {
            const auto kT = folded(rig.placement,
                                   animation::Channel::Translation,
                                   {bind.translation[0], bind.translation[1], bind.translation[2], 0.0},
                                   true);
            const auto kS = folded(
                rig.placement, animation::Channel::Scale, {bind.scale[0], bind.scale[1], bind.scale[2], 0.0}, true);
            bind.translation = {kT[0], kT[1], kT[2]};
            bind.rotation = normalized(multiply(rig.placement.rotation, bind.rotation));
            bind.scale = {kS[0], kS[1], kS[2]};
        }
        rig.skeleton.bones.push_back(animation::Bone{
            .target = animation::targetIdOf(views),
            .name = std::move(name),
            .parent = kParent.has_value() ? std::optional{animation::BoneIndex{static_cast<std::uint32_t>(*kParent)}}
                                          : std::nullopt,
            .bind = bind});
        paths.push_back(std::move(path));
        rig.bones.emplace(kNode, kBone);
        for (cgltf_size i = kNode->children_count; i > 0; --i) {
            if (joints.contains(kNode->children[i - 1])) {
                pending.emplace_back(kNode->children[i - 1], kBone);
            }
        }
        if (rig.skeleton.bones.size() > skin.joints_count) {
            return badSource("a skin whose joints are reached twice");
        }
    }
    return rig;
}

/// The floats of element `index` of an accessor, as many as it has.
std::array<double, 4> element(const cgltf_accessor& accessor, cgltf_size index, std::size_t width) {
    std::array<cgltf_float, 4> read{};
    cgltf_accessor_read_float(&accessor, index, read.data(), width);
    std::array<double, 4> made{};
    for (std::size_t each = 0; each < width; ++each) {
        made[each] = read[each];
    }
    return made;
}

/// A track's keys as differences from its basis: the bone's bind, or the
/// track's first key. An offset, the turn after the basis's, a factor.
result::Status difference(animation::Track& track, const animation::Transform& bind, animation::AdditiveBasis basis) {
    std::array<double, 4> from{};
    switch (track.channel) {
    case animation::Channel::Translation:
        from = {bind.translation[0], bind.translation[1], bind.translation[2], 0.0};
        break;
    case animation::Channel::Rotation:
        from = bind.rotation;
        break;
    case animation::Channel::Scale:
        from = {bind.scale[0], bind.scale[1], bind.scale[2], 0.0};
        break;
    }
    if (basis == animation::AdditiveBasis::FirstFrame) {
        from = track.keys.front().value;
    }
    const Quaternion kBack{-from[0], -from[1], -from[2], from[3]};
    for (animation::Key& key : track.keys) {
        switch (track.channel) {
        case animation::Channel::Translation:
            for (std::size_t each = 0; each < 3; ++each) {
                key.value[each] -= from[each];
            }
            break;
        case animation::Channel::Rotation:
            key.value = normalized(multiply(kBack, key.value));
            break;
        case animation::Channel::Scale:
            for (std::size_t each = 0; each < 3; ++each) {
                if (from[each] == 0.0) {
                    return badSource("an additive scale from a basis of nought");
                }
                key.value[each] /= from[each];
                key.in[each] /= from[each];
                key.out[each] /= from[each];
            }
            break;
        }
    }
    return {};
}

struct Counts {
    std::size_t skipped = 0;
    std::size_t flattened = 0;
};

result::Result<animation::Clip>
clipOf(const cgltf_animation& source, const Rig& rig, const ImportSettings& settings, Counts& counts) {
    animation::Clip clip{.skeleton = settings.skeleton,
                         .duration = 0.0,
                         .loop = settings.loop ? animation::Loop::Loop : animation::Loop::Clamp,
                         .additive = settings.additive,
                         .tracks = {},
                         .events = {},
                         .syncMarkers = {}};
    for (cgltf_size i = 0; i < source.samplers_count; ++i) {
        const cgltf_accessor& kInput = *source.samplers[i].input;
        if (kInput.count != 0) {
            clip.duration = std::max(clip.duration, element(kInput, kInput.count - 1, 1)[0]);
        }
    }
    const bool kInstantOnly = !(clip.duration > 0.0);
    if (kInstantOnly) {
        clip.duration = kInstant;
    }
    for (cgltf_size i = 0; i < source.channels_count; ++i) {
        const cgltf_animation_channel& kChannel = source.channels[i];
        const auto kBone = kChannel.target_node != nullptr ? rig.bones.find(kChannel.target_node) : rig.bones.end();
        animation::Channel channel = animation::Channel::Translation;
        std::size_t width = 3;
        switch (kChannel.target_path) {
        case cgltf_animation_path_type_translation:
            break;
        case cgltf_animation_path_type_rotation:
            channel = animation::Channel::Rotation;
            width = 4;
            break;
        case cgltf_animation_path_type_scale:
            channel = animation::Channel::Scale;
            break;
        default:
            ++counts.skipped;
            continue;
        }
        if (kBone == rig.bones.end()) {
            ++counts.skipped;
            continue;
        }
        const cgltf_animation_sampler& kSampler = *kChannel.sampler;
        const bool kCubic = kSampler.interpolation == cgltf_interpolation_type_cubic_spline;
        const std::size_t kKeys = kSampler.input->count;
        if (kSampler.output->count != (kCubic ? 3 : 1) * kKeys || kKeys == 0) {
            return badSource("an animation sampler whose keys and values do not match");
        }
        if (kKeys > settings.clipLimits.maximumKeys) {
            return overLimit("an animation channel with more keys than a track may have");
        }
        const bool kRoot = !rig.skeleton.bones[kBone->second].parent.has_value();
        const bool kFlatten = kCubic && channel == animation::Channel::Rotation;
        counts.flattened += kFlatten ? 1 : 0;
        animation::Track track{.bone = rig.skeleton.bones[kBone->second].target, .channel = channel, .keys = {}};
        for (std::size_t key = 0; key < kKeys; ++key) {
            const std::size_t kValueAt = kCubic ? (key * 3) + 1 : key;
            animation::Key made{.time = element(*kSampler.input, key, 1)[0],
                                .value = element(*kSampler.output, kValueAt, width),
                                .interpolation = kSampler.interpolation == cgltf_interpolation_type_step
                                                     ? animation::Interpolation::Step
                                                     : animation::Interpolation::Linear,
                                .in = {},
                                .out = {}};
            if (kCubic && !kFlatten) {
                made.interpolation = animation::Interpolation::Cubic;
                made.in = element(*kSampler.output, key * 3, width);
                made.out = element(*kSampler.output, (key * 3) + 2, width);
            }
            if (kRoot) {
                made.value = folded(rig.placement, channel, made.value, true);
                if (made.interpolation == animation::Interpolation::Cubic) {
                    made.in = folded(rig.placement, channel, made.in, false);
                    made.out = folded(rig.placement, channel, made.out, false);
                }
            }
            if (channel == animation::Channel::Rotation) {
                made.value = normalized(made.value);
            }
            track.keys.push_back(made);
        }
        if (kInstantOnly) {
            track.keys.resize(1);
            track.keys.front().time = 0.0;
        } else if (settings.loop) {
            // A glTF loop repeats its first key at its end, where a clip
            // that loops comes back round by itself. The root's end may be
            // somewhere else, the ground a walk cycle covers: that becomes
            // its drift (D134).
            const animation::Key kEnd = track.keys.back();
            std::erase_if(track.keys, [&clip](const animation::Key& key) {
                return key.time >= clip.duration;
            });
            if (track.keys.empty()) {
                return badSource("a looped animation channel with keys only at its end");
            }
            const animation::Key& first = track.keys.front();
            if (kRoot && !settings.additive.has_value() && kEnd.time >= clip.duration && kEnd.value != first.value) {
                if (channel == animation::Channel::Translation) {
                    track.drift = std::array<double, 4>{kEnd.value[0] - first.value[0],
                                                        kEnd.value[1] - first.value[1],
                                                        kEnd.value[2] - first.value[2],
                                                        0.0};
                } else if (channel == animation::Channel::Rotation) {
                    // The turn that takes the first key to the end.
                    track.drift = normalized(
                        multiply(kEnd.value, {-first.value[0], -first.value[1], -first.value[2], first.value[3]}));
                }
            }
        }
        if (settings.additive.has_value()) {
            RAWFRAME_TRY(difference(track, rig.skeleton.bones[kBone->second].bind, *settings.additive));
        }
        clip.tracks.push_back(std::move(track));
    }
    RAWFRAME_TRY(animation::validate(clip, settings.clipLimits));
    return clip;
}

} // namespace

result::Result<ImportedAnimation>
importGltf(std::span<const std::byte> source, const ReadFile& read, const ImportSettings& settings) {
    if (source.size() > settings.maximumBytes) {
        return overLimit("a glTF larger than its limits allow");
    }
    Buffers buffers{.read = &read, .refused = {}};
    cgltf_options options{};
    options.file.read = &readBuffer;
    options.file.release = &releaseBuffer;
    options.file.user_data = &buffers;
    cgltf_data* parsed = nullptr;
    if (cgltf_parse(&options, source.data(), source.size(), &parsed) != cgltf_result_success) {
        return badSource("not glTF 2.0 this importer reads");
    }
    const std::unique_ptr<cgltf_data, Free> kData{parsed};
    RAWFRAME_TRY(requireSupported(*kData));
    for (cgltf_size i = 0; i < kData->buffers_count; ++i) {
        if (kData->buffers[i].size > settings.maximumBytes) {
            return overLimit("a glTF buffer larger than its limits allow");
        }
    }
    if (cgltf_load_buffers(&options, kData.get(), kSourceName) != cgltf_result_success) {
        if (!buffers.refused.empty()) {
            return badSource("the glTF's buffer " + buffers.refused + " cannot be read whole");
        }
        return badSource("a glTF buffer that cannot be read");
    }
    if (cgltf_validate(kData.get()) != cgltf_result_success) {
        return badSource("a glTF whose accessors, views, or nodes are not consistent");
    }
    if (kData->skins_count == 0) {
        return badSource("a glTF with no skin has no skeleton to import");
    }
    RAWFRAME_TRY_ASSIGN(Rig rig, rigOf(kData->skins[0]));
    RAWFRAME_TRY(animation::validate(rig.skeleton, settings.skeletonLimits));
    ImportedAnimation made{.skeleton = rig.skeleton, .clips = {}, .channelsSkipped = 0, .rotationsFlattened = 0};
    Counts counts;
    for (cgltf_size i = 0; i < kData->animations_count; ++i) {
        const cgltf_animation& kAnimation = kData->animations[i];
        auto clip = clipOf(kAnimation, rig, settings, counts);
        const std::string kName = kAnimation.name != nullptr && kAnimation.name[0] != '\0'
                                      ? std::string{kAnimation.name}
                                      : "animation" + std::to_string(i);
        if (!clip.has_value()) {
            return std::unexpected<result::Error>{std::move(clip).error().withContext("animation", kName)};
        }
        made.clips.push_back(ImportedClip{.name = kName, .clip = std::move(*clip)});
    }
    made.channelsSkipped = counts.skipped;
    made.rotationsFlattened = counts.flattened;
    return made;
}

} // namespace rawframe::animation_import
