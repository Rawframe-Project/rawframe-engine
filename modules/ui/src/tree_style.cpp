// Style classes (SPEC-0030, D431): Maul UI's classes, their variants'
// looks and transitions, and a node's classes and states.

#include "rawframe/ui/tree.h"
#include "tree_state.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <maul-ui/transition.h>
#include <utility>

namespace rawframe::ui {

namespace {

constexpr muiPropertyGroup kVisual = MUI_PROPERTY_GROUP(mui_propertyBackground);
constexpr std::size_t kMostClasses = 8;

muiStyleId styleIdOf(Style style) noexcept {
    return muiStyleId{.index1 = static_cast<std::uint32_t>(style.key),
                      .generation = static_cast<std::uint32_t>(style.key >> 32U)};
}

muiVariant variantOf(Variant variant) noexcept {
    constexpr std::array<muiVariant, 5> kVariants = {
        mui_variantBase, mui_variantFocused, mui_variantHovered, mui_variantPressed, mui_variantDisabled};
    return kVariants[static_cast<std::size_t>(variant)];
}

} // namespace

muiPropertyMask maskOf(LookParts parts) noexcept {
    constexpr std::array<std::pair<LookPart, std::array<muiProperty, 4>>, 9> kProperties = {{
        {LookPart::Fill, {mui_propertyBackground}},
        {LookPart::BorderColor,
         {mui_propertyBorderColorStart,
          mui_propertyBorderColorEnd,
          mui_propertyBorderColorTop,
          mui_propertyBorderColorBottom}},
        {LookPart::Radius,
         {mui_propertyRadiusTopStart,
          mui_propertyRadiusTopEnd,
          mui_propertyRadiusBottomEnd,
          mui_propertyRadiusBottomStart}},
        {LookPart::Clip, {mui_propertyClip}},
        {LookPart::Image, {mui_propertyImage, mui_propertyImageSlice}},
        {LookPart::ImageTint, {mui_propertyImageTint}},
        {LookPart::OuterShadow, {mui_propertyOuterShadow}},
        {LookPart::InnerShadow, {mui_propertyInnerShadow}},
        {LookPart::Gradient, {mui_propertyGradient}},
    }};
    muiPropertyMask mask = 0;
    for (const auto& [kPart, kNamed] : kProperties) {
        if ((parts & static_cast<LookParts>(kPart)) == 0) {
            continue;
        }
        for (const muiProperty kProperty : kNamed) {
            // Property nought, the layout's width, ends a shorter list.
            if (kProperty != 0) {
                mask |= MUI_PROPERTY_BIT(kProperty);
            }
        }
    }
    return mask;
}

result::Result<Style> Tree::addStyle() {
    muiStyleId made{};
    RAWFRAME_TRY(checked(muiCreateStyle(state_->context, &made), "a style class could not be made"));
    return Style{.key = made.index1 | (std::uint64_t{made.generation} << 32U)};
}

result::Status Tree::removeStyle(Style style) {
    return checked(muiDestroyStyle(state_->context, styleIdOf(style)), "a style class could not be removed");
}

result::Status Tree::setStyleLook(Style style, Variant variant, const Look& look, LookParts parts) {
    const muiVisualStyle kStyle = visualOf(look);
    return checked(
        muiStyle_SetVisualValues(state_->context, styleIdOf(style), variantOf(variant), &kStyle, maskOf(parts)),
        "a style class's look was refused");
}

result::Status Tree::setStyleTransition(Style style, Variant variant, LookParts parts, const Transition& transition) {
    const auto kTime = [](float seconds) {
        return std::isfinite(seconds) && seconds >= 0 && seconds < 3600;
    };
    if (!kTime(transition.seconds) || !kTime(transition.delaySeconds)) {
        return refuse(UiError::Invalid, "a transition's time is below nought or not a time");
    }
    muiTransitionDef def = muiDefaultTransitionDef();
    def.kind = transition.kind == Transition::Kind::Spring ? mui_transitionSpring : mui_transitionTimed;
    def.delayNs = static_cast<std::uint64_t>(static_cast<double>(transition.delaySeconds) * 1e9);
    def.durationNs = static_cast<std::uint64_t>(static_cast<double>(transition.seconds) * 1e9);
    def.easing = static_cast<muiEasing>(transition.easing);
    for (std::size_t at = 0; at < transition.bezier.size(); ++at) {
        def.bezier[at] = transition.bezier[at];
    }
    def.frequency = transition.frequency;
    def.dampingRatio = transition.dampingRatio;
    muiTransitionId made{};
    RAWFRAME_TRY(checked(muiCreateTransition(state_->context, &def, &made), "a transition was refused"));
    // The spec lives as long as the tree: a class names it.
    return checked(
        muiStyle_SetTransition(state_->context, styleIdOf(style), variantOf(variant), made, kVisual, maskOf(parts)),
        "a style class's transition was refused");
}

result::Status Tree::setClasses(Node node, std::span<const Style> styles) {
    if (styles.size() > kMostClasses) {
        return refuse(UiError::Invalid, "a node has at most 8 classes");
    }
    std::array<muiStyleId, kMostClasses> ids{};
    for (std::size_t at = 0; at < styles.size(); ++at) {
        ids[at] = styleIdOf(styles[at]);
    }
    return checked(
        muiNode_SetClasses(state_->context, idOf(node), ids.data(), static_cast<std::uint32_t>(styles.size())),
        "a UI node's classes were refused");
}

result::Status Tree::setStates(Node node, States states) {
    muiState bits = 0;
    bits |= states.focused ? mui_stateFocused : 0;
    bits |= states.hovered ? mui_stateHovered : 0;
    bits |= states.pressed ? mui_statePressed : 0;
    bits |= states.disabled ? mui_stateDisabled : 0;
    return checked(muiNode_SetStates(state_->context, idOf(node), bits), "a UI node's states were refused");
}

bool Tree::transitioning(Node node) const noexcept {
    for (muiProperty property = mui_propertyBackground; property <= mui_propertyClip; ++property) {
        if (muiNode_IsTransitioning(state_->context, idOf(node), property)) {
            return true;
        }
    }
    return false;
}

} // namespace rawframe::ui
