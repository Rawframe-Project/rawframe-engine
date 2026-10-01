// A view's geometry (ADR-0052, D366): the eye's axes, the perspective and
// orthographic verbs each the other's inverse, and every failure one of
// the closed set.

#include "rawframe/test/test.h"
#include "rawframe/view/view.h"

#include <cmath>
#include <limits>

using namespace rawframe;
using view::Failure;

namespace {

bool near(double a, double b, double within = 1e-3) {
    return std::abs(a - b) <= within;
}

} // namespace

RAWFRAME_TEST(TheEyesAxesFollowItsAim) {
    const view::Axes kStraight = view::axesOf(0, 0);
    RAWFRAME_EXPECT(near(kStraight.forward[2], -1) && near(kStraight.right[0], 1) && near(kStraight.up[1], 1));
    // A quarter turn looks along -X; straight up is kept short of it.
    const view::Axes kTurned = view::axesOf(1.5707964F, 0);
    RAWFRAME_EXPECT(near(kTurned.forward[0], -1) && near(kTurned.right[2], -1));
    RAWFRAME_EXPECT(view::axesOf(0, 3.0F).forward[1] < 1.0F);
}

RAWFRAME_TEST(PerspectivePickingAndProjectionAreInverse) {
    const view::Perspective kView{.eye = {10, 2, -5}, .yaw = 0.4F, .pitch = -0.3F, .fovY = 1.0F, .near = 0.1F};
    const view::ViewSize kSize{.width = 1280, .height = 720};
    // The middle looks along the eye's forward.
    const auto kMiddle = view::pointToRay(kView, kSize, 640, 360);
    const view::Axes kAxes = view::axesOf(kView.yaw, kView.pitch);
    RAWFRAME_EXPECT(kMiddle.has_value() && near(kMiddle->direction[0], kAxes.forward[0]) &&
                    near(kMiddle->direction[1], kAxes.forward[1]) && near(kMiddle->direction[2], kAxes.forward[2]));
    // A point picked, then a World point along its ray, shows where it was
    // picked, at the depth it lies.
    for (const auto& [kX, kY] : {std::pair{0.0F, 0.0F}, std::pair{1280.0F, 720.0F}, std::pair{300.0F, 500.0F}}) {
        const auto kRay = view::pointToRay(kView, kSize, kX, kY);
        RAWFRAME_EXPECT(kRay.has_value());
        if (!kRay.has_value()) {
            continue;
        }
        const std::array<double, 3> kFar = {kRay->origin[0] + (kRay->direction[0] * 20.0),
                                            kRay->origin[1] + (kRay->direction[1] * 20.0),
                                            kRay->origin[2] + (kRay->direction[2] * 20.0)};
        const auto kShown = view::worldToPoint(kView, kSize, kFar);
        RAWFRAME_EXPECT(kShown.has_value() && near(kShown->x, kX, 0.05) && near(kShown->y, kY, 0.05) &&
                        kShown->depth > 1);
    }
    // The top of the view is up.
    const auto kAbove = view::worldToPoint(kView,
                                           kSize,
                                           {kView.eye[0] + (kAxes.forward[0] * 5) + kAxes.up[0],
                                            kView.eye[1] + (kAxes.forward[1] * 5) + kAxes.up[1],
                                            kView.eye[2] + (kAxes.forward[2] * 5) + kAxes.up[2]});
    RAWFRAME_EXPECT(kAbove.has_value() && kAbove->y < 360 && near(kAbove->x, 640, 0.05) && near(kAbove->depth, 5));
}

RAWFRAME_TEST(OrthographicPickingAndProjectionAreInverse) {
    const view::Orthographic kView{.middle = {3, -2}, .height = 18};
    const view::ViewSize kSize{.width = 640, .height = 360};
    const auto kCorner = view::pointToWorld(kView, kSize, 0, 0);
    RAWFRAME_EXPECT(kCorner.has_value() && near((*kCorner)[0], 3 - 16) && near((*kCorner)[1], -2 + 9));
    const auto kBack = view::worldToPoint(kView, kSize, {5.5, 1});
    const auto kPicked =
        view::pointToWorld(kView, kSize, kBack.value_or(view::ViewPoint{}).x, kBack.value_or(view::ViewPoint{}).y);
    RAWFRAME_EXPECT(kBack.has_value() && kPicked.has_value() && near((*kPicked)[0], 5.5) && near((*kPicked)[1], 1) &&
                    kBack->depth == 0);
}

RAWFRAME_TEST(EveryFailureIsOneOfTheClosedSet) {
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    const view::Perspective kView;
    const view::ViewSize kSize{.width = 100, .height = 100};
    RAWFRAME_EXPECT(view::pointToRay(kView, {}, 1, 1).error() == Failure::NoViewSize);
    RAWFRAME_EXPECT(view::pointToRay(kView, kSize, kNaN, 1).error() == Failure::NotFinite);
    RAWFRAME_EXPECT(view::pointToRay(view::Perspective{.fovY = 0}, kSize, 1, 1).error() == Failure::SeesNothing);
    RAWFRAME_EXPECT(view::pointToRay(view::Perspective{.near = 0}, kSize, 1, 1).error() == Failure::SeesNothing);
    RAWFRAME_EXPECT(view::worldToPoint(kView, kSize, std::array<double, 3>{0, 0, 5}).error() == Failure::BehindNear);
    RAWFRAME_EXPECT(view::worldToPoint(kView, kSize, std::array<double, 3>{0, 0, -0.05}).error() ==
                    Failure::BehindNear);
    RAWFRAME_EXPECT(
        view::worldToPoint(view::Perspective{.eye = {kNaN, 0, 0}}, kSize, std::array<double, 3>{0, 0, -5}).error() ==
        Failure::NotFinite);
    RAWFRAME_EXPECT(view::pointToWorld(view::Orthographic{.height = 0}, kSize, 1, 1).error() == Failure::SeesNothing);
    RAWFRAME_EXPECT(view::pointToWorld(view::Orthographic{}, {.width = 10, .height = 0}, 1, 1).error() ==
                    Failure::NoViewSize);
}
