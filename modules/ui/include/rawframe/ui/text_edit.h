#pragma once

// Editing a node's text (SPEC-0030's UiTextEdit, D426): the one editing
// implementation over a tree's text, holding its caret and selection,
// applying typed text and editing keys to them, showing an input method's
// composition inline, and telling where the platform's candidate window
// belongs. Drawn as the caret and the selection's boxes over the tree's own
// list. Undo is not built.

#include "rawframe/result/result.h"
#include "rawframe/ui/tree.h"

#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::ui {

struct EditSettings {
    /// Whether line breaks may be typed; a single line drops them, with
    /// every other control character.
    bool multiline = false;
    /// The most bytes the text holds; what is typed past it is left out,
    /// whole code points at a time.
    std::uint32_t maximumBytes = 1024;
    /// The caret's and the selection's colors, 0xRRGGBBAA in sRGB.
    std::uint32_t caretColor = 0x000000FF;
    std::uint32_t selectionColor = 0x3390FF66;
};

/// The keys that edit: deleting back and forward, moving by cluster on
/// screen and by line, to a line's ends, and selecting all.
enum class EditKey : std::uint8_t {
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    SelectAll
};

/// How a key edits: `extend` (Shift) moves the caret keeping the selection's
/// other end; `word` (Control, or Option on macOS) goes by word, and Home
/// and End to the text's ends.
struct EditModifiers {
    bool extend = false;
    bool word = false;
};

class TextEdit {
public:
    /// Editing `node`, which shows text, in `tree`, which outlives it; the
    /// caret at the text's end.
    TextEdit(Tree& tree, Node node, EditSettings settings) noexcept;

    [[nodiscard]] Node node() const noexcept {
        return node_;
    }

    /// Typed or committed text in place of the selection, the caret after
    /// it; a composition there ends first.
    [[nodiscard]] result::Status type(std::string_view text);
    [[nodiscard]] result::Status press(EditKey key, EditModifiers modifiers);
    /// The caret to the point in the node's border box, the selection
    /// collapsed there unless `extend`.
    [[nodiscard]] result::Status pointAt(float x, float y, bool extend);
    /// The input method's composition at the caret, in place of the
    /// selection, `caret` bytes into it (-1 hides it); empty text cancels
    /// it.
    [[nodiscard]] result::Status
    compose(std::string_view text, std::int32_t caret, std::span<const CompositionPart> parts);

    /// The selection, `start` not after `end`; empty at the caret.
    [[nodiscard]] TextRange selection() const noexcept;
    [[nodiscard]] TextPosition caret() const noexcept {
        return caret_;
    }
    /// Where the caret is drawn, in the node's border box: the rectangle a
    /// candidate window is anchored to.
    [[nodiscard]] result::Result<Rect> caretRect() const;
    /// The selection's boxes and then the caret's appended to `into`, drawn
    /// by `tree.draw(root, ...)`, so in its space; a caret is a device pixel
    /// wide at least.
    [[nodiscard]] result::Status decorate(Node root, DrawList& into) const;

private:
    /// The selection replaced by `text` as typed, the caret after it.
    [[nodiscard]] result::Status replaceSelection(std::string_view text);
    [[nodiscard]] result::Status moveTo(TextPosition to, bool extend);

    Tree* tree_;
    Node node_;
    EditSettings settings_;
    TextPosition caret_;
    std::uint32_t anchor_ = 0;
    /// The x a run of moves up and down keeps.
    float preferredX_ = std::nanf("");
    /// The composition's caret, bytes into it; -1 hides the caret.
    std::int32_t composing_ = 0;
};

} // namespace rawframe::ui
