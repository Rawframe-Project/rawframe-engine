#pragma once

// Files dropped on a window from outside (D802), as its host gathers them:
// where, in the window's logical pixels, and their paths, which are inert
// text that grant nothing. An editor takes them; a game's window gathers
// them too and nothing takes them, the oldest let go past a few.

#include "rawframe/composition/participant.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::view {

class FilesDropped {
public:
    struct Drop {
        float x = 0;
        float y = 0;
        std::vector<std::string> paths;
        /// Some of the drop's files were left out for the window's limit.
        bool truncated = false;
    };

    /// The drops held at most until taken.
    static constexpr std::size_t kMostHeld = 8;

    /// The host's side: a drop on the window.
    void dropped(Drop drop) {
        if (held_.size() >= kMostHeld) {
            held_.erase(held_.begin());
        }
        held_.push_back(std::move(drop));
    }

    /// The editor's side: the oldest drop not yet taken.
    [[nodiscard]] std::optional<Drop> take() {
        if (held_.empty()) {
            return std::nullopt;
        }
        Drop oldest = std::move(held_.front());
        held_.erase(held_.begin());
        return oldest;
    }

private:
    std::vector<Drop> held_;
};

inline constexpr composition::Capability<FilesDropped> kFilesDropped{"rawframe.view.files_dropped"};

} // namespace rawframe::view
