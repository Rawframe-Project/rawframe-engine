#include "rawframe/ui/styles.h"

#include "rawframe/base/hex64.h"
#include "rawframe/document/json.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>

namespace rawframe::ui {

namespace {

using document::invalid;
using document::Record;
using document::Value;

constexpr std::array<std::string_view, 3> kDocumentFields = {"kind", "formatVersion", "styles"};
constexpr std::array<std::string_view, 8> kStyleFields = {
    "styleId", "name", "base", "focused", "hovered", "pressed", "disabled", "transitions"};
// A variant's look, in Variant's order of the style's fields after `name`.
constexpr std::array<std::string_view, 5> kVariantFields = {"base", "focused", "hovered", "pressed", "disabled"};
constexpr std::array<std::string_view, 8> kLookFields = {
    "fill", "borderColor", "radius", "clip", "imageTint", "outerShadow", "innerShadow", "gradient"};
constexpr std::array<std::string_view, 5> kShadowFields = {"color", "x", "y", "blur", "spread"};
constexpr std::array<std::string_view, 4> kGradientFields = {"kind", "angle", "from", "to"};
constexpr std::array<std::string_view, 8> kTransitionFields = {
    "parts", "kind", "seconds", "delaySeconds", "easing", "bezier", "frequency", "dampingRatio"};
constexpr std::array<std::string_view, 6> kEasings = {
    "linear", "ease", "ease_in", "ease_out", "ease_in_out", "cubic_bezier"};
// The parts a transition names, as the look's fields name them.
constexpr std::array<std::pair<std::string_view, LookPart>, 8> kPartNames = {{{"fill", LookPart::Fill},
                                                                              {"borderColor", LookPart::BorderColor},
                                                                              {"radius", LookPart::Radius},
                                                                              {"clip", LookPart::Clip},
                                                                              {"imageTint", LookPart::ImageTint},
                                                                              {"outerShadow", LookPart::OuterShadow},
                                                                              {"innerShadow", LookPart::InnerShadow},
                                                                              {"gradient", LookPart::Gradient}}};
// Lengths and offsets in logical pixels, and times in seconds, are bounded
// so no document asks for a look past what a screen shows.
constexpr double kMostPixels = 4096;
constexpr double kMostSeconds = 60;

std::string indexed(const std::string& path, std::size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

/// `#rrggbbaa`, lowercase, as 0xRRGGBBAA.
result::Result<std::uint32_t> colorOf(const Record& record, std::string_view field) {
    RAWFRAME_TRY_ASSIGN(const std::string_view kText, record.text(field));
    const auto kValue = kText.size() == 9 && kText.front() == '#'
                            ? base::parseHex64(std::string(8, '0') + std::string{kText.substr(1)})
                            : std::nullopt;
    if (!kValue.has_value()) {
        return invalid(record.pathOf(field), "a color is # and 8 lowercase hexadecimal digits, #rrggbbaa");
    }
    return static_cast<std::uint32_t>(*kValue);
}

/// A real within `lowest` and `highest`; absent, `fallback`.
result::Result<float>
realOf(const Record& record, std::string_view field, double fallback, double lowest, double highest) {
    RAWFRAME_TRY_ASSIGN(const double kValue, record.real(field, fallback));
    if (!std::isfinite(kValue) || kValue < lowest || kValue > highest) {
        return invalid(record.pathOf(field), "a number out of its range");
    }
    return static_cast<float>(kValue);
}

result::Result<ShadowLook> shadowOf(const Value& value, const std::string& path) {
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kShadowFields, path));
    ShadowLook shadow;
    RAWFRAME_TRY_ASSIGN(shadow.color, colorOf(kRecord, "color"));
    RAWFRAME_TRY_ASSIGN(shadow.x, realOf(kRecord, "x", 0, -kMostPixels, kMostPixels));
    RAWFRAME_TRY_ASSIGN(shadow.y, realOf(kRecord, "y", 0, -kMostPixels, kMostPixels));
    RAWFRAME_TRY_ASSIGN(shadow.blur, realOf(kRecord, "blur", 0, 0, kMostPixels));
    RAWFRAME_TRY_ASSIGN(shadow.spread, realOf(kRecord, "spread", 0, -kMostPixels, kMostPixels));
    return shadow;
}

result::Result<GradientLook> gradientOf(const Value& value, const std::string& path) {
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kGradientFields, path));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    GradientLook gradient;
    if (kKind == "linear") {
        gradient.kind = GradientLook::Kind::Linear;
    } else if (kKind == "radial") {
        gradient.kind = GradientLook::Kind::Radial;
    } else {
        return invalid(kRecord.pathOf("kind"), "a gradient is linear or radial");
    }
    RAWFRAME_TRY_ASSIGN(gradient.angle, realOf(kRecord, "angle", 0, 0, 360));
    RAWFRAME_TRY_ASSIGN(gradient.colors[0], colorOf(kRecord, "from"));
    RAWFRAME_TRY_ASSIGN(gradient.colors[1], colorOf(kRecord, "to"));
    gradient.positions = {0, 1};
    gradient.stops = 2;
    return gradient;
}

/// A variant's look: the parts given, and only those.
result::Result<StyleLook> lookOf(const Value& value, const std::string& path) {
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kLookFields, path));
    StyleLook styled;
    const auto kHas = [&value](std::string_view field) {
        return value.find(field) != nullptr;
    };
    if (kHas("fill")) {
        RAWFRAME_TRY_ASSIGN(styled.look.fill, colorOf(kRecord, "fill"));
        styled.parts |= static_cast<LookParts>(LookPart::Fill);
    }
    if (kHas("borderColor")) {
        RAWFRAME_TRY_ASSIGN(styled.look.borderColor, colorOf(kRecord, "borderColor"));
        styled.parts |= static_cast<LookParts>(LookPart::BorderColor);
    }
    if (kHas("radius")) {
        RAWFRAME_TRY_ASSIGN(const Value* radius, kRecord.required("radius", Value::Kind::Number));
        const std::optional<double> kRadius = radius->real();
        if (!kRadius.has_value() || !std::isfinite(*kRadius) || *kRadius < 0 || *kRadius > kMostPixels) {
            return invalid(kRecord.pathOf("radius"), "a radius is from nought to 4096 pixels");
        }
        styled.look.radius = static_cast<float>(*kRadius);
        styled.parts |= static_cast<LookParts>(LookPart::Radius);
    }
    if (kHas("clip")) {
        RAWFRAME_TRY_ASSIGN(const Value* clip, kRecord.required("clip", Value::Kind::Bool));
        styled.look.clip = clip->truth().value_or(false);
        styled.parts |= static_cast<LookParts>(LookPart::Clip);
    }
    if (kHas("imageTint")) {
        RAWFRAME_TRY_ASSIGN(styled.look.imageTint, colorOf(kRecord, "imageTint"));
        styled.parts |= static_cast<LookParts>(LookPart::ImageTint);
    }
    if (kHas("outerShadow")) {
        RAWFRAME_TRY_ASSIGN(const Value* shadow, kRecord.required("outerShadow", Value::Kind::Object));
        RAWFRAME_TRY_ASSIGN(styled.look.outerShadow, shadowOf(*shadow, kRecord.pathOf("outerShadow")));
        styled.parts |= static_cast<LookParts>(LookPart::OuterShadow);
    }
    if (kHas("innerShadow")) {
        RAWFRAME_TRY_ASSIGN(const Value* shadow, kRecord.required("innerShadow", Value::Kind::Object));
        RAWFRAME_TRY_ASSIGN(styled.look.innerShadow, shadowOf(*shadow, kRecord.pathOf("innerShadow")));
        styled.parts |= static_cast<LookParts>(LookPart::InnerShadow);
    }
    if (kHas("gradient")) {
        RAWFRAME_TRY_ASSIGN(const Value* gradient, kRecord.required("gradient", Value::Kind::Object));
        RAWFRAME_TRY_ASSIGN(styled.look.gradient, gradientOf(*gradient, kRecord.pathOf("gradient")));
        styled.parts |= static_cast<LookParts>(LookPart::Gradient);
    }
    if (styled.parts == 0) {
        return document::notCanonical(path, "a look that sets nothing is omitted");
    }
    return styled;
}

result::Result<std::pair<LookParts, Transition>> transitionOf(const Value& value, const std::string& path) {
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kTransitionFields, path));
    RAWFRAME_TRY_ASSIGN(const Value* parts, kRecord.required("parts", Value::Kind::Array));
    LookParts named = 0;
    for (std::size_t index = 0; index < parts->items().size(); ++index) {
        const std::string* kName = parts->items()[index].text();
        const auto kPart = kName == nullptr
                               ? kPartNames.end()
                               : std::ranges::find(kPartNames, *kName, &std::pair<std::string_view, LookPart>::first);
        if (kPart == kPartNames.end()) {
            return invalid(indexed(kRecord.pathOf("parts"), index), "a part is one a look has");
        }
        if ((named & static_cast<LookParts>(kPart->second)) != 0) {
            return invalid(indexed(kRecord.pathOf("parts"), index), "a part is named once");
        }
        named |= static_cast<LookParts>(kPart->second);
    }
    if (named == 0) {
        return invalid(kRecord.pathOf("parts"), "a transition moves at least one part");
    }
    Transition transition;
    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kKind, kRecord.optionalText("kind"));
    if (kKind == "timed") {
        return document::notCanonical(kRecord.pathOf("kind"), "a field at its default is omitted");
    }
    if (kKind.has_value() && kKind != "spring") {
        return invalid(kRecord.pathOf("kind"), "a transition is timed or spring");
    }
    const bool kSpring = kKind.has_value();
    const auto kOnly = [&](std::string_view field, bool allowed) -> result::Status {
        if (!allowed && value.find(field) != nullptr) {
            return invalid(kRecord.pathOf(field),
                           kSpring ? "a spring has no such field" : "a timed transition has no such field");
        }
        return {};
    };
    RAWFRAME_TRY(kOnly("seconds", !kSpring));
    RAWFRAME_TRY(kOnly("delaySeconds", !kSpring));
    RAWFRAME_TRY(kOnly("easing", !kSpring));
    RAWFRAME_TRY(kOnly("bezier", !kSpring));
    RAWFRAME_TRY(kOnly("frequency", kSpring));
    RAWFRAME_TRY(kOnly("dampingRatio", kSpring));
    if (kSpring) {
        transition.kind = Transition::Kind::Spring;
        RAWFRAME_TRY_ASSIGN(transition.frequency, realOf(kRecord, "frequency", 2, 0.01, 100));
        RAWFRAME_TRY_ASSIGN(transition.dampingRatio, realOf(kRecord, "dampingRatio", 1, 0.01, 100));
        return std::pair{named, transition};
    }
    RAWFRAME_TRY_ASSIGN(transition.seconds, realOf(kRecord, "seconds", 0.25, 0, kMostSeconds));
    RAWFRAME_TRY_ASSIGN(transition.delaySeconds, realOf(kRecord, "delaySeconds", 0, 0, kMostSeconds));
    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kEasing, kRecord.optionalText("easing"));
    if (kEasing == "ease") {
        return document::notCanonical(kRecord.pathOf("easing"), "a field at its default is omitted");
    }
    if (kEasing.has_value()) {
        const auto kFound = std::ranges::find(kEasings, *kEasing);
        if (kFound == kEasings.end()) {
            return invalid(kRecord.pathOf("easing"),
                           "an easing is linear, ease, ease_in, ease_out, ease_in_out, or cubic_bezier");
        }
        transition.easing = static_cast<Transition::Easing>(kFound - kEasings.begin());
    }
    const bool kBezier = transition.easing == Transition::Easing::CubicBezier;
    RAWFRAME_TRY_ASSIGN(const Value* bezier, kRecord.optional("bezier", Value::Kind::Array));
    if ((bezier != nullptr) != kBezier) {
        return invalid(kRecord.pathOf("bezier"), "control points are given with cubic_bezier, and only then");
    }
    if (bezier != nullptr) {
        if (bezier->items().size() != 4) {
            return invalid(kRecord.pathOf("bezier"), "a cubic Bezier has four numbers, x1, y1, x2, y2");
        }
        for (std::size_t index = 0; index < 4; ++index) {
            const std::optional<double> kPoint = bezier->items()[index].real();
            // Both x within nought and one; each y within a sane overshoot.
            const double kLimit = index % 2 == 0 ? 1 : 4;
            const double kLow = index % 2 == 0 ? 0 : -3;
            if (!kPoint.has_value() || !std::isfinite(*kPoint) || *kPoint < kLow || *kPoint > kLimit) {
                return invalid(indexed(kRecord.pathOf("bezier"), index), "a control point out of its range");
            }
            transition.bezier[index] = static_cast<float>(*kPoint);
        }
    }
    return std::pair{named, transition};
}

result::Result<StyleClass> styleOf(const Value& value, const std::string& path, const StyleLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kStyleFields, path));
    StyleClass style;
    RAWFRAME_TRY_ASSIGN(const std::string_view kId, kRecord.text("styleId"));
    const auto kParsed = base::parseHex64(kId);
    if (!kParsed.has_value() || *kParsed == 0) {
        return invalid(kRecord.pathOf("styleId"), "an identity is 16 lowercase hexadecimal digits, not all nought");
    }
    style.id = *kParsed;
    RAWFRAME_TRY_ASSIGN(const std::string_view kName, kRecord.text("name"));
    const bool kMachine = !kName.empty() && kName.size() <= limits.maximumNameLength && kName.front() >= 'a' &&
                          kName.front() <= 'z' && std::ranges::all_of(kName, [](char each) {
                              return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
                          });
    if (!kMachine) {
        return invalid(kRecord.pathOf("name"), "a name is a lowercase letter, then letters, digits, or underscores");
    }
    style.name = std::string{kName};
    for (std::size_t variant = 0; variant < kVariantFields.size(); ++variant) {
        RAWFRAME_TRY_ASSIGN(const Value* look, kRecord.optional(kVariantFields[variant], Value::Kind::Object));
        if (look != nullptr) {
            RAWFRAME_TRY_ASSIGN(style.looks[variant], lookOf(*look, kRecord.pathOf(kVariantFields[variant])));
        }
    }
    RAWFRAME_TRY_ASSIGN(const Value* transitions, kRecord.optional("transitions", Value::Kind::Array));
    if (transitions != nullptr) {
        if (transitions->items().empty()) {
            return document::notCanonical(kRecord.pathOf("transitions"), "a field at its default is omitted");
        }
        if (transitions->items().size() > limits.maximumTransitions) {
            return invalid(kRecord.pathOf("transitions"), "more transitions than allowed");
        }
        LookParts moved = 0;
        for (std::size_t index = 0; index < transitions->items().size(); ++index) {
            const std::string kPath = indexed(kRecord.pathOf("transitions"), index);
            RAWFRAME_TRY_ASSIGN(auto transition, transitionOf(transitions->items()[index], kPath));
            if ((moved & transition.first) != 0) {
                return invalid(kPath + ".parts", "a part moves by one transition");
            }
            moved |= transition.first;
            style.transitions.push_back(transition);
        }
    }
    return style;
}

} // namespace

std::optional<std::size_t> StyleSheet::styleWithId(std::uint64_t id) const noexcept {
    const auto kFound = std::ranges::find(styles, id, &StyleClass::id);
    return kFound == styles.end() ? std::nullopt : std::optional{static_cast<std::size_t>(kFound - styles.begin())};
}

result::Result<StyleSheet> readStyles(std::string_view text, const StyleLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Value kRoot, document::parseCanonical(text));
    // The version first: a document of another version is not read further.
    const Value* version = kRoot.find("formatVersion");
    if (version == nullptr || version->integer() != 1) {
        return invalid("formatVersion", "the format version is 1");
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(kRoot, kDocumentFields, "$"));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    if (kKind != "ui.styles") {
        return invalid(kRecord.pathOf("kind"), "the kind is ui.styles");
    }
    RAWFRAME_TRY_ASSIGN(const Value* styles, kRecord.required("styles", Value::Kind::Array));
    if (styles->items().size() > limits.maximumStyles) {
        return invalid(kRecord.pathOf("styles"), "more styles than allowed");
    }
    StyleSheet sheet;
    std::set<std::uint64_t> identities;
    std::set<std::string> names;
    for (std::size_t index = 0; index < styles->items().size(); ++index) {
        const std::string kPath = indexed(kRecord.pathOf("styles"), index);
        RAWFRAME_TRY_ASSIGN(StyleClass style, styleOf(styles->items()[index], kPath, limits));
        if (!identities.insert(style.id).second) {
            return invalid(kPath + ".styleId", "an identity is used once in a document");
        }
        if (!names.insert(style.name).second) {
            return invalid(kPath + ".name", "a style's name is used once");
        }
        sheet.styles.push_back(std::move(style));
    }
    return sheet;
}

result::Result<std::vector<Style>> addStyles(Tree& tree, const StyleSheet& sheet) {
    std::vector<Style> made;
    made.reserve(sheet.styles.size());
    for (const StyleClass& kClass : sheet.styles) {
        RAWFRAME_TRY_ASSIGN(const Style kStyle, tree.addStyle());
        made.push_back(kStyle);
        for (std::size_t variant = 0; variant < kClass.looks.size(); ++variant) {
            const StyleLook& kLook = kClass.looks[variant];
            if (kLook.parts != 0) {
                RAWFRAME_TRY(tree.setStyleLook(kStyle, static_cast<Variant>(variant), kLook.look, kLook.parts));
            }
        }
        // A part moves the same way into every state, and back to the base.
        for (const auto& [kParts, kTransition] : kClass.transitions) {
            for (std::size_t variant = 0; variant < kClass.looks.size(); ++variant) {
                RAWFRAME_TRY(tree.setStyleTransition(kStyle, static_cast<Variant>(variant), kParts, kTransition));
            }
        }
    }
    return made;
}

} // namespace rawframe::ui
