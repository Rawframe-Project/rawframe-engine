#pragma once

// What a client's UI takes of its local player's keyboard and input method
// (SPEC-0030's text edit and IME contract, SPEC-0029's text-edit gate,
// D426). A client host lends one as `rawframe.view.ui_typing`: the UI says
// while a text field holds focus and where its caret is, the host then
// hands it what is typed, the editing keys, and the input method's
// composition, and asks the platform for text input with the caret as the
// candidate window's anchor; each player's input gates its keyboard
// actions meanwhile.

#include "rawframe/composition/participant.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::view {

/// The keys a field takes while it holds focus: those that edit, Enter
/// (`Submit`: a line break in a field of lines, else the field's text
/// given), and Escape (`Dismiss`: focus let go).
enum class TypingKey : std::uint8_t {
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    SelectAll,
    Submit,
    Dismiss
};

/// A styled part of a composition, in bytes of its text: plain, still to
/// convert, the part a conversion works on, or converted.
struct TypingSpan {
    enum class Style : std::uint8_t {
        Plain,
        Underline,
        Target,
        Converted
    };
    std::uint32_t start = 0;
    std::uint32_t length = 0;
    Style style = Style::Underline;
};

/// One thing typed: text typed or committed, a key with Shift (`extend`)
/// and Control or Option (`word`), or the input method's composition, its
/// caret bytes into it (-1 hidden), empty text ending it.
struct Typing {
    enum class Kind : std::uint8_t {
        Text,
        Key,
        Composition
    };
    Kind kind = Kind::Text;
    std::string text;
    TypingKey key = TypingKey::Backspace;
    bool extend = false;
    bool word = false;
    std::int32_t caret = -1;
    std::vector<TypingSpan> spans;
};

/// A field's text given by Enter: the field's press code and its text.
struct Submitted {
    std::int64_t press = 0;
    std::string text;
};

class UiTyping {
public:
    /// Submissions held for the input to take at most; past them the
    /// oldest goes.
    static constexpr std::size_t kMostSubmitted = 16;

    /// The caret's rectangle, logical pixels of the window: x, y, width,
    /// height.
    using Caret = std::array<float, 4>;
    using Take = std::function<void(const Typing& typing)>;

    UiTyping() = default;
    UiTyping(const UiTyping&) = delete;
    UiTyping& operator=(const UiTyping&) = delete;

    /// The UI's side: what takes typing from now on; empty, as the UI goes,
    /// for nothing.
    void answer(Take take) noexcept {
        take_ = std::move(take);
        if (!take_) {
            caret_.reset();
        }
    }
    /// A field holds focus with its caret there, or none does.
    void focus(std::optional<Caret> caret) noexcept {
        caret_ = caret;
    }

    /// The host's and the input's side: whether a field holds focus, and
    /// where its caret is.
    [[nodiscard]] bool editing() const noexcept {
        return caret_.has_value() && take_;
    }
    [[nodiscard]] const std::optional<Caret>& caret() const noexcept {
        return caret_;
    }
    /// The host's side: `typing` to the field that holds focus; nothing
    /// while none does.
    void type(const Typing& typing) const {
        if (editing()) {
            take_(typing);
        }
    }

    /// The UI's side: a field's text given.
    void submit(Submitted submitted) {
        if (submitted_.size() >= kMostSubmitted) {
            submitted_.erase(submitted_.begin());
        }
        submitted_.push_back(std::move(submitted));
    }
    /// The input's side: the oldest text given and not yet taken.
    [[nodiscard]] std::optional<Submitted> takeSubmitted() {
        if (submitted_.empty()) {
            return std::nullopt;
        }
        Submitted oldest = std::move(submitted_.front());
        submitted_.erase(submitted_.begin());
        return oldest;
    }

private:
    Take take_;
    std::vector<Submitted> submitted_;
    std::optional<Caret> caret_;
};

inline constexpr composition::Capability<UiTyping> kUiTyping{"rawframe.view.ui_typing"};

} // namespace rawframe::view
