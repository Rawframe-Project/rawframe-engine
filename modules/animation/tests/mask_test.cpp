// Masks: one text for each mask, anything else refused; and each bone's
// weight, from the nearest chain at or above it that reaches it.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/mask.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr base::Bits128 kSpine{1, 2};
constexpr base::Bits128 kArm{1, 3};
constexpr base::Bits128 kHand{1, 4};
constexpr base::Bits128 kLeg{1, 5};

/// A root with a spine and a leg; the spine carries an arm, the arm a hand.
Skeleton body() {
    return Skeleton{.bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}},
                              Bone{.target = kSpine, .name = "spine", .parent = BoneIndex{0}, .bind = {}},
                              Bone{.target = kArm, .name = "arm", .parent = BoneIndex{1}, .bind = {}},
                              Bone{.target = kHand, .name = "hand", .parent = BoneIndex{2}, .bind = {}},
                              Bone{.target = kLeg, .name = "leg", .parent = BoneIndex{0}, .bind = {}}}};
}

/// The upper body, the hand only half.
Mask upper() {
    return Mask{.skeleton = kSkeletonId,
                .chains = {MaskChain{.root = kSpine, .descendants = true, .weight = 1.0},
                           MaskChain{.root = kHand, .descendants = false, .weight = 0.5}}};
}

} // namespace

RAWFRAME_TEST(AMaskHasOneText) {
    const auto kText = writeMask(upper());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const auto kRead = readMask(*kText);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == upper());
    RAWFRAME_EXPECT(refusedWith(readMask(*kText + " "), AnimationError::MaskInvalid));
    Mask twice = upper();
    twice.chains.push_back(twice.chains.back());
    RAWFRAME_EXPECT(refusedWith(writeMask(twice), AnimationError::MaskInvalid));
    Mask heavy = upper();
    heavy.chains[0].weight = 1.5;
    RAWFRAME_EXPECT(refusedWith(writeMask(heavy), AnimationError::MaskInvalid));
    RAWFRAME_EXPECT(refusedWith(writeMask(Mask{.skeleton = kSkeletonId, .chains = {}}), AnimationError::MaskInvalid));
    RAWFRAME_EXPECT(refusedWith(writeMask(upper(), {.maximumChains = 1}), AnimationError::OverLimit));
}

RAWFRAME_TEST(BonesWeighAsTheNearestChainThatReachesThem) {
    const auto kWeights = boneWeights(upper(), body(), kSkeletonId);
    RAWFRAME_EXPECT(kWeights.has_value() && *kWeights == (std::vector<double>{0.0, 1.0, 1.0, 0.5, 0.0}));
    // Of another skeleton, or rooted at a bone this one lacks.
    RAWFRAME_EXPECT(refusedWith(boneWeights(upper(), body(), {7, 8}), AnimationError::BindingInvalid));
    Mask stray = upper();
    stray.chains[0].root = {9, 9};
    RAWFRAME_EXPECT(refusedWith(boneWeights(stray, body(), kSkeletonId), AnimationError::BindingInvalid));
}

RAWFRAME_TEST(ASubsetHoldsWhatItWeighsAndEveryBoneAbove) {
    // The hand alone: the bones that place it come with it; the leg does
    // not.
    const Mask kHandOnly{.skeleton = kSkeletonId, .chains = {MaskChain{.root = kHand, .descendants = false}}};
    const auto kSubset = boneSubset(kHandOnly, body(), kSkeletonId);
    RAWFRAME_EXPECT(kSubset.has_value() && *kSubset == (std::vector<std::uint8_t>{1, 1, 1, 1, 0}));
}
