#pragma once

// The platform's clipboard for an editor (D999): text it puts there, and
// text it asks for, answered a frame or more later (the web's browser may
// ask the user first). The window host lends one as
// `rawframe.view.clipboard`; a host with no window lends none, and a read
// where the platform has no clipboard is answered unsupported.

#include "rawframe/composition/participant.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::view {

class Clipboard {
public:
    struct Read {
        /// The editor's own number for it, given back with its text.
        std::uint64_t ticket = 0;
        std::string text;
        /// Where the platform gave no text: unsupported, or refused.
        bool unsupported = false;
    };

    /// The editor's side: `text` put on the clipboard; the latest written
    /// before the host looks is the one put.
    void write(std::string text) {
        written_ = std::move(text);
    }
    /// The editor's side: the clipboard's text asked; it comes with `ticket`.
    void read(std::uint64_t ticket) {
        reads_.push_back(ticket);
    }
    /// The editor's side: the oldest read answered and not yet taken.
    [[nodiscard]] std::optional<Read> take() {
        if (answers_.empty()) {
            return std::nullopt;
        }
        Read oldest = std::move(answers_.front());
        answers_.erase(answers_.begin());
        return oldest;
    }

    /// The host's side: the text to put there since it last looked.
    [[nodiscard]] std::optional<std::string> written() {
        return std::exchange(written_, std::nullopt);
    }
    /// The host's side: the reads asked since it last looked, oldest first.
    [[nodiscard]] std::vector<std::uint64_t> reads() {
        return std::exchange(reads_, {});
    }
    /// The host's side: a read's answer.
    void answered(Read read) {
        answers_.push_back(std::move(read));
    }

private:
    std::optional<std::string> written_;
    std::vector<std::uint64_t> reads_;
    std::vector<Read> answers_;
};

inline constexpr composition::Capability<Clipboard> kClipboard{"rawframe.view.clipboard"};

} // namespace rawframe::view
