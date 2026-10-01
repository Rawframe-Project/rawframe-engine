// Picking agrees with drawing (ADR-0052, D366): where the view's geometry
// says a World point shows is where the scene's matrices put it.

#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"
#include "rawframe/view/view.h"

#include <array>
#include <cmath>

using namespace rawframe;

RAWFRAME_TEST(PickingAgreesWithTheScenesMatrices) {
    const render_scene::SceneCamera kCamera{
        .eye = {4, 3, 9}, .yaw = -0.7F, .pitch = -0.2F, .fovY = 0.9F, .near = 0.2F, .aspect = 1280.0F / 720.0F};
    const render_scene::CameraMatrices kMatrices = render_scene::matricesOf(kCamera);
    const view::ViewSize kSize{.width = 1280, .height = 720};
    for (const std::array<double, 3>& kWorld :
         {std::array<double, 3>{0, 1, 0}, std::array<double, 3>{-3, 0.5, 2}, std::array<double, 3>{2, 4, -6}}) {
        // Clip space through the matrices, about the eye.
        const std::array<float, 4> kFrom = {static_cast<float>(kWorld[0] - kCamera.eye[0]),
                                            static_cast<float>(kWorld[1] - kCamera.eye[1]),
                                            static_cast<float>(kWorld[2] - kCamera.eye[2]),
                                            1};
        std::array<float, 4> eye{};
        std::array<float, 4> clip{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                eye[row] += kMatrices.view[(column * 4) + row] * kFrom[column];
            }
        }
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                clip[row] += kMatrices.projection[(column * 4) + row] * eye[column];
            }
        }
        const float kX = ((clip[0] / clip[3]) + 1) / 2 * kSize.width;
        const float kY = (1 - (clip[1] / clip[3])) / 2 * kSize.height;
        const auto kPoint = view::worldToPoint(render_scene::perspectiveOf(kCamera), kSize, kWorld);
        RAWFRAME_EXPECT(kPoint.has_value() && std::abs(kPoint->x - kX) < 0.05F && std::abs(kPoint->y - kY) < 0.05F &&
                        std::abs(kPoint->depth - clip[3]) < 1e-3F);
    }
}
