#pragma once

// A session's style sheets (D636): the `.styles` documents under its root,
// opened as records name them and kept open with their histories as its
// scenes are, reopened when their file changes under it, and written by
// rename when a change is kept. A record names one where it would name a
// scene: apply runs `styles.set_part`, read `styles.read_sheet`, and undo
// and redo step its history. A played game follows the file (D634).

#include "rawframe/authoring/authored_styles.h"
#include "rawframe/authoring/session.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace rawframe::authoring_session {

class Styling {
public:
    explicit Styling(std::filesystem::path root) : root_(std::move(root)) {
    }

    /// Whether a record's document is a style sheet.
    [[nodiscard]] static bool names(std::string_view document) noexcept;

    /// The outcome of an apply, a step, or a read; `failed` set where an
    /// operation in it failed, as the session's own are.
    [[nodiscard]] result::Result<document::Value> applied(const authoring::SessionRecord& record, bool& failed);
    [[nodiscard]] result::Result<document::Value>
    stepped(const authoring::SessionRecord& record, bool undo, bool& failed);
    [[nodiscard]] result::Result<document::Value> answered(const authoring::SessionRecord& record, bool& failed);

private:
    struct Open {
        std::unique_ptr<authoring::AuthoredStyles> document;
        /// The digest of the bytes on disk as last read or written.
        std::string onDisk;
    };

    [[nodiscard]] result::Result<Open*> open(const std::string& name, bool& reopened);
    [[nodiscard]] result::Result<bool> save(const std::string& name, Open& open);

    std::filesystem::path root_;
    std::map<std::string, Open> sheets_;
};

} // namespace rawframe::authoring_session
