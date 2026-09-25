#include "rotation.h"

#include <cmath>
#include <cstddef>

namespace rawframe::animation {

namespace {

/// (-1)^n / (2n+1)!, the sine series' coefficients, made by the compiler.
constexpr std::array<double, 12> kSineTerms = [] {
    std::array<double, 12> made{};
    double factorial = 1.0;
    for (std::size_t n = 0; n < made.size(); ++n) {
        if (n > 0) {
            factorial *= static_cast<double>(2 * n) * static_cast<double>((2 * n) + 1);
        }
        made[n] = (n % 2 == 0 ? 1.0 : -1.0) / factorial;
    }
    return made;
}();

/// sin(x) for x in [0, pi/2], by its series to x^23: the next term is below
/// 1e-20 there.
double sine(double x) noexcept {
    const double kSquare = x * x;
    double sum = 0.0;
    for (std::size_t n = kSineTerms.size(); n-- > 0;) {
        sum = kSineTerms[n] + (kSquare * sum);
    }
    return x * sum;
}

/// atan(u) for u in [0, 1]: halved twice, to u <= tan(pi/16), then its
/// series to u^25, whose next term is below 1e-18 there.
double arcTangent(double u) noexcept {
    for (int halving = 0; halving < 2; ++halving) {
        u = u / (1.0 + std::sqrt(1.0 + (u * u)));
    }
    const double kSquare = u * u;
    double sum = 0.0;
    for (int n = 12; n >= 0; --n) {
        sum = ((n % 2 == 0 ? 1.0 : -1.0) / ((2.0 * n) + 1.0)) + (kSquare * sum);
    }
    return 4.0 * u * sum;
}

double lengthOf(const std::array<double, 4>& value) noexcept {
    return std::sqrt((value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]) + (value[3] * value[3]));
}

} // namespace

std::array<double, 4> normalized(const std::array<double, 4>& rotation) noexcept {
    const double kLength = lengthOf(rotation);
    if (!(kLength > 0.0) || !std::isfinite(kLength)) {
        return {0.0, 0.0, 0.0, 1.0};
    }
    return {rotation[0] / kLength, rotation[1] / kLength, rotation[2] / kLength, rotation[3] / kLength};
}

std::array<double, 4> multiplied(const std::array<double, 4>& a, const std::array<double, 4>& b) noexcept {
    return {(a[3] * b[0]) + (a[0] * b[3]) + (a[1] * b[2]) - (a[2] * b[1]),
            (a[3] * b[1]) - (a[0] * b[2]) + (a[1] * b[3]) + (a[2] * b[0]),
            (a[3] * b[2]) + (a[0] * b[1]) - (a[1] * b[0]) + (a[2] * b[3]),
            (a[3] * b[3]) - (a[0] * b[0]) - (a[1] * b[1]) - (a[2] * b[2])};
}

std::array<double, 4> inverted(const std::array<double, 4>& rotation) noexcept {
    return {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
}

std::array<double, 3> rotated(const std::array<double, 4>& rotation, const std::array<double, 3>& vector) noexcept {
    // v + 2w (q x v) + 2 q x (q x v).
    const std::array<double, 4>& q = rotation;
    const std::array<double, 3> kC{(q[1] * vector[2]) - (q[2] * vector[1]),
                                   (q[2] * vector[0]) - (q[0] * vector[2]),
                                   (q[0] * vector[1]) - (q[1] * vector[0])};
    const std::array<double, 3> kCc{
        (q[1] * kC[2]) - (q[2] * kC[1]), (q[2] * kC[0]) - (q[0] * kC[2]), (q[0] * kC[1]) - (q[1] * kC[0])};
    std::array<double, 3> made{};
    for (std::size_t each = 0; each < 3; ++each) {
        made[each] = vector[each] + (2.0 * q[3] * kC[each]) + (2.0 * kCc[each]);
    }
    return made;
}

std::array<double, 4> slerp(const std::array<double, 4>& from, const std::array<double, 4>& to, double at) noexcept {
    // The shorter arc: q and -q are one rotation.
    const double kDot = (from[0] * to[0]) + (from[1] * to[1]) + (from[2] * to[2]) + (from[3] * to[3]);
    const double kSign = kDot < 0.0 ? -1.0 : 1.0;
    std::array<double, 4> apart{};
    std::array<double, 4> together{};
    for (std::size_t each = 0; each < 4; ++each) {
        apart[each] = (kSign * to[each]) - from[each];
        together[each] = (kSign * to[each]) + from[each];
    }
    // The angle between them from half-chords, |a-b|/|a+b| = tan(angle/2),
    // which stays exact where the arc cosine of a dot product does not.
    const double kAngle = 2.0 * arcTangent(lengthOf(apart) / lengthOf(together));
    double fromWeight = 1.0 - at;
    double toWeight = at;
    if (kAngle > 1e-6) {
        const double kSine = sine(kAngle);
        fromWeight = sine((1.0 - at) * kAngle) / kSine;
        toWeight = sine(at * kAngle) / kSine;
    }
    std::array<double, 4> made{};
    for (std::size_t each = 0; each < 4; ++each) {
        made[each] = (fromWeight * from[each]) + (toWeight * kSign * to[each]);
    }
    return normalized(made);
}

} // namespace rawframe::animation
