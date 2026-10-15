#pragma once

// A scene's bookmarks (D796): up to nine views kept in the scene's editor
// state, a text file beside it (`<scene>.editor`) that the cook passes by,
// having no sidecar, and that a version control system keeps like any
// other document. Editor state, never the scene's data (ADR-0066).

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace rawframe::authoring_session {

struct Bookmark {
    /// One to nine.
    std::uint8_t slot = 0;
    authoring::SceneView view;
};

/// The editor state's file of the scene at `scene`.
[[nodiscard]] std::filesystem::path editorStateOf(const std::filesystem::path& scene);

/// The bookmarks the editor state at `path` keeps, in slot order: none
/// where there is no such file; refused (`validation_failed`) where it
/// does not read as one.
[[nodiscard]] result::Result<std::vector<Bookmark>> readBookmarks(const std::filesystem::path& path);

/// `bookmarks` written to the editor state at `path`, by rename, every
/// other member it holds kept.
[[nodiscard]] result::Status writeBookmarks(const std::filesystem::path& path, const std::vector<Bookmark>& bookmarks);

/// `bookmarks` as a reply gives them: each slot with its view.
[[nodiscard]] document::Value bookmarksValue(const std::vector<Bookmark>& bookmarks);

} // namespace rawframe::authoring_session
