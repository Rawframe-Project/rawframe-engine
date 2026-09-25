// The `rawframe/blend_space_1d@1` and `rawframe/blend_space_2d@1` nodes as
// the graph document holds them, and how they share their weight.

#include "blend_space.h"

#include "graph_parts.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace rawframe::animation {

namespace {

using document::Value;

using Plane = std::array<double, 2>;

Value pointsValue(std::span<const BlendSpacePoint> points, bool plane) {
    Value made = Value::object();
    for (const BlendSpacePoint& point : points) {
        made.add(point.name, plane ? arrayOf({point.at[0], point.at[1], 0.0, 0.0}, 2) : Value::real(point.at[0]));
    }
    return made;
}

/// The points `params` places, joined to their `inputs` by name; none when
/// the two do not name the same points.
std::optional<std::vector<BlendSpacePoint>> pointsOf(const Value& params, const Value& inputs, bool plane) {
    const Value* points = params.find("points");
    if (points == nullptr || points->kind() != Value::Kind::Object ||
        !std::ranges::equal(points->names(), inputs.names())) {
        return std::nullopt;
    }
    std::vector<BlendSpacePoint> made;
    for (std::size_t at = 0; at < inputs.names().size(); ++at) {
        const std::optional<Connection> kFrom = connectionOf(inputs.items()[at]);
        const Value& place = points->items()[at];
        std::optional<Plane> kAt;
        if (plane) {
            const auto kNumbers = numbersOf(&place, 2);
            if (kNumbers.has_value()) {
                kAt = Plane{(*kNumbers)[0], (*kNumbers)[1]};
            }
        } else if (const std::optional<double> kNumber = numberOf(&place)) {
            kAt = Plane{*kNumber, 0.0};
        }
        if (!kFrom.has_value() || !kAt.has_value()) {
            return std::nullopt;
        }
        made.push_back(BlendSpacePoint{.name = inputs.names()[at], .from = *kFrom, .at = *kAt});
    }
    return made;
}

/// Whether two triangles' insides are apart: some edge of either has the
/// other wholly on its far side, or on it (separating axes, exact).
bool apart(const std::array<Plane, 3>& a, const std::array<Plane, 3>& b) {
    const auto kSeparates = [](const std::array<Plane, 3>& from, const std::array<Plane, 3>& other) {
        // `from` turns left, so its inside is left of every edge.
        const double kSign = turn(from[0], from[1], from[2]) > 0.0 ? 1.0 : -1.0;
        for (std::size_t edge = 0; edge < 3; ++edge) {
            const Plane& start = from[edge];
            const Plane& end = from[(edge + 1) % 3];
            if (std::ranges::all_of(other, [&](const Plane& each) {
                    return kSign * turn(start, end, each) <= 0.0;
                })) {
                return true;
            }
        }
        return false;
    };
    return kSeparates(a, b) || kSeparates(b, a);
}

result::Status pointsInForm(std::span<const BlendSpacePoint> points, const GraphLimits& limits, std::size_t least) {
    if (points.size() > limits.maximumInputs) {
        return graphOverLimit("a blend space has more points than its limit");
    }
    if (points.size() < least) {
        return graphInvalid("a line blend space has a point, and a plane one three");
    }
    for (std::size_t at = 0; at < points.size(); ++at) {
        const BlendSpacePoint& point = points[at];
        if (!machineName(point.name) || (at > 0 && !(points[at - 1].name < point.name)) ||
            !std::isfinite(point.at[0]) || !std::isfinite(point.at[1])) {
            return graphInvalid("a blend space's points are machine names in order, at finite places");
        }
        for (std::size_t before = 0; before < at; ++before) {
            if (points[before].at == point.at) {
                return graphInvalid("a blend space's points are at places of their own");
            }
        }
    }
    return {};
}

} // namespace

double turn(const Plane& a, const Plane& b, const Plane& c) noexcept {
    return ((b[0] - a[0]) * (c[1] - a[1])) - ((b[1] - a[1]) * (c[0] - a[0]));
}

Value blendSpaceParams(const BlendSpace1DNode& node) {
    Value made = Value::object();
    made.add("points", pointsValue(node.points, false));
    if (node.position != Scalar{0.0}) {
        made.add("position", scalarValue(node.position));
    }
    return made;
}

Value blendSpaceParams(const BlendSpace2DNode& node) {
    Value made = Value::object();
    made.add("points", pointsValue(node.points, true));
    if (const auto* kLiteral = std::get_if<Plane>(&node.position)) {
        if (*kLiteral != Plane{}) {
            made.add("position", arrayOf({(*kLiteral)[0], (*kLiteral)[1], 0.0, 0.0}, 2));
        }
    } else {
        Value position = Value::object();
        position.add("parameter", hexValue(std::get<ParameterRef>(node.position).parameter));
        made.add("position", std::move(position));
    }
    Value triangles = Value::array();
    for (const std::array<std::string, 3>& triangle : node.triangles) {
        Value names = Value::array();
        for (const std::string& name : triangle) {
            names.push(Value::string(name));
        }
        triangles.push(std::move(names));
    }
    made.add("triangles", std::move(triangles));
    return made;
}

Value blendSpaceInputs(std::span<const BlendSpacePoint> points) {
    Value made = Value::object();
    for (const BlendSpacePoint& point : points) {
        made.add(point.name, connectionValue(point.from));
    }
    return made;
}

result::Result<BlendSpace1DNode> blendSpace1DOf(const Value& params, const Value& inputs) {
    const Value* position = params.find("position");
    auto points = pointsOf(params, inputs, false);
    const std::optional<Scalar> kPosition = position != nullptr ? scalarOf(*position) : std::optional{Scalar{0.0}};
    if (params.names().size() != (position != nullptr ? 2U : 1U) || !points.has_value() || !kPosition.has_value()) {
        return graphInvalid("a line blend space's params are its points, a number for each input, and optionally "
                            "its position");
    }
    return BlendSpace1DNode{.position = *kPosition, .points = std::move(*points)};
}

result::Result<BlendSpace2DNode> blendSpace2DOf(const Value& params, const Value& inputs) {
    const Value* position = params.find("position");
    const Value* triangles = params.find("triangles");
    auto points = pointsOf(params, inputs, true);
    std::optional<Point> kPosition = Point{Plane{}};
    if (position != nullptr && position->kind() == Value::Kind::Array) {
        const auto kNumbers = numbersOf(position, 2);
        kPosition = kNumbers.has_value() ? std::optional{Point{Plane{(*kNumbers)[0], (*kNumbers)[1]}}} : std::nullopt;
    } else if (position != nullptr) {
        const std::optional<std::uint64_t> kId =
            hasMembers(*position, {"parameter"}) ? bits64Of(position->find("parameter")) : std::nullopt;
        kPosition = kId.has_value() ? std::optional{Point{ParameterRef{*kId}}} : std::nullopt;
    }
    if (params.names().size() != (position != nullptr ? 3U : 2U) || !points.has_value() || !kPosition.has_value() ||
        triangles == nullptr || triangles->kind() != Value::Kind::Array) {
        return graphInvalid("a plane blend space's params are its points, a place for each input, its triangles, "
                            "and optionally its position");
    }
    BlendSpace2DNode made{.position = *kPosition, .points = std::move(*points), .triangles = {}};
    for (const Value& triangle : triangles->items()) {
        if (triangle.kind() != Value::Kind::Array || triangle.items().size() != 3 ||
            !std::ranges::all_of(triangle.items(), [](const Value& name) {
                return name.kind() == Value::Kind::String;
            })) {
            return graphInvalid("a triangle is three names");
        }
        made.triangles.push_back(
            {*triangle.items()[0].text(), *triangle.items()[1].text(), *triangle.items()[2].text()});
    }
    return made;
}

result::Status blendSpaceInForm(const Graph& graph, const BlendSpace1DNode& node, const GraphLimits& limits) {
    RAWFRAME_TRY(pointsInForm(node.points, limits, 1));
    if (!scalarInForm(graph, node.position, true)) {
        return graphInvalid("a line blend space's position is a number or a float parameter");
    }
    return {};
}

result::Status blendSpaceInForm(const Graph& graph, const BlendSpace2DNode& node, const GraphLimits& limits) {
    RAWFRAME_TRY(pointsInForm(node.points, limits, 3));
    if (const auto* kRef = std::get_if<ParameterRef>(&node.position)) {
        const Parameter* declared = parameterOf(graph, kRef->parameter);
        if (declared == nullptr || declared->type != ParameterType::Vec2) {
            return graphInvalid("a plane blend space's position is a place or a vec2 parameter");
        }
    } else if (!finite(std::get<Plane>(node.position))) {
        return graphInvalid("a plane blend space's position is a place or a vec2 parameter");
    }
    if (node.triangles.size() > limits.maximumTriangles) {
        return graphOverLimit("a blend space has more triangles than its limit");
    }
    if (node.triangles.empty()) {
        return graphInvalid("a plane blend space has a triangle");
    }
    const auto kPlace = [&node](const std::string& name) -> std::optional<std::size_t> {
        const auto kFound = std::ranges::lower_bound(node.points, name, {}, &BlendSpacePoint::name);
        if (kFound == node.points.end() || kFound->name != name) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(kFound - node.points.begin());
    };
    std::vector<std::array<Plane, 3>> corners;
    std::vector<bool> used(node.points.size(), false);
    for (std::size_t at = 0; at < node.triangles.size(); ++at) {
        const std::array<std::string, 3>& triangle = node.triangles[at];
        if (!(triangle[0] < triangle[1] && triangle[1] < triangle[2]) ||
            (at > 0 && !(node.triangles[at - 1] < triangle))) {
            return graphInvalid("a triangle names its points in name order, and the triangles are in order");
        }
        std::array<Plane, 3> made{};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const std::optional<std::size_t> kAt = kPlace(triangle[corner]);
            if (!kAt.has_value()) {
                return graphInvalid("a triangle names points of its blend space");
            }
            made[corner] = node.points[*kAt].at;
            used[*kAt] = true;
        }
        if (turn(made[0], made[1], made[2]) == 0.0) {
            return graphInvalid("a triangle has area");
        }
        for (const std::array<Plane, 3>& earlier : corners) {
            if (!apart(earlier, made)) {
                return graphInvalid("a blend space's triangles do not overlap");
            }
        }
        corners.push_back(made);
    }
    if (std::ranges::contains(used, false)) {
        return graphInvalid("every point of a plane blend space is in a triangle");
    }
    return {};
}

void lineShares(std::span<const Plane> points, double position, std::span<double> shares) {
    std::ranges::fill(shares, 0.0);
    // The nearest point at or below, and at or above.
    std::optional<std::size_t> below;
    std::optional<std::size_t> above;
    for (std::size_t at = 0; at < points.size(); ++at) {
        const double kAt = points[at][0];
        if (kAt <= position && (!below.has_value() || kAt > points[*below][0])) {
            below = at;
        }
        if (kAt >= position && (!above.has_value() || kAt < points[*above][0])) {
            above = at;
        }
    }
    if (!below.has_value() || !above.has_value() || *below == *above) {
        shares[below.value_or(above.value_or(0))] = 1.0;
        return;
    }
    const double kAlong = (position - points[*below][0]) / (points[*above][0] - points[*below][0]);
    shares[*below] = 1.0 - kAlong;
    shares[*above] = kAlong;
}

void planeShares(std::span<const Plane> points,
                 std::span<const std::array<std::size_t, 3>> triangles,
                 const Plane& position,
                 std::span<double> shares) {
    std::ranges::fill(shares, 0.0);
    for (const std::array<std::size_t, 3>& triangle : triangles) {
        const Plane& a = points[triangle[0]];
        const Plane& b = points[triangle[1]];
        const Plane& c = points[triangle[2]];
        const double kWhole = turn(a, b, c);
        const double kA = turn(position, b, c) / kWhole;
        const double kB = turn(a, position, c) / kWhole;
        const double kC = 1.0 - kA - kB;
        if (kA >= 0.0 && kB >= 0.0 && kC >= 0.0) {
            shares[triangle[0]] = kA;
            shares[triangle[1]] = kB;
            shares[triangle[2]] = kC;
            return;
        }
    }
    // Outside them all: the nearest point on any triangle's edge.
    double nearest = std::numeric_limits<double>::infinity();
    std::size_t from = 0;
    std::size_t to = 0;
    double along = 0.0;
    for (const std::array<std::size_t, 3>& triangle : triangles) {
        for (std::size_t edge = 0; edge < 3; ++edge) {
            const std::size_t kStart = triangle[edge];
            const std::size_t kEnd = triangle[(edge + 1) % 3];
            const Plane& start = points[kStart];
            const Plane kSpan{points[kEnd][0] - start[0], points[kEnd][1] - start[1]};
            const double kLength = (kSpan[0] * kSpan[0]) + (kSpan[1] * kSpan[1]);
            const double kT = std::clamp(
                (((position[0] - start[0]) * kSpan[0]) + ((position[1] - start[1]) * kSpan[1])) / kLength, 0.0, 1.0);
            const double kX = start[0] + (kT * kSpan[0]) - position[0];
            const double kY = start[1] + (kT * kSpan[1]) - position[1];
            const double kDistance = (kX * kX) + (kY * kY);
            if (kDistance < nearest) {
                nearest = kDistance;
                from = kStart;
                to = kEnd;
                along = kT;
            }
        }
    }
    shares[from] = 1.0 - along;
    shares[to] += along;
}

} // namespace rawframe::animation
