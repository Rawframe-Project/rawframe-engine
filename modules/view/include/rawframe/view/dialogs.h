#pragma once

// The platform's file dialogs for an editor (D918): what it asks, shown by
// the window host over its window, and what the user chose, answered a
// frame or more later while the window goes on drawing. The window host
// lends one as `rawframe.view.file_dialogs`; a host with no window lends
// none, and a dialog asked where there are no paths (the web) is answered
// unsupported.

#include "rawframe/composition/participant.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::view {

class FileDialogs {
public:
    enum class Kind : std::uint8_t {
        Open,
        OpenMany,
        Save,
        Folder
    };
    struct Asked {
        /// The editor's own number for it, given back with its answer.
        std::uint64_t ticket = 0;
        Kind kind = Kind::Open;
        std::string title;
        /// The absolute folder it opens in, empty for the platform's.
        std::string folder;
        /// The kinds of file offered: a name and its extensions without
        /// dots joined by ';'.
        std::vector<std::pair<std::string, std::string>> filters;
    };
    struct Answer {
        std::uint64_t ticket = 0;
        /// The absolute paths chosen; none when the user closed it.
        std::vector<std::string> paths;
        /// Where the platform shows no dialog.
        bool unsupported = false;
    };

    /// The editor's side: a dialog asked; its answer comes with `ticket`.
    void ask(Asked asked) {
        asked_.push_back(std::move(asked));
    }
    /// The editor's side: the oldest answer not yet taken.
    [[nodiscard]] std::optional<Answer> take() {
        if (answers_.empty()) {
            return std::nullopt;
        }
        Answer oldest = std::move(answers_.front());
        answers_.erase(answers_.begin());
        return oldest;
    }

    /// The host's side: what was asked since it last looked, oldest first.
    [[nodiscard]] std::vector<Asked> asked() {
        return std::exchange(asked_, {});
    }
    /// The host's side: a dialog's answer.
    void answered(Answer answer) {
        answers_.push_back(std::move(answer));
    }

private:
    std::vector<Asked> asked_;
    std::vector<Answer> answers_;
};

inline constexpr composition::Capability<FileDialogs> kFileDialogs{"rawframe.view.file_dialogs"};

} // namespace rawframe::view
