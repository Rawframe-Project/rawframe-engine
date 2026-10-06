#include "rawframe/view/typing.h"

#include <vector>

namespace rawframe::view {

namespace {

ui::EditKey editKeyOf(TypingKey key) noexcept {
    switch (key) {
    case TypingKey::Backspace:
        return ui::EditKey::Backspace;
    case TypingKey::Delete:
        return ui::EditKey::Delete;
    case TypingKey::Left:
        return ui::EditKey::Left;
    case TypingKey::Right:
        return ui::EditKey::Right;
    case TypingKey::Up:
        return ui::EditKey::Up;
    case TypingKey::Down:
        return ui::EditKey::Down;
    case TypingKey::Home:
        return ui::EditKey::Home;
    case TypingKey::End:
        return ui::EditKey::End;
    default:
        return ui::EditKey::SelectAll;
    }
}

} // namespace

std::optional<TypingKey> edit(ui::TextEdit& edit, const Typing& typing) {
    switch (typing.kind) {
    case Typing::Kind::Text:
        static_cast<void>(edit.type(typing.text));
        return std::nullopt;
    case Typing::Kind::Composition: {
        std::vector<ui::CompositionPart> parts;
        for (const TypingSpan& kSpan : typing.spans) {
            parts.push_back(ui::CompositionPart{.start = kSpan.start,
                                                .length = kSpan.length,
                                                .style = static_cast<ui::CompositionPart::Style>(kSpan.style)});
        }
        static_cast<void>(edit.compose(typing.text, typing.caret, parts));
        return std::nullopt;
    }
    case Typing::Kind::Key:
        break;
    }
    if (typing.key == TypingKey::Submit || typing.key == TypingKey::Dismiss || typing.key == TypingKey::Next) {
        return typing.key;
    }
    static_cast<void>(edit.press(editKeyOf(typing.key), {.extend = typing.extend, .word = typing.word}));
    return std::nullopt;
}

} // namespace rawframe::view
