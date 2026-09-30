#include "rawframe/render_scene/scene.h"

#include <algorithm>
#include <cmath>

namespace rawframe::render_scene {

namespace {

using Row = std::array<float, 3>;
using Matrix3 = std::array<Row, 3>;

/// Linear Rec. 709 to LMS and back, the white balance's space (the pair
/// Unity's post-processing uses, a CAT02 cone space).
constexpr Matrix3 kToLms = {
    {{0.390405F, 0.549941F, 0.00892632F}, {0.0708416F, 0.963172F, 0.00135775F}, {0.0231082F, 0.128021F, 0.936245F}}};
constexpr Matrix3 kFromLms = {
    {{2.85847F, -1.62879F, -0.024891F}, {-0.210182F, 1.1582F, 0.000324281F}, {-0.041812F, -0.118169F, 1.06867F}}};

/// A white of chromaticity (x, y) in LMS.
Row lmsOf(float x, float y) noexcept {
    const float kX = x / y;
    const float kZ = (1 - x - y) / y;
    return {(0.7328F * kX) + 0.4296F - (0.1624F * kZ),
            (-0.7036F * kX) + 1.6975F + (0.0061F * kZ),
            (0.0030F * kX) + 0.0136F + (0.9834F * kZ)};
}

Matrix3 times(const Matrix3& left, const Matrix3& right) noexcept {
    Matrix3 out{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t k = 0; k < 3; ++k) {
                out[row][column] += left[row][k] * right[k][column];
            }
        }
    }
    return out;
}

} // namespace

SceneGrading gradingOf(const std::optional<Grading>& asked) noexcept {
    if (!asked.has_value()) {
        return {};
    }
    const Grading& kGrade = *asked;
    const std::array kValues = {kGrade.slopeR,
                                kGrade.slopeG,
                                kGrade.slopeB,
                                kGrade.offsetR,
                                kGrade.offsetG,
                                kGrade.offsetB,
                                kGrade.powerR,
                                kGrade.powerG,
                                kGrade.powerB,
                                kGrade.saturation,
                                kGrade.contrast,
                                kGrade.temperature,
                                kGrade.tint};
    if (!std::ranges::all_of(kValues,
                             [](float value) {
                                 return std::isfinite(value);
                             }) ||
        kGrade.powerR <= 0 || kGrade.powerG <= 0 || kGrade.powerB <= 0) {
        return {};
    }
    SceneGrading made{.enabled = true,
                      .slope = {kGrade.slopeR, kGrade.slopeG, kGrade.slopeB},
                      .offset = {kGrade.offsetR, kGrade.offsetG, kGrade.offsetB},
                      .power = {kGrade.powerR, kGrade.powerG, kGrade.powerB},
                      .saturation = std::max(kGrade.saturation, 0.0F),
                      .contrast = std::max(kGrade.contrast, 0.0F)};
    // The white the temperature and tint name, moved from D65 along the
    // daylight locus (warmer lowers its x less than cooler raises it), and
    // the light scaled in LMS by D65's white over it.
    const float kTemperature = std::clamp(kGrade.temperature, -1.5F, 1.5F);
    const float kTint = std::clamp(kGrade.tint, -1.5F, 1.5F);
    const float kX = 0.31271F - (kTemperature * (kTemperature < 0 ? 0.1F : 0.05F));
    const float kY = (2.87F * kX) - (3 * kX * kX) - 0.27509507F + (kTint * 0.05F);
    const Row kFrom = lmsOf(0.31271F, 0.32902F);
    const Row kTo = lmsOf(kX, kY);
    Matrix3 scale{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        scale[axis][axis] = kFrom[axis] / kTo[axis];
    }
    const Matrix3 kBalance = times(kFromLms, times(scale, kToLms));
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            made.balance[(row * 3) + column] = kBalance[row][column];
        }
    }
    return made;
}

} // namespace rawframe::render_scene
