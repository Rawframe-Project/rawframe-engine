#include "rawframe/ui/node_look.h"
#include "rawframe/ui/styles.h"
#include "shell.h"

#include <map>

namespace rawframe::studio {

namespace {

/// The screen a UI document is laid out on: a game's window has no size an
/// editor knows, so a common one.
constexpr float kScreenWidth = 960;
constexpr float kScreenHeight = 540;
constexpr std::uint32_t kScreenFill = 0x101216FFU;
constexpr float kFrame = 2;

} // namespace

void ShellParticipant::showUiPreview(const Value& document) {
    clear(viewRows_);
    viewFields_.clear();
    clear(held_.cameraRows);
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
    forgetUiPreview();
    // Read as the game reads it, so what is laid out is what it shows; a
    // document it would refuse has no preview.
    const auto kRead = ui::readDocument(document::write(document));
    if (!kRead.has_value()) {
        return;
    }
    // The game's classes made in Studio's tree, from its style sheet as the
    // session holds it, so a styled node looks as its class says.
    std::map<std::uint64_t, ui::Style> classes;
    const auto kSheetAt = std::ranges::find(listed_, Listed::Sheet);
    if (kSheetAt != listed_.end()) {
        const std::optional<Value> kSheetRead =
            read(scenes_[static_cast<std::size_t>(kSheetAt - listed_.begin())], "styles.read_sheet");
        const Value* kSheet = kSheetRead.has_value() ? kSheetRead->find("sheet") : nullptr;
        const auto kStyles = kSheet != nullptr ? ui::readStyles(document::write(*kSheet))
                                               : result::Result<ui::StyleSheet>{ui::StyleSheet{}};
        auto made = kStyles.has_value() ? ui::addStyles(*tree_, *kStyles) : result::Result<std::vector<ui::Style>>{};
        if (kStyles.has_value() && made.has_value()) {
            held_.previewStyles = *made;
            for (std::size_t each = 0; each < made->size(); ++each) {
                classes.emplace(kStyles->styles[each].id, (*made)[each]);
            }
        }
    }
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "UI preview"));
    auto screen = box(*pane,
                      ui::Layout{.width = ui::pixels(kScreenWidth),
                                 .height = ui::pixels(kScreenHeight),
                                 .direction = ui::Direction::Column,
                                 .alignItems = ui::Align::Start,
                                 .shrink = 0,
                                 .margin = {16, 16, 16, 16}},
                      kScreenFill);
    if (!screen.has_value()) {
        return;
    }
    // Each node a box inside its parent's, laid out and looking as the
    // game's tree would make it from its fields (ui::layoutOf, lookOf),
    // its image left out: Studio holds none of the game's textures. One
    // that shows words says whose they are.
    std::vector<ui::Node> made;
    std::size_t laid = 0;
    for (const ui::DocumentNode& kNode : kRead->nodes) {
        const ui::Node kParent =
            kNode.parent.has_value() && *kNode.parent < made.size() ? made[*kNode.parent] : *screen;
        const std::optional<ui::Layout> kLaidOut = ui::layoutOf(kNode.fields);
        auto node = box(kParent, kLaidOut.value_or(ui::Layout{}), 0);
        if (!node.has_value()) {
            return;
        }
        made.push_back(*node);
        ui::Look look = ui::lookOf(kNode.fields);
        look.image = 0;
        // A styled node sets only what it gives itself, its class the rest,
        // as the game's tree has it (D431).
        const auto kClass = kNode.fields.style != 0 ? classes.find(kNode.fields.style) : classes.end();
        if (kClass != classes.end()) {
            static_cast<void>(tree_->setClasses(*node, std::span{&kClass->second, 1}));
        }
        static_cast<void>(
            tree_->setLook(*node, look, kClass != classes.end() ? ui::ownPartsOf(kNode.fields) : ui::kEveryLookPart));
        if (kNode.fields.text != 0 || !kNode.shows.empty()) {
            const float kSize = kNode.fields.textSize > 0 ? kNode.fields.textSize : 14;
            const std::uint32_t kColor = kNode.fields.textColor != 0 ? kNode.fields.textColor : kText;
            static_cast<void>(words(*node, kNode.shows.empty() ? kNode.component : kNode.shows, kColor, kSize));
        }
        laid += kLaidOut.has_value() ? 1 : 0;
        // Named and pressed as its row: the node's component.
        static_cast<void>(tree_->setRole(*node, ui::Role::Button));
        static_cast<void>(tree_->setName(*node, kNode.component + " preview"));
        const auto kListed = std::ranges::find(entities_, kNode.component);
        if (kListed != entities_.end()) {
            const std::size_t kEntity = static_cast<std::size_t>(kListed - entities_.begin());
            held_.canvasParts.emplace_back(*node, kEntity);
            held_.previewBoxes.emplace_back(*node, kEntity);
        }
    }
    markUiPreview();
    emitter_.log(diagnostics::Severity::Info,
                 kUiPreviewShown,
                 "a UI document laid out as its game would show it",
                 {diagnostics::field("nodes", kRead->nodes.size()),
                  diagnostics::field("laid", laid),
                  diagnostics::field("pressable", held_.previewBoxes.size()),
                  diagnostics::field("classes", held_.previewStyles.size())});
}

void ShellParticipant::forgetUiPreview() {
    held_.previewBoxes.clear();
    held_.previewFrame.reset();
    for (const ui::Style kStyle : held_.previewStyles) {
        static_cast<void>(tree_->removeStyle(kStyle));
    }
    held_.previewStyles.clear();
}

void ShellParticipant::markUiPreview() {
    if (held_.previewFrame.has_value()) {
        static_cast<void>(tree_->remove(*held_.previewFrame));
        held_.previewFrame.reset();
    }
    // The chosen node framed by a box laid over it, apart from its layout,
    // so marking it moves nothing.
    const auto kChosenBox = std::ranges::find(held_.previewBoxes, entityAt_, &std::pair<ui::Node, std::size_t>::second);
    if (kChosenBox == held_.previewBoxes.end()) {
        return;
    }
    auto frame = box(kChosenBox->first,
                     ui::Layout{.width = ui::share(1),
                                .height = ui::share(1),
                                .border = {kFrame, kFrame, kFrame, kFrame},
                                .placement = ui::Placement{.absolute = true, .x = ui::pixels(0), .y = ui::pixels(0)}},
                     0);
    if (frame.has_value()) {
        static_cast<void>(tree_->setLook(*frame, ui::Look{.borderColor = kChosen}));
        static_cast<void>(tree_->setInteraction(*frame, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        held_.previewFrame = *frame;
    }
}

} // namespace rawframe::studio
