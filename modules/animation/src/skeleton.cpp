#include "rawframe/animation/skeleton.h"

#include "rawframe/animation/errors.h"
#include "rawframe/base/sha256.h"
#include "rawframe/document/json.h"
#include "text.h"

#include <algorithm>
#include <array>

namespace rawframe::animation {

namespace {

using document::Value;

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::SkeletonInvalid), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::OverLimit), why);
}

std::array<double, 4> widened(const std::array<double, 3>& numbers) {
    return {numbers[0], numbers[1], numbers[2], 0.0};
}

std::array<double, 3> narrowed(const std::array<double, 4>& numbers) {
    return {numbers[0], numbers[1], numbers[2]};
}

} // namespace

std::optional<BoneIndex> Skeleton::find(base::Bits128 target) const noexcept {
    const auto kFound = std::ranges::find(bones, target, &Bone::target);
    if (kFound == bones.end()) {
        return std::nullopt;
    }
    return BoneIndex{static_cast<std::uint32_t>(kFound - bones.begin())};
}

base::Bits128 targetIdOf(std::span<const std::string_view> namePath) {
    // Each name with its length before it, so no two paths share bytes.
    base::Sha256 digest;
    digest.update(std::string_view{"rawframe.animation.target"});
    for (const std::string_view kName : namePath) {
        std::array<std::byte, 4> length{};
        for (std::size_t at = 0; at < length.size(); ++at) {
            length[at] = static_cast<std::byte>((kName.size() >> (8U * at)) & 0xFFU);
        }
        digest.update(length);
        digest.update(kName);
    }
    const base::Sha256Digest kDigest = digest.finish();
    base::Bits128 made;
    for (std::size_t at = 0; at < 8; ++at) {
        made.high = (made.high << 8U) | std::to_integer<std::uint64_t>(kDigest[at]);
        made.low = (made.low << 8U) | std::to_integer<std::uint64_t>(kDigest[at + 8]);
    }
    return made;
}

result::Status validate(const Skeleton& skeleton, const SkeletonLimits& limits) {
    if (skeleton.bones.size() > limits.maximumBones) {
        return overLimit("a skeleton has more bones than its limit");
    }
    if (skeleton.bones.empty()) {
        return invalid("a skeleton has a bone");
    }
    std::vector<base::Bits128> targets;
    for (std::size_t at = 0; at < skeleton.bones.size(); ++at) {
        const Bone& bone = skeleton.bones[at];
        if (at == 0 ? bone.parent.has_value() : !bone.parent.has_value() || bone.parent->value >= at) {
            return invalid("a skeleton's first bone is its one root, and every other names a parent before it");
        }
        if (bone.target == base::Bits128{} || bone.name.empty()) {
            return invalid("a bone has a target and a name");
        }
        if (!finite(bone.bind.translation) || !unit(bone.bind.rotation) || !finite(bone.bind.scale)) {
            return invalid("a bone's bind pose is finite, with a unit rotation");
        }
        targets.push_back(bone.target);
    }
    std::ranges::sort(targets);
    if (std::ranges::adjacent_find(targets) != targets.end()) {
        return invalid("a skeleton has each target once");
    }
    return {};
}

result::Result<std::string> writeSkeleton(const Skeleton& skeleton, const SkeletonLimits& limits) {
    RAWFRAME_TRY(validate(skeleton, limits));
    Value bones = Value::array();
    for (const Bone& bone : skeleton.bones) {
        Value made = Value::object();
        made.add("target", Value::string(hexOf(bone.target)));
        made.add("name", Value::string(bone.name));
        if (bone.parent.has_value()) {
            made.add("parent", Value::integer(bone.parent->value));
        }
        made.add("translation", arrayOf(widened(bone.bind.translation), 3));
        made.add("rotation", arrayOf(bone.bind.rotation, 4));
        made.add("scale", arrayOf(widened(bone.bind.scale), 3));
        bones.push(std::move(made));
    }
    Value made = Value::object();
    made.add("formatVersion", Value::integer(1));
    made.add("kind", Value::string("animation.skeleton"));
    made.add("bones", std::move(bones));
    return document::write(made);
}

result::Result<Skeleton> readSkeleton(std::string_view text, const SkeletonLimits& limits) {
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error()};
    }
    const Value* kind = parsed->find("kind");
    if (!hasMembers(*parsed, {"formatVersion", "kind", "bones"}) || kind->text() == nullptr ||
        *kind->text() != "animation.skeleton" || parsed->find("formatVersion")->integer() != 1 ||
        parsed->find("bones")->kind() != Value::Kind::Array) {
        return invalid("a skeleton is format version 1, kind animation.skeleton, and bones");
    }
    const Value& bones = *parsed->find("bones");
    if (bones.items().size() > limits.maximumBones) {
        return overLimit("a skeleton has more bones than its limit");
    }
    Skeleton skeleton;
    for (const Value& each : bones.items()) {
        const bool kChild = each.find("parent") != nullptr;
        if (!(kChild ? hasMembers(each, {"target", "name", "parent", "translation", "rotation", "scale"})
                     : hasMembers(each, {"target", "name", "translation", "rotation", "scale"}))) {
            return invalid("a bone is a target, a name, an optional parent, and its bind pose");
        }
        const auto kTarget = bits128Of(each.find("target"));
        const auto kTranslation = numbersOf(each.find("translation"), 3);
        const auto kRotation = numbersOf(each.find("rotation"), 4);
        const auto kScale = numbersOf(each.find("scale"), 3);
        const std::optional<std::int64_t> kParent =
            kChild ? each.find("parent")->integer() : std::optional<std::int64_t>{};
        if (!kTarget.has_value() || each.find("name")->text() == nullptr ||
            each.find("name")->kind() != Value::Kind::String || !kTranslation.has_value() || !kRotation.has_value() ||
            !kScale.has_value() || (kChild && (!kParent.has_value() || *kParent < 0 || *kParent > UINT32_MAX))) {
            return invalid("a bone's target is 32 hex digits, its name text, its parent an index, and its bind "
                           "pose numbers");
        }
        Bone bone{.target = *kTarget,
                  .name = *each.find("name")->text(),
                  .parent = std::nullopt,
                  .bind = Transform{
                      .translation = narrowed(*kTranslation), .rotation = *kRotation, .scale = narrowed(*kScale)}};
        if (kChild) {
            bone.parent = BoneIndex{static_cast<std::uint32_t>(*kParent)};
        }
        skeleton.bones.push_back(std::move(bone));
    }
    // What the writer makes of it is the text, byte for byte, or the text
    // was not in the one form.
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeSkeleton(skeleton, limits));
    if (kWritten != text) {
        return invalid("a skeleton is not in its canonical form");
    }
    return skeleton;
}

} // namespace rawframe::animation
