#include "rawframe/ui/text_edit.h"

#include <algorithm>
#include <array>
#include <string>

namespace rawframe::ui {

namespace {

/// 0xRRGGBBAA in sRGB as a box's linear premultiplied fill.
std::array<float, 4> fillOf(std::uint32_t color) noexcept {
    const auto kLinear = [](std::uint32_t channel) {
        const float kValue = static_cast<float>(channel & 0xFFU) / 255.0F;
        return kValue <= 0.04045F ? kValue / 12.92F : std::pow((kValue + 0.055F) / 1.055F, 2.4F);
    };
    const float kAlpha = static_cast<float>(color & 0xFFU) / 255.0F;
    return {kLinear(color >> 24U) * kAlpha, kLinear(color >> 16U) * kAlpha, kLinear(color >> 8U) * kAlpha, kAlpha};
}

/// The bytes of `text` that may be typed: control characters left out, but
/// for line breaks and tabs where the text has lines.
std::string typeable(std::string_view text, bool multiline) {
    std::string kept;
    kept.reserve(text.size());
    for (const char kCharacter : text) {
        const auto kByte = static_cast<unsigned char>(kCharacter);
        if ((kByte < 0x20 && !(multiline && (kByte == '\n' || kByte == '\t'))) || kByte == 0x7F) {
            continue;
        }
        kept.push_back(kCharacter);
    }
    return kept;
}

/// The longest start of `text` of at most `room` bytes that ends on a code
/// point.
std::string_view fitted(std::string_view text, std::size_t room) {
    if (text.size() <= room) {
        return text;
    }
    std::size_t end = room;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    return text.substr(0, end);
}

} // namespace

TextEdit::TextEdit(Tree& tree, Node node, EditSettings settings) noexcept
    : tree_(&tree), node_(node), settings_(settings) {
    const auto kEnd = static_cast<std::uint32_t>(tree.textOf(node).size());
    caret_ = TextPosition{.offset = kEnd};
    anchor_ = kEnd;
}

TextRange TextEdit::selection() const noexcept {
    return TextRange{.start = std::min(anchor_, caret_.offset), .end = std::max(anchor_, caret_.offset)};
}

result::Status TextEdit::replaceSelection(std::string_view text) {
    RAWFRAME_TRY_ASSIGN(const TextRange kComposed, tree_->composition(node_));
    if (kComposed.end > kComposed.start) {
        RAWFRAME_TRY(tree_->setComposition(node_, 0, {}, {}));
    }
    const TextRange kSelected = selection();
    const std::string kTyped = typeable(text, settings_.multiline);
    const std::size_t kHeld = tree_->textOf(node_).size() - (kSelected.end - kSelected.start);
    const std::string_view kFits =
        fitted(kTyped, settings_.maximumBytes > kHeld ? settings_.maximumBytes - kHeld : std::size_t{0});
    RAWFRAME_TRY(tree_->replaceText(node_, kSelected, kFits));
    caret_ = TextPosition{.offset = kSelected.start + static_cast<std::uint32_t>(kFits.size())};
    anchor_ = caret_.offset;
    preferredX_ = std::nanf("");
    return {};
}

result::Status TextEdit::type(std::string_view text) {
    return replaceSelection(text);
}

result::Status TextEdit::moveTo(TextPosition to, bool extend) {
    caret_ = to;
    if (!extend) {
        anchor_ = to.offset;
    }
    return {};
}

result::Status TextEdit::press(EditKey key, EditModifiers modifiers) {
    const TextRange kSelected = selection();
    const bool kVertical = key == EditKey::Up || key == EditKey::Down;
    if (!kVertical) {
        preferredX_ = std::nanf("");
    }
    switch (key) {
    case EditKey::Backspace:
    case EditKey::Delete: {
        if (kSelected.end > kSelected.start) {
            return replaceSelection({});
        }
        const bool kForward = key == EditKey::Delete;
        TextRange removed;
        if (modifiers.word) {
            RAWFRAME_TRY_ASSIGN(
                const TextPosition kTo,
                tree_->moved(
                    node_, caret_, kForward ? TextMove::NextWordEnd : TextMove::PreviousWordStart, std::nanf("")));
            removed =
                TextRange{.start = std::min(kTo.offset, caret_.offset), .end = std::max(kTo.offset, caret_.offset)};
        } else {
            RAWFRAME_TRY_ASSIGN(removed, tree_->deletion(node_, caret_.offset, kForward));
        }
        anchor_ = removed.start;
        caret_ = TextPosition{.offset = removed.end};
        return replaceSelection({});
    }
    case EditKey::Left:
    case EditKey::Right: {
        const bool kRight = key == EditKey::Right;
        // A selection collapses to the end the key points at.
        if (kSelected.end > kSelected.start && !modifiers.extend && !modifiers.word) {
            return moveTo(TextPosition{.offset = kRight ? kSelected.end : kSelected.start}, false);
        }
        const TextMove kMove = modifiers.word ? (kRight ? TextMove::NextWordEnd : TextMove::PreviousWordStart)
                                              : (kRight ? TextMove::Right : TextMove::Left);
        RAWFRAME_TRY_ASSIGN(const TextPosition kTo, tree_->moved(node_, caret_, kMove, std::nanf("")));
        return moveTo(kTo, modifiers.extend);
    }
    case EditKey::Up:
    case EditKey::Down: {
        if (std::isnan(preferredX_)) {
            RAWFRAME_TRY_ASSIGN(const Rect kCaret, tree_->caretOf(node_, caret_));
            preferredX_ = kCaret.x;
        }
        RAWFRAME_TRY_ASSIGN(
            const TextPosition kTo,
            tree_->moved(node_, caret_, key == EditKey::Up ? TextMove::LineUp : TextMove::LineDown, preferredX_));
        return moveTo(kTo, modifiers.extend);
    }
    case EditKey::Home:
    case EditKey::End: {
        const bool kEnd = key == EditKey::End;
        const TextMove kMove = modifiers.word ? (kEnd ? TextMove::TextEnd : TextMove::TextStart)
                                              : (kEnd ? TextMove::LineEnd : TextMove::LineStart);
        RAWFRAME_TRY_ASSIGN(const TextPosition kTo, tree_->moved(node_, caret_, kMove, std::nanf("")));
        return moveTo(kTo, modifiers.extend);
    }
    case EditKey::SelectAll:
        anchor_ = 0;
        caret_ = TextPosition{.offset = static_cast<std::uint32_t>(tree_->textOf(node_).size())};
        return {};
    }
    return {};
}

result::Status TextEdit::pointAt(float x, float y, bool extend) {
    preferredX_ = std::nanf("");
    RAWFRAME_TRY_ASSIGN(const TextPosition kTo, tree_->textAt(node_, x, y));
    return moveTo(kTo, extend);
}

result::Status TextEdit::compose(std::string_view text, std::int32_t caret, std::span<const CompositionPart> parts) {
    RAWFRAME_TRY_ASSIGN(const TextRange kComposed, tree_->composition(node_));
    if (kComposed.end == kComposed.start && !text.empty()) {
        // A composition starts in place of the selection.
        const TextRange kSelected = selection();
        if (kSelected.end > kSelected.start) {
            RAWFRAME_TRY(replaceSelection({}));
        }
    }
    // A composition stays within the text's room as typed text does, and
    // parts past what fits are drawn as none.
    const std::size_t kHeld = tree_->textOf(node_).size() - (kComposed.end - kComposed.start);
    const std::string_view kFits =
        fitted(text, settings_.maximumBytes > kHeld ? settings_.maximumBytes - kHeld : std::size_t{0});
    const bool kPartsFit = std::ranges::all_of(parts, [&kFits](const CompositionPart& part) {
        return std::size_t{part.start} + part.length <= kFits.size();
    });
    RAWFRAME_TRY(
        tree_->setComposition(node_, caret_.offset, kFits, kPartsFit ? parts : std::span<const CompositionPart>{}));
    // The caret stays at the composition's start; it is drawn inside it.
    composing_ = std::clamp<std::int32_t>(caret, -1, static_cast<std::int32_t>(kFits.size()));
    preferredX_ = std::nanf("");
    return {};
}

result::Result<Rect> TextEdit::caretRect() const {
    RAWFRAME_TRY_ASSIGN(const TextRange kComposed, tree_->composition(node_));
    if (kComposed.end > kComposed.start && composing_ >= 0) {
        return tree_->caretOf(node_, TextPosition{.offset = kComposed.start + static_cast<std::uint32_t>(composing_)});
    }
    return tree_->caretOf(node_, caret_);
}

result::Status TextEdit::decorate(Node root, DrawList& into) const {
    RAWFRAME_TRY_ASSIGN(const Rect kPlace, tree_->placeOf(root, node_));
    RAWFRAME_TRY_ASSIGN(const TextRange kComposed, tree_->composition(node_));
    const bool kComposing = kComposed.end > kComposed.start;
    const auto kAdd = [&into, &kPlace](Rect rect, std::uint32_t color) {
        rect.x += kPlace.x;
        rect.y += kPlace.y;
        into.commands.push_back(
            DrawCommand{.kind = DrawCommand::Kind::Box, .index = static_cast<std::uint32_t>(into.boxes.size())});
        into.boxes.push_back(Box{.rect = rect, .fill = fillOf(color)});
    };
    const TextRange kSelected = selection();
    if (!kComposing && kSelected.end > kSelected.start) {
        RAWFRAME_TRY_ASSIGN(const std::vector<Rect> kRects, tree_->rangeRects(node_, kSelected));
        for (const Rect& kRect : kRects) {
            kAdd(kRect, settings_.selectionColor);
        }
    }
    if (kComposing && composing_ < 0) {
        return {};
    }
    RAWFRAME_TRY_ASSIGN(Rect caret, caretRect());
    caret.width = std::max(1.0F, 1.0F / std::max(into.scale, 1.0F));
    kAdd(caret, settings_.caretColor);
    return {};
}

} // namespace rawframe::ui
