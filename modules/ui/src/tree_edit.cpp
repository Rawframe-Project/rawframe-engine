// Editing a node's text (D426) over Maul UI's editing primitives (its record
// mui-0006): positions, carets, movement, deletions, and an input method's
// composition, in the node's border box. Selection and input are
// ui::TextEdit's.

#include "rawframe/ui/tree.h"
#include "tree_state.h"

#include <cmath>
#include <maul-ui/text_edit.h>

namespace rawframe::ui {

namespace {

muiTextPosition positionOf(TextPosition position) noexcept {
    return muiTextPosition{
        .offset = position.offset,
        .affinity = static_cast<muiTextAffinity>(position.upstream ? mui_affinityUpstream : mui_affinityDownstream)};
}

TextPosition positionOf(muiTextPosition position) noexcept {
    return TextPosition{.offset = position.offset, .upstream = position.affinity == mui_affinityUpstream};
}

/// Where a node's content box sits in its border box, and how wide it is,
/// as painting is given it.
struct Content {
    float x = 0;
    float y = 0;
    float width = 0;
};

} // namespace

/// The text `node` shows and its content box; refused for none.
static result::Result<std::pair<const Text*, Content>> editable(const Tree::State& state, Node node) {
    const muiNodeId kNode = idOf(node);
    const Text* kText = state.textOf(kNode);
    if (kText == nullptr || !kText->editable || !muiNode_IsValid(state.context, kNode)) {
        return refuse(UiError::Stale, "a node's text is edited where the node was added editable");
    }
    muiLayoutStyle style{};
    RAWFRAME_TRY(checked(muiNode_GetLayoutStyle(state.context, kNode, &style), "a UI node's layout could not be read"));
    const muiRect kRect = muiNode_GetRect(state.context, kNode);
    const float kLeft = style.border.start + style.padding.start;
    const float kRight = style.border.end + style.padding.end;
    return std::pair{kText,
                     Content{.x = kLeft,
                             .y = style.border.top + style.padding.top,
                             .width = std::max(0.0F, kRect.width - kLeft - kRight)}};
}

std::string_view Tree::textOf(Node node) const noexcept {
    const Text* kText = state_->textOf(idOf(node));
    const char* bytes = nullptr;
    std::size_t length = 0;
    if (kText == nullptr || muiTextBlock_GetText(state_->text, kText->block, &bytes, &length) != mui_success) {
        return {};
    }
    return {bytes, length};
}

result::Result<TextPosition> Tree::textAt(Node node, float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return refuse(UiError::Invalid, "a point in a node's text is finite");
    }
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    muiTextPosition found{};
    RAWFRAME_TRY(checked(
        muiTextHitTest(
            &state_->host, idOf(node), kEditable.second.width, x - kEditable.second.x, y - kEditable.second.y, &found),
        "a point in a node's text could not be found"));
    return positionOf(found);
}

result::Result<Rect> Tree::caretOf(Node node, TextPosition position) const {
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    muiTextCaret caret{};
    RAWFRAME_TRY(
        checked(muiTextGetCaret(&state_->host, idOf(node), kEditable.second.width, positionOf(position), &caret),
                "a caret in a node's text could not be found"));
    return Rect{
        .x = kEditable.second.x + caret.x, .y = kEditable.second.y + caret.y, .width = 0, .height = caret.height};
}

result::Result<TextPosition> Tree::moved(Node node, TextPosition from, TextMove move, float preferredX) const {
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    // Kept in the content box, as Maul UI keeps it.
    const float kPreferred = std::isnan(preferredX) ? preferredX : preferredX - kEditable.second.x;
    muiTextPosition to{};
    RAWFRAME_TRY(checked(muiTextMove(&state_->host,
                                     idOf(node),
                                     kEditable.second.width,
                                     positionOf(from),
                                     static_cast<muiTextMovement>(move),
                                     kPreferred,
                                     &to),
                         "a position in a node's text could not be moved"));
    return positionOf(to);
}

result::Result<std::vector<Rect>> Tree::rangeRects(Node node, TextRange range) const {
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    std::uint32_t count = 0;
    std::vector<muiRect> found(4);
    muiResult outcome = muiTextGetRangeRects(&state_->host,
                                             idOf(node),
                                             kEditable.second.width,
                                             range.start,
                                             range.end,
                                             found.data(),
                                             static_cast<std::uint32_t>(found.size()),
                                             &count);
    if (outcome == mui_errorCapacity && count > found.size()) {
        found.resize(count);
        outcome = muiTextGetRangeRects(
            &state_->host, idOf(node), kEditable.second.width, range.start, range.end, found.data(), count, &count);
    }
    RAWFRAME_TRY(checked(outcome, "the rectangles of a node's text could not be found"));
    std::vector<Rect> rects;
    for (std::uint32_t at = 0; at < count; ++at) {
        rects.push_back(Rect{.x = kEditable.second.x + found[at].x,
                             .y = kEditable.second.y + found[at].y,
                             .width = found[at].width,
                             .height = found[at].height});
    }
    return rects;
}

result::Result<TextRange> Tree::deletion(Node node, std::uint32_t offset, bool forward) const {
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    TextRange range;
    RAWFRAME_TRY(checked(muiTextBlock_FindDeletion(state_->text,
                                                   kEditable.first->block,
                                                   offset,
                                                   forward ? mui_deleteForward : mui_deleteBackward,
                                                   &range.start,
                                                   &range.end),
                         "a deletion in a node's text could not be found"));
    return range;
}

result::Status Tree::replaceText(Node node, TextRange range, std::string_view text) {
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    RAWFRAME_TRY(checked(
        muiTextBlock_Replace(state_->text, kEditable.first->block, range.start, range.end, text.data(), text.size()),
        "a node's text could not be replaced"));
    return state_->setContent(idOf(node), mui_contentHost);
}

result::Status
Tree::setComposition(Node node, std::uint32_t offset, std::string_view text, std::span<const CompositionPart> parts) {
    if (parts.size() > kMaximumCompositionParts) {
        return refuse(UiError::Invalid, "a composition has at most 32 parts");
    }
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    std::array<muiCompositionSegment, kMaximumCompositionParts> segments{};
    for (std::size_t at = 0; at < parts.size(); ++at) {
        segments[at] = muiCompositionSegment{.start = parts[at].start,
                                             .length = parts[at].length,
                                             .style = static_cast<muiCompositionStyle>(parts[at].style)};
    }
    RAWFRAME_TRY(checked(muiTextBlock_SetComposition(state_->text,
                                                     kEditable.first->block,
                                                     offset,
                                                     text.data(),
                                                     text.size(),
                                                     segments.data(),
                                                     static_cast<std::uint32_t>(parts.size())),
                         "a node's composition was refused"));
    return state_->setContent(idOf(node), mui_contentHost);
}

result::Status Tree::endComposition(Node node) {
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    RAWFRAME_TRY(checked(muiTextBlock_EndComposition(state_->text, kEditable.first->block),
                         "a node's composition could not be ended"));
    return state_->setContent(idOf(node), mui_contentHost);
}

result::Result<TextRange> Tree::composition(Node node) const {
    RAWFRAME_TRY_ASSIGN(const auto kEditable, editable(*state_, node));
    std::uint32_t start = 0;
    std::uint32_t length = 0;
    RAWFRAME_TRY(checked(muiTextBlock_GetComposition(state_->text, kEditable.first->block, &start, &length),
                         "a node's composition could not be read"));
    return length == 0 ? TextRange{} : TextRange{.start = start, .end = start + length};
}

result::Result<Rect> Tree::placeOf(Node root, Node node) const {
    const muiNodeId kRoot = idOf(root);
    muiNodeId at = idOf(node);
    if (!muiNode_IsValid(state_->context, at) || !muiNode_IsValid(state_->context, kRoot)) {
        return refuse(UiError::Stale, "a node is placed in a tree it is in");
    }
    const muiRect kRect = muiNode_GetRect(state_->context, at);
    Rect placed{.x = kRect.x, .y = kRect.y, .width = kRect.width, .height = kRect.height};
    // Each rectangle is its parent's; the root's at its own, as points hit it.
    while (at.index1 != kRoot.index1 || at.generation != kRoot.generation) {
        at = muiNode_GetParent(state_->context, at);
        if (at.index1 == 0) {
            return refuse(UiError::Invalid, "a node is placed in a subtree it is in");
        }
        const muiRect kParent = muiNode_GetRect(state_->context, at);
        placed.x += kParent.x;
        placed.y += kParent.y;
    }
    return placed;
}

} // namespace rawframe::ui
