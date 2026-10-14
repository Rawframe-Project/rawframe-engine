#include "rawframe/ui/node_look.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace rawframe::ui {

namespace {

/// A size along one axis: automatic at nought and nought.
Dimension dimensionOf(float scale, float offset) noexcept {
    return scale == 0 && offset == 0 ? Dimension{} : Dimension{.automatic = false, .scale = scale, .offset = offset};
}

} // namespace

std::optional<Layout> layoutOf(const NodeFields& node) noexcept {
    if (node.direction > 3 || node.justify > 5 || node.alignItems > 4 || node.alignSelf > 4 || node.scroll > 3) {
        return std::nullopt;
    }
    const auto kSides = [](float value) {
        return std::array<float, 4>{value, value, value, value};
    };
    return Layout{
        .width = dimensionOf(node.widthScale, node.widthOffset),
        .height = dimensionOf(node.heightScale, node.heightOffset),
        .direction = static_cast<Direction>(node.direction),
        .justify = static_cast<Justify>(node.justify),
        // A parent's AUTO stretches.
        .alignItems = node.alignItems == 0 ? Align::Stretch : static_cast<Align>(node.alignItems),
        .gap = node.gap,
        .grow = node.grow,
        .shrink = node.shrink,
        .alignSelf = static_cast<Align>(node.alignSelf),
        .padding = kSides(node.padding),
        // A popup's margin is its gap from its anchor (D623).
        .margin = kSides(node.popup != 0 ? 0 : node.margin),
        .border = kSides(node.border),
        .placement = {.absolute = node.absolute != 0 || node.popup != 0,
                      .x = {.automatic = false, .scale = node.xScale, .offset = node.xOffset},
                      .y = {.automatic = false, .scale = node.yScale, .offset = node.yOffset},
                      .anchorX = node.anchorX,
                      .anchorY = node.anchorY},
        .scroll = static_cast<Scroll>(node.scroll),
    };
}

Look lookOf(const NodeFields& node) noexcept {
    const float kSlice = node.imageSlice;
    const GradientLook kGradient =
        node.gradientKind == 0
            ? GradientLook{}
            : GradientLook{.kind = static_cast<GradientLook::Kind>(std::min<std::uint32_t>(node.gradientKind, 255)),
                           .angle = node.gradientAngle,
                           .colors = {node.gradientFrom, node.gradientTo},
                           .positions = {0, 1},
                           .stops = 2};
    return Look{
        .fill = node.fill,
        .borderColor = node.borderColor,
        .radius = node.radius,
        .clip = node.clip != 0,
        .image = node.image,
        .imageSlice = {kSlice, kSlice, kSlice, kSlice},
        // Nought draws it as it is.
        .imageTint = node.imageTint != 0 ? node.imageTint : 0xFFFFFFFF,
        .outerShadow = {.color = node.shadowColor, .x = node.shadowX, .y = node.shadowY, .blur = node.shadowBlur},
        .gradient = kGradient};
}

LookParts ownPartsOf(const NodeFields& node) noexcept {
    LookParts parts = 0;
    const auto kOwn = [&parts](bool given, LookPart part) {
        parts |= given ? static_cast<LookParts>(part) : 0;
    };
    kOwn(node.fill != 0, LookPart::Fill);
    kOwn(node.borderColor != 0, LookPart::BorderColor);
    kOwn(node.radius != 0, LookPart::Radius);
    kOwn(node.clip != 0, LookPart::Clip);
    kOwn(node.image != 0, LookPart::Image);
    kOwn(node.imageTint != 0, LookPart::ImageTint);
    kOwn(node.shadowColor != 0, LookPart::OuterShadow);
    kOwn(node.gradientKind != 0, LookPart::Gradient);
    return parts;
}

TextLook textLookOf(const NodeFields& node, Font font) noexcept {
    return TextLook{.font = font,
                    .size = node.textSize > 0 ? node.textSize : 16,
                    .color = node.textColor,
                    .align = static_cast<TextAlign>(node.textAlign),
                    .wrap = node.textWrap == 0};
}

} // namespace rawframe::ui
