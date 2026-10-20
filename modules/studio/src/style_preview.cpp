#include "rawframe/ui/styles.h"
#include "shell.h"

#include <cinttypes>
#include <cstdio>

namespace rawframe::studio {

namespace {

constexpr std::size_t kMostClasses = 256;
constexpr std::array<std::string_view, 6> kVariantNames = {
    "base", "focused", "hovered", "pressed", "disabled", "checked"};

/// `under` with the parts `over` sets taken from `over`'s look, as a node
/// in that variant shows its class (D431).
ui::Look overlaid(ui::Look under, const ui::StyleLook& over) {
    const auto kSets = [&](ui::LookPart part) {
        return (over.parts & static_cast<ui::LookParts>(part)) != 0;
    };
    if (kSets(ui::LookPart::Fill)) {
        under.fill = over.look.fill;
    }
    if (kSets(ui::LookPart::BorderColor)) {
        under.borderColor = over.look.borderColor;
    }
    if (kSets(ui::LookPart::Radius)) {
        under.radius = over.look.radius;
    }
    if (kSets(ui::LookPart::Clip)) {
        under.clip = over.look.clip;
    }
    if (kSets(ui::LookPart::ImageTint)) {
        under.imageTint = over.look.imageTint;
    }
    if (kSets(ui::LookPart::OuterShadow)) {
        under.outerShadow = over.look.outerShadow;
    }
    if (kSets(ui::LookPart::InnerShadow)) {
        under.innerShadow = over.look.innerShadow;
    }
    if (kSets(ui::LookPart::Gradient)) {
        under.gradient = over.look.gradient;
    }
    return under;
}

} // namespace

void ShellParticipant::showSheetPreview(const Value& sheet) {
    clear(viewRows_);
    viewFields_.clear();
    clear(held_.cameraRows);
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
    forgetUiPreview();
    const auto kRead = ui::readStyles(document::write(sheet));
    if (!kRead.has_value()) {
        return;
    }
    auto pane = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0),
                               .minHeight = ui::pixels(240),
                               .direction = ui::Direction::Column,
                               .gap = 8,
                               .grow = 1,
                               .shrink = 0,
                               .padding = {16, 16, 16, 16},
                               .scroll = ui::Scroll::Both},
                    kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Style preview"));
    // Each class a row: its name, then a sample of each variant it sets,
    // the base always, each looking as a node of the class does in it.
    std::size_t swatches = 0;
    for (std::size_t each = 0; each < kRead->styles.size() && each < kMostClasses; ++each) {
        const ui::StyleClass& kClass = kRead->styles[each];
        char id[24];
        std::snprintf(id, sizeof id, "%016" PRIx64, kClass.id);
        const auto kListed = std::ranges::find(entities_, std::string{id});
        auto line = box(*pane, ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0}, 0);
        if (!line.has_value()) {
            return;
        }
        auto named = box(*line, ui::Layout{.width = ui::pixels(160), .shrink = 0}, 0);
        if (named.has_value()) {
            static_cast<void>(words(*named, kClass.name, kText, 13));
        }
        const ui::Look kBase = overlaid(ui::Look{}, kClass.looks[0]);
        for (std::size_t variant = 0; variant < kClass.looks.size(); ++variant) {
            if (variant != 0 && kClass.looks[variant].parts == 0) {
                continue;
            }
            auto swatch = box(*line,
                              ui::Layout{.width = ui::pixels(112),
                                         .height = ui::pixels(40),
                                         .shrink = 0,
                                         .padding = {8, 4, 8, 4},
                                         .border = {2, 2, 2, 2}},
                              0);
            if (!swatch.has_value()) {
                return;
            }
            static_cast<void>(tree_->setLook(*swatch, variant == 0 ? kBase : overlaid(kBase, kClass.looks[variant])));
            static_cast<void>(words(*swatch, std::string{kVariantNames[variant]}, kText, 12));
            static_cast<void>(tree_->setRole(*swatch, ui::Role::Button));
            static_cast<void>(tree_->setName(*swatch, kClass.name + " " + std::string{kVariantNames[variant]}));
            if (kListed != entities_.end()) {
                const std::size_t kEntity = static_cast<std::size_t>(kListed - entities_.begin());
                held_.canvasParts.emplace_back(*swatch, kEntity);
                held_.previewBoxes.emplace_back(*swatch, kEntity);
            }
            ++swatches;
        }
    }
    markUiPreview();
    emitter_.log(diagnostics::Severity::Info,
                 kStylePreviewShown,
                 "a style sheet's classes shown as each of their variants looks",
                 {diagnostics::field("classes", kRead->styles.size()), diagnostics::field("swatches", swatches)});
}

} // namespace rawframe::studio
