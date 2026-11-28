#include "rawframe/world_kest/debug_doors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rawframe::world_kest {

void PresentedDrawings::advance() {
    ++tick;
    const auto kGone = std::ranges::remove_if(shapes, [this](const DebugShape& shape) {
        return shape.until < tick;
    });
    if (kGone.begin() != shapes.end()) {
        shapes.erase(kGone.begin(), kGone.end());
        ++revision;
    }
}

bool PresentedDrawings::add(const DebugShape& shape) {
    const auto kFinite = [](const std::array<double, 3>& point) {
        return std::ranges::all_of(point, [](double each) {
            return std::isfinite(each);
        });
    };
    if (shapes.size() >= kMostDebugShapes || shape.channel >= kDebugChannels || !kFinite(shape.a) ||
        !kFinite(shape.b) || (shape.kind == DebugShape::Kind::Sphere && shape.b[0] < 0)) {
        ++leftOut;
        return false;
    }
    shapes.push_back(shape);
    ++revision;
    return true;
}

void PresentedDrawings::clear(std::uint32_t channel) {
    const auto kGone = std::ranges::remove_if(shapes, [channel](const DebugShape& shape) {
        return channel >= kDebugChannels || shape.channel == channel;
    });
    if (kGone.begin() != shapes.end()) {
        shapes.erase(kGone.begin(), kGone.end());
        ++revision;
    }
}

namespace {

constexpr kest::Parameter kReal{kest::Slot::F64};
constexpr kest::Parameter kFloat{kest::Slot::F32};
constexpr kest::Parameter kWhole{kest::Slot::U32};
/// Two points, a color, seconds and a channel.
constexpr std::array<kest::Parameter, 9> kTwoPoints = {
    kReal, kReal, kReal, kReal, kReal, kReal, kWhole, kFloat, kWhole};
/// A center, a radius, a color, seconds and a channel.
constexpr std::array<kest::Parameter, 7> kCentered = {kReal, kReal, kReal, kFloat, kWhole, kFloat, kWhole};
constexpr std::array<kest::Parameter, 1> kChannel = {kWhole};

/// The drawings a door draws into; none where nothing draws, and none in
/// a shipping build.
PresentedDrawings* drawingsOf(void* context) noexcept {
#if RAWFRAME_SHIPPING
    static_cast<void>(context);
    return nullptr;
#else
    const auto* const kContext = static_cast<const DebugDoorContext*>(context);
    return kContext != nullptr ? kContext->drawings : nullptr;
#endif
}

/// The last tick a shape drawn now for `seconds` shows on: this one for
/// nought, every one until cleared below nought; none for not a number.
std::optional<std::uint64_t>
untilOf(const PresentedDrawings& drawings, const DebugDoorContext& context, double seconds) {
    if (std::isnan(seconds)) {
        return std::nullopt;
    }
    if (seconds < 0) {
        return kDebugForever;
    }
    // Whole ticks, up, past what an f32's seconds carry by rounding: a
    // twentieth of a second is three ticks at sixty, not four.
    const double kTicks = std::ceil((seconds * context.rate.ticks / context.rate.seconds) - 1e-4);
    // A day of ticks at most: anything longer stays until cleared.
    constexpr double kMostTicks = 86400.0 * 1000;
    return kTicks >= kMostTicks ? kDebugForever
                                : drawings.tick + std::max<std::uint64_t>(static_cast<std::uint64_t>(kTicks), 1) - 1;
}

void draw(kest::DoorCall& call, void* context, DebugShape::Kind kind) noexcept {
    PresentedDrawings* drawings = drawingsOf(context);
    if (drawings == nullptr) {
        return;
    }
    const bool kSphere = kind == DebugShape::Kind::Sphere;
    // A sphere's radius sits where the other shapes' second point does.
    const std::size_t kColor = kSphere ? 4 : 6;
    const std::optional<std::uint64_t> kUntil =
        untilOf(*drawings, *static_cast<const DebugDoorContext*>(context), call.real(kColor + 1));
    if (!kUntil.has_value()) {
        ++drawings->leftOut;
        return;
    }
    DebugShape shape{.kind = kind,
                     .a = {call.real(0), call.real(1), call.real(2)},
                     .b = kSphere ? std::array<double, 3>{call.real(3), 0, 0}
                                  : std::array<double, 3>{call.real(3), call.real(4), call.real(5)},
                     .color = static_cast<std::uint32_t>(call.integer(kColor)),
                     .channel = static_cast<std::uint32_t>(call.integer(kColor + 2)),
                     .until = *kUntil};
    if (kind == DebugShape::Kind::Box) {
        // Least and greatest corners, however they were given.
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double kLeast = std::min(shape.a[axis], shape.b[axis]);
            shape.b[axis] = std::max(shape.a[axis], shape.b[axis]);
            shape.a[axis] = kLeast;
        }
    }
    static_cast<void>(drawings->add(shape));
}

void lineDoor(kest::DoorCall& call, void* context) noexcept {
    draw(call, context, DebugShape::Kind::Line);
}

void arrowDoor(kest::DoorCall& call, void* context) noexcept {
    draw(call, context, DebugShape::Kind::Arrow);
}

void boxDoor(kest::DoorCall& call, void* context) noexcept {
    draw(call, context, DebugShape::Kind::Box);
}

void sphereDoor(kest::DoorCall& call, void* context) noexcept {
    draw(call, context, DebugShape::Kind::Sphere);
}

void clearDoor(kest::DoorCall& call, void* context) noexcept {
    if (PresentedDrawings* drawings = drawingsOf(context)) {
        drawings->clear(static_cast<std::uint32_t>(call.integer(0)));
    }
}

} // namespace

result::Status addDebugDoors(kest::DoorTable& doors, const DebugDoorContext* context) {
    auto* const kContext = const_cast<DebugDoorContext*>(context);
    RAWFRAME_TRY(
        doors.add(kest::Door{.name = "Debug.line", .function = &lineDoor, .context = kContext, .takes = kTwoPoints}));
    RAWFRAME_TRY(
        doors.add(kest::Door{.name = "Debug.arrow", .function = &arrowDoor, .context = kContext, .takes = kTwoPoints}));
    RAWFRAME_TRY(
        doors.add(kest::Door{.name = "Debug.box", .function = &boxDoor, .context = kContext, .takes = kTwoPoints}));
    RAWFRAME_TRY(doors.add(
        kest::Door{.name = "Debug.sphere", .function = &sphereDoor, .context = kContext, .takes = kCentered}));
    return doors.add(kest::Door{.name = "Debug.clear", .function = &clearDoor, .context = kContext, .takes = kChannel});
}

} // namespace rawframe::world_kest
