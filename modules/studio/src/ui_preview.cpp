#include "rawframe/ui/node_look.h"
#include "rawframe/ui/styles.h"
#include "shell.h"

#include <charconv>
#include <map>

namespace rawframe::studio {

namespace {

/// The screen a UI document is laid out on: a game's window has no size an
/// editor knows, so a common one.
constexpr float kScreenWidth = 960;
constexpr float kScreenHeight = 540;
constexpr std::uint32_t kScreenFill = 0x101216FFU;
constexpr float kFrame = 2;

/// `value` as the 16 hexadecimal digits a document writes an identity in.
std::optional<std::uint64_t> hexIdOf(std::string_view value) {
    std::uint64_t made = 0;
    const auto [kStop, kError] = std::from_chars(value.data(), value.data() + value.size(), made, 16);
    if (kError != std::errc{} || kStop != value.data() + value.size() || made == 0) {
        return std::nullopt;
    }
    return made;
}

} // namespace

std::string ShellParticipant::labelMessage(std::uint64_t label) {
    const auto kAsset = std::ranges::find_if(assets_, [label](const Asset& each) {
        return each.kind == "label" && each.id == label;
    });
    const std::size_t kSlash = kAsset != assets_.end() ? kAsset->name.find('/') : std::string::npos;
    if (kSlash == std::string::npos) {
        return {};
    }
    const std::optional<Value> kRead = read(kAsset->name.substr(0, kSlash), "data.read");
    const Value* kData = kRead.has_value() ? kRead->find("data") : nullptr;
    const Value* kEntries = kData != nullptr ? kData->find("entries") : nullptr;
    const Value* kEntry = kEntries != nullptr ? kEntries->find(kAsset->name.substr(kSlash + 1)) : nullptr;
    const Value* kMessage = kEntry != nullptr ? kEntry->find("message") : nullptr;
    if (kMessage == nullptr || kMessage->text() == nullptr) {
        return {};
    }
    // `{$coins :integer}` said as `{coins}`.
    std::string made;
    const std::string& kWritten = *kMessage->text();
    for (std::size_t at = 0; at < kWritten.size(); ++at) {
        if (kWritten.compare(at, 2, "{$") == 0) {
            const std::size_t kEnd = kWritten.find('}', at);
            const std::size_t kName = kWritten.find_first_of(" }", at + 2);
            if (kEnd != std::string::npos && kName != std::string::npos) {
                made += "{" + kWritten.substr(at + 2, kName - at - 2) + "}";
                at = kEnd;
                continue;
            }
        }
        made += kWritten[at];
    }
    return made;
}

std::string ShellParticipant::uiNamed(std::string_view member, std::string_view value) {
    const std::optional<std::uint64_t> kId = hexIdOf(value);
    if (!kId.has_value()) {
        return {};
    }
    if (member == "style") {
        const auto kSheetAt = std::ranges::find(listed_, Listed::Sheet);
        const std::optional<Value> kSheetRead =
            kSheetAt != listed_.end()
                ? read(scenes_[static_cast<std::size_t>(kSheetAt - listed_.begin())], "styles.read_sheet")
                : std::nullopt;
        const Value* kSheet = kSheetRead.has_value() ? kSheetRead->find("sheet") : nullptr;
        const Value* kStyles = kSheet != nullptr ? kSheet->find("styles") : nullptr;
        for (const Value& kStyle : kStyles != nullptr ? kStyles->items() : std::span<const Value>{}) {
            const Value* kStyleId = kStyle.find("styleId");
            const Value* kName = kStyle.find("name");
            if (kStyleId != nullptr && kStyleId->text() != nullptr && *kStyleId->text() == value && kName != nullptr &&
                kName->text() != nullptr) {
                return "." + *kName->text();
            }
        }
        return {};
    }
    if (member == "text") {
        const std::string kMessage = labelMessage(*kId);
        return kMessage.empty() ? std::string{} : "\"" + kMessage + "\"";
    }
    const auto kAsset = std::ranges::find(assets_, *kId, &Asset::id);
    return kAsset != assets_.end() ? kAsset->name : std::string{};
}

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
    auto pane = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0),
                               .minHeight = ui::pixels(240),
                               .direction = ui::Direction::Column,
                               .grow = 1,
                               .shrink = 0,
                               .scroll = ui::Scroll::Both},
                    0x0D0E11FFU);
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
                                 .margin = {16, 16, 24, 8},
                                 .border = {1, 1, 1, 1}},
                      kScreenFill);
    if (!screen.has_value()) {
        return;
    }
    // As the design's UI screen (D941): the screen raised over the pane,
    // and what draws it said under it.
    static_cast<void>(tree_->setLook(*screen,
                                     ui::Look{.fill = kScreenFill,
                                              .borderColor = look::kRaisedEdge,
                                              .radius = 4,
                                              .outerShadow = {.color = 0x00000080U, .y = 6, .blur = 20}}));
    if (auto said = box(*pane, ui::Layout{.shrink = 0, .margin = {16, 16, 0, 16}}, 0); said.has_value()) {
        static_cast<void>(
            words(*said,
                  "Laid out as the game's UI tree lays it out, " + std::to_string(static_cast<int>(kScreenWidth)) +
                      " by " + std::to_string(static_cast<int>(kScreenHeight)) +
                      (held_.previewStyles.empty() ? std::string{} : ", its style sheet's classes applied"),
                  look::kFaintInk,
                  look::kSmallSize));
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
            // Its label's message where the game's strings say one (D941).
            const std::string kMessage = kNode.fields.text != 0 ? labelMessage(kNode.fields.text) : std::string{};
            static_cast<void>(words(*node,
                                    !kNode.shows.empty() ? kNode.shows
                                    : !kMessage.empty()  ? kMessage
                                                         : kNode.component,
                                    kColor,
                                    kSize));
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
