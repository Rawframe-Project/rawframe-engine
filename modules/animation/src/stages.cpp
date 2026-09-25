// Modifier stages (SPEC-0035, D138): two-bone IK and look-at on the pose a
// graph made, solved from square roots and the four arithmetic operations
// alone, so every machine turns the same bones the same way.

#include "rawframe/animation/instance.h"
#include "rawframe/base/assert.h"
#include "rotation.h"

#include <algorithm>
#include <cmath>

namespace rawframe::animation {

namespace {

using Vector = std::array<double, 3>;
using Rotation = std::array<double, 4>;

Vector minus(const Vector& a, const Vector& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vector plus(const Vector& a, const Vector& b) noexcept {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Vector times(const Vector& a, double by) noexcept {
    return {a[0] * by, a[1] * by, a[2] * by};
}

double dot(const Vector& a, const Vector& b) noexcept {
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

Vector cross(const Vector& a, const Vector& b) noexcept {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

double lengthOf(const Vector& a) noexcept {
    return std::sqrt(dot(a, a));
}

/// Some unit vector square to unit `a`: along the axis `a` leans on least,
/// with `a`'s part taken out.
Vector squareTo(const Vector& a) noexcept {
    std::size_t least = 0;
    for (std::size_t axis = 1; axis < 3; ++axis) {
        if (std::abs(a[axis]) < std::abs(a[least])) {
            least = axis;
        }
    }
    Vector made{};
    made[least] = 1.0;
    made = minus(made, times(a, a[least]));
    return times(made, 1.0 / lengthOf(made));
}

/// The shortest turn taking the direction of `from` to that of `to`; none
/// when either has no length.
Rotation turnBetween(const Vector& from, const Vector& to) noexcept {
    const double kFrom = lengthOf(from);
    const double kTo = lengthOf(to);
    if (!(kFrom > 0.0) || !(kTo > 0.0)) {
        return {0.0, 0.0, 0.0, 1.0};
    }
    const Vector kA = times(from, 1.0 / kFrom);
    const Vector kB = times(to, 1.0 / kTo);
    const double kDot = dot(kA, kB);
    if (kDot <= -1.0 + 1e-12) {
        // Opposite: half a turn about any square axis.
        const Vector kAxis = squareTo(kA);
        return {kAxis[0], kAxis[1], kAxis[2], 0.0};
    }
    const Vector kAxis = cross(kA, kB);
    return normalized({kAxis[0], kAxis[1], kAxis[2], 1.0 + kDot});
}

/// A bone's local rotation for a model rotation, under its parent's.
Rotation localFor(const Pose& model,
                  std::span<const std::optional<BoneIndex>> parents,
                  BoneIndex bone,
                  const Rotation& rotation) noexcept {
    const std::optional<BoneIndex>& parent = parents[bone.value];
    if (!parent.has_value()) {
        return rotation;
    }
    return normalized(multiplied(inverted(model.bones[parent->value].rotation), rotation));
}

} // namespace

std::size_t PoseEvaluator::modify(const GraphInstance& instance, Pose& local, bool simulationOnly) {
    const CompiledGraph& graph = instance.graph();
    RAWFRAME_CHECK(local.bones.size() == graph.bindPose().bones.size(), "a pose of the graph's skeleton");
    const auto kValue = [&instance](const CompiledGraph::Stage::Number& number) {
        return number.parameter.has_value() ? instance.get(*number.parameter)[0] : number.literal;
    };
    const auto kPlace = [&kValue](const std::array<CompiledGraph::Stage::Number, 3>& numbers) {
        return Vector{kValue(numbers[0]), kValue(numbers[1]), kValue(numbers[2])};
    };
    std::size_t run = 0;
    for (const CompiledGraph::Stage& stage : graph.stages()) {
        const double kWeight = std::clamp(kValue(stage.weight), 0.0, 1.0);
        if ((simulationOnly && stage.relevance != Relevance::Simulation) || !(kWeight > 0.0)) {
            continue;
        }
        // Each stage sees what the ones before it made.
        toModelSpace(graph.parents(), local, model_);
        const Vector kGoal = kPlace(stage.goal);
        if (!std::ranges::all_of(kGoal, [](double each) {
                return std::isfinite(each);
            })) {
            continue;
        }
        if (stage.lookAt) {
            const BoneIndex kBone = stage.bones[0];
            const Transform& bone = model_.bones[kBone.value];
            const Rotation kTurn = turnBetween(rotated(bone.rotation, stage.axis), minus(kGoal, bone.translation));
            const Rotation kSolved =
                localFor(model_, graph.parents(), kBone, normalized(multiplied(kTurn, bone.rotation)));
            Rotation& turned = local.bones[kBone.value].rotation;
            turned = slerp(turned, kSolved, kWeight);
            ++run;
            continue;
        }
        const auto [kTip, kMid, kRoot] = stage.bones;
        const Vector kRootAt = model_.bones[kRoot.value].translation;
        const Vector kMidAt = model_.bones[kMid.value].translation;
        const Vector kTipAt = model_.bones[kTip.value].translation;
        const double kUpper = lengthOf(minus(kMidAt, kRootAt));
        const double kLower = lengthOf(minus(kTipAt, kMidAt));
        const Vector kToGoal = minus(kGoal, kRootAt);
        const double kReach = lengthOf(kToGoal);
        if (!(kUpper > 0.0) || !(kLower > 0.0) || !(kReach > 0.0)) {
            continue;
        }
        const Vector kAlong = times(kToGoal, 1.0 / kReach);
        // As near as the chain's lengths allow.
        const double kSpan = std::clamp(kReach, std::abs(kUpper - kLower), kUpper + kLower);
        // The bend: toward the pole, or the way the chain bends now, square
        // to the line to the goal.
        const Vector kToward = stage.pole.has_value() ? minus(kPlace(*stage.pole), kRootAt) : minus(kMidAt, kRootAt);
        Vector bend = minus(kToward, times(kAlong, dot(kToward, kAlong)));
        const double kBend = lengthOf(bend);
        bend = kBend > 1e-9 * kUpper ? times(bend, 1.0 / kBend) : squareTo(kAlong);
        // The law of cosines, without the angle: where the joint goes.
        const double kCosine =
            std::clamp(((kUpper * kUpper) + (kSpan * kSpan) - (kLower * kLower)) / (2.0 * kUpper * kSpan), -1.0, 1.0);
        const double kSine = std::sqrt(std::max(0.0, 1.0 - (kCosine * kCosine)));
        const Vector kJoint = plus(kRootAt, plus(times(kAlong, kUpper * kCosine), times(bend, kUpper * kSine)));
        const Vector kEnd = plus(kRootAt, times(kAlong, kSpan));
        // The grandparent turns the joint into place, carrying the tip; the
        // parent then turns the tip onto the end.
        const Rotation kFirst = turnBetween(minus(kMidAt, kRootAt), minus(kJoint, kRootAt));
        const Vector kCarried = plus(kRootAt, rotated(kFirst, minus(kTipAt, kRootAt)));
        const Rotation kSecond = turnBetween(minus(kCarried, kJoint), minus(kEnd, kJoint));
        const Rotation kRootTurned = normalized(multiplied(kFirst, model_.bones[kRoot.value].rotation));
        const Rotation kMidTurned =
            normalized(multiplied(kSecond, multiplied(kFirst, model_.bones[kMid.value].rotation)));
        Rotation& root = local.bones[kRoot.value].rotation;
        Rotation& mid = local.bones[kMid.value].rotation;
        root = slerp(root, localFor(model_, graph.parents(), kRoot, kRootTurned), kWeight);
        // The parent's local turn is under its grandparent as solved.
        mid = slerp(mid, normalized(multiplied(inverted(kRootTurned), kMidTurned)), kWeight);
        ++run;
    }
    return run;
}

} // namespace rawframe::animation
