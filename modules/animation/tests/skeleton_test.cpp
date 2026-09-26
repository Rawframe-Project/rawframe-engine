// Skeletons: one canonical text, target identities derived once from name
// paths, and every rule of the hierarchy held against hostile documents.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

base::Bits128 target(std::string_view name) {
    const std::array<std::string_view, 1> kPath{name};
    return targetIdOf(kPath);
}

/// A root and two children, one of them turned a quarter about y.
Skeleton rig() {
    const double kHalf = std::sqrt(0.5);
    return Skeleton{.bones = {Bone{.target = target("hips"),
                                   .name = "hips",
                                   .parent = std::nullopt,
                                   .bind = Transform{.translation = {0.0, 1.0, 0.0}}},
                              Bone{.target = target("spine"),
                                   .name = "spine",
                                   .parent = BoneIndex{0},
                                   .bind = Transform{.translation = {0.0, 0.25, 0.0}}},
                              Bone{.target = target("leg"),
                                   .name = "leg",
                                   .parent = BoneIndex{0},
                                   .bind = Transform{.translation = {0.125, -0.5, 0.0},
                                                     .rotation = {0.0, kHalf, 0.0, kHalf},
                                                     .scale = {1.0, 2.0, 1.0}}}}};
}

} // namespace

RAWFRAME_TEST(ASkeletonHasOneText) {
    const auto kText = writeSkeleton(rig());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    // Changing the form moves this, and needs a new format version.
    RAWFRAME_EXPECT(kText->starts_with("{\n  \"formatVersion\": 1,\n  \"kind\": \"animation.skeleton\",\n  \"bones\": "
                                       "[\n    {\n      \"target\": \""));
    RAWFRAME_EXPECT(kText->contains("\"name\": \"spine\",\n      \"parent\": 0,\n      \"translation\": [\n        "
                                    "0,\n        0.25,\n        0\n      ],"));
    RAWFRAME_EXPECT(kText->contains("0.7071067811865476"));
    const auto kRead = readSkeleton(*kText);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == rig());
    RAWFRAME_EXPECT(kRead.has_value() && kRead->find(target("leg")) == BoneIndex{2});
    RAWFRAME_EXPECT(kRead.has_value() && !kRead->find(target("arm")).has_value());
}

RAWFRAME_TEST(ASkeletonMayDeclareItsRootMotion) {
    Skeleton walker = rig();
    walker.rootMotion = RootMotionSource{.translation = {true, false, true}, .rotation = Axis::Y};
    const auto kText = writeSkeleton(walker);
    RAWFRAME_EXPECT(
        kText.has_value() &&
        kText->ends_with("  \"rootMotion\": {\n    \"translation\": \"xz\",\n    \"rotation\": \"y\"\n  }\n}\n"));
    RAWFRAME_EXPECT(kText.has_value() && readSkeleton(*kText) == walker);
    // A turn alone, or a translation alone.
    Skeleton turner = rig();
    turner.rootMotion = RootMotionSource{.rotation = Axis::Z};
    const auto kTurner = writeSkeleton(turner);
    RAWFRAME_EXPECT(kTurner.has_value() && kTurner->contains("\"translation\": \"\"") &&
                    readSkeleton(*kTurner) == turner);
    // Taking nothing is no source; an axis out of order, repeated, or
    // unknown, or two turns, is no text of one.
    Skeleton idle = rig();
    idle.rootMotion = RootMotionSource{};
    RAWFRAME_EXPECT(refusedWith(writeSkeleton(idle), AnimationError::SkeletonInvalid));
    if (!kText.has_value()) {
        return;
    }
    for (const std::string_view kWrong : {"\"zx\"", "\"xx\"", "\"w\"", "7"}) {
        std::string text = *kText;
        text.replace(text.find("\"xz\""), 4, kWrong);
        RAWFRAME_EXPECT(refusedWith(readSkeleton(text), AnimationError::SkeletonInvalid));
    }
    std::string twoTurns = *kText;
    twoTurns.replace(twoTurns.find("\"y\""), 3, "\"yz\"");
    RAWFRAME_EXPECT(refusedWith(readSkeleton(twoTurns), AnimationError::SkeletonInvalid));
}

RAWFRAME_TEST(ATargetIsItsWholeNamePath) {
    const std::array<std::string_view, 2> kSplitLate{"ab", "c"};
    const std::array<std::string_view, 2> kSplitEarly{"a", "bc"};
    const std::array<std::string_view, 1> kJoined{"abc"};
    RAWFRAME_EXPECT(targetIdOf(kSplitLate) == targetIdOf(kSplitLate));
    RAWFRAME_EXPECT(targetIdOf(kSplitLate) != targetIdOf(kSplitEarly));
    RAWFRAME_EXPECT(targetIdOf(kSplitLate) != targetIdOf(kJoined));
    // Fixed forever: a skeleton imported again keeps its targets.
    RAWFRAME_EXPECT(target("hips") == base::parseBits128Hex("1e895f353806509c756004284b9483ea").value);
}

RAWFRAME_TEST(ASkeletonOutOfItsRulesIsRefused) {
    Skeleton twoRoots = rig();
    twoRoots.bones[2].parent.reset();
    Skeleton childFirst = rig();
    childFirst.bones[1].parent = BoneIndex{2};
    Skeleton ownParent = rig();
    ownParent.bones[1].parent = BoneIndex{1};
    Skeleton rootWithParent = rig();
    rootWithParent.bones[0].parent = BoneIndex{0};
    Skeleton twice = rig();
    twice.bones[2].target = twice.bones[1].target;
    Skeleton noTarget = rig();
    noTarget.bones[1].target = {};
    Skeleton unnamed = rig();
    unnamed.bones[1].name.clear();
    Skeleton notUnit = rig();
    notUnit.bones[2].bind.rotation = {0.0, 1.0, 0.0, 1.0};
    Skeleton notFinite = rig();
    notFinite.bones[0].bind.translation[1] = std::numeric_limits<double>::infinity();
    Skeleton nothing;
    for (const Skeleton& kSkeleton :
         {twoRoots, childFirst, ownParent, rootWithParent, twice, noTarget, unnamed, notUnit, notFinite, nothing}) {
        RAWFRAME_EXPECT(refusedWith(writeSkeleton(kSkeleton), AnimationError::SkeletonInvalid));
    }
    RAWFRAME_EXPECT(refusedWith(writeSkeleton(rig(), {.maximumBones = 2}), AnimationError::OverLimit));
}

RAWFRAME_TEST(OnlyTheCanonicalTextReads) {
    const auto kText = writeSkeleton(rig());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const auto kReplaced = [&kText](std::string_view from, std::string_view to) {
        std::string made = *kText;
        made.replace(made.find(from), from.size(), to);
        return made;
    };
    for (const std::string& kWrong : {kReplaced("0.25", "0.250"),
                                      kReplaced("\"formatVersion\": 1", "\"formatVersion\": 2"),
                                      kReplaced("animation.skeleton", "animation.clip"),
                                      kReplaced("\"parent\": 0", "\"parent\": -1"),
                                      kReplaced("\"parent\": 0", "\"parent\": 0.5"),
                                      kReplaced("\"name\": \"hips\"", "\"name\": 7"),
                                      kReplaced("\"name\": \"hips\",", "\"name\": \"hips\",\n      \"mass\": 1,"),
                                      *kText + " ",
                                      kReplaced("{\n  \"formatVersion\"", "{\n   \"formatVersion\""),
                                      std::string{"[]"}}) {
        RAWFRAME_EXPECT(!readSkeleton(kWrong).has_value());
    }
    // Cut short anywhere, it is refused.
    for (std::size_t length = 0; length < kText->size(); ++length) {
        RAWFRAME_EXPECT(!readSkeleton(std::string_view{*kText}.substr(0, length)).has_value());
    }
    RAWFRAME_EXPECT(refusedWith(readSkeleton(*kText, {.maximumBones = 2}), AnimationError::OverLimit));
}

RAWFRAME_TEST(HostileSkeletonsReadOnlyAsTheyWrite) {
    const auto kText = writeSkeleton(rig());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const test::WrittenRun kRun = test::readOnlyAsWritten(
        *kText,
        "\"{}[],:.-+eE0123456789 \n",
        [](std::string_view text) {
            return readSkeleton(text);
        },
        [](const Skeleton& read) {
            return writeSkeleton(read);
        });
    RAWFRAME_EXPECT(kRun.read > 0 && kRun.differing == 0);
}
