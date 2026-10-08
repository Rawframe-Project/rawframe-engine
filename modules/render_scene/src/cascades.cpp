#include "cascades.h"

#include <algorithm>
#include <cmath>

namespace rawframe::render_scene {

void fitCascades(const ShadowSettings& settings,
                 const Vector& toSun,
                 const SceneCamera& camera,
                 const std::array<Vector, 3>& axes,
                 float half,
                 float aspect,
                 float near,
                 SceneShadows& shadows) {
    shadows.count = std::min<std::size_t>(settings.cascades, shadows.cascades.size());
    shadows.side = settings.side;
    shadows.distance = settings.distance;
    // The sun's axes: across its square, then along its light.
    const Vector kAlong = {-toSun[0], -toSun[1], -toSun[2]};
    const Vector kAcross = normalized(cross(kAlong, std::abs(kAlong[1]) < 0.99F ? Vector{0, 1, 0} : Vector{1, 0, 0}));
    const Vector kUpward = cross(kAcross, kAlong);
    const auto kDot = [](const Vector& left, const std::array<double, 3>& right) {
        return (left[0] * right[0]) + (left[1] * right[1]) + (left[2] * right[2]);
    };
    const auto kDotF = [](const Vector& left, const Vector& right) {
        return (left[0] * right[0]) + (left[1] * right[1]) + (left[2] * right[2]);
    };
    const Vector& kForward = axes[2];
    const float kTan = std::tan(half);
    float start = near;
    for (std::size_t at = 0; at < shadows.count; ++at) {
        const float kPart = static_cast<float>(at + 1) / static_cast<float>(shadows.count);
        const float kUniform = near + ((settings.distance - near) * kPart);
        const float kLogarithmic = near * std::pow(settings.distance / near, kPart);
        const float kEnd = (settings.logarithmicBlend * kLogarithmic) + ((1 - settings.logarithmicBlend) * kUniform);
        // The slice's bounding sphere: its center on the view's axis,
        // where it is nearest all eight corners.
        const float kNearHalf = start * kTan;
        const float kFarHalf = kEnd * kTan;
        const float kNearCorner = kNearHalf * kNearHalf * (1 + (aspect * aspect));
        const float kFarCorner = kFarHalf * kFarHalf * (1 + (aspect * aspect));
        const float kMiddle = std::clamp(
            ((kEnd * kEnd) - (start * start) + kFarCorner - kNearCorner) / (2 * (kEnd - start)), start, kEnd);
        const float kRadiusRaw = std::sqrt(std::max(((kEnd - kMiddle) * (kEnd - kMiddle)) + kFarCorner,
                                                    ((kMiddle - start) * (kMiddle - start)) + kNearCorner));
        // Rounded up to a sixteenth of a meter, so it is the same frame
        // to frame.
        const float kRadius = std::ceil(kRadiusRaw * 16) / 16;
        const float kTexel = 2 * kRadius / static_cast<float>(settings.side);
        const Vector kCenter = {kForward[0] * kMiddle, kForward[1] * kMiddle, kForward[2] * kMiddle};
        // Snapped on the World's texels: the eye's place in the sun's
        // axes in doubles, the center's offset from it in floats.
        const double kEyeAcross = kDot(kAcross, camera.eye);
        const double kEyeUpward = kDot(kUpward, camera.eye);
        const double kWorldAcross = std::floor((kEyeAcross + kDotF(kAcross, kCenter)) / kTexel) * kTexel;
        const double kWorldUpward = std::floor((kEyeUpward + kDotF(kUpward, kCenter)) / kTexel) * kTexel;
        const auto kCenterAcross = static_cast<float>(kWorldAcross - kEyeAcross);
        const auto kCenterUpward = static_cast<float>(kWorldUpward - kEyeUpward);
        const float kCenterAlong = kDotF(kAlong, kCenter);
        // Depth one toward the sun, at the shadows' distance before the
        // sphere; nought behind it.
        const float kNearest = kCenterAlong - kRadius - settings.distance;
        const float kFarthest = kCenterAlong + kRadius;
        const float kDepth = kFarthest - kNearest;
        Matrix& matrix = shadows.cascades[at].viewProjection;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            matrix[(axis * 4) + 0] = kAcross[axis] / kRadius;
            matrix[(axis * 4) + 1] = kUpward[axis] / kRadius;
            matrix[(axis * 4) + 2] = -kAlong[axis] / kDepth;
            matrix[(axis * 4) + 3] = 0;
        }
        matrix[12] = -kCenterAcross / kRadius;
        matrix[13] = -kCenterUpward / kRadius;
        matrix[14] = kFarthest / kDepth;
        matrix[15] = 1;
        shadows.cascades[at].far = kEnd;
        shadows.cascades[at].texel = kTexel;
        start = kEnd;
    }
}

} // namespace rawframe::render_scene
