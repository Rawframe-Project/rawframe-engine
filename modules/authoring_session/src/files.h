#pragma once

// A game's files moved, removed, and its folders made, under its directory
// (D929): what an editor's asset browser does to them, as the session's
// verbs, so a tool or an agent does the same without Studio. A source
// moves with its sidecar, which keeps its resource identity, so what the
// cook made of it is still found by what uses it; the game's description
// lines that name it by its path are renamed with it. What else names the
// old path in its text is said, never rewritten.

#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

/// What a move or a removal did.
struct FilesChanged {
    /// The path asked for, and where it went (empty for a removal).
    std::string from;
    std::string to;
    /// Each file moved or removed, sidecars too, by its path under the
    /// game's directory as it was.
    std::vector<std::string> files;
    /// The game description's lines renamed or removed with it.
    std::size_t lines = 0;
    /// The game's other text files whose text names the old path, by their
    /// paths under its directory.
    std::vector<std::string> named;
};

/// The answer a move or a removal gives: `authoring.moved` or
/// `authoring.removed` with what it did.
[[nodiscard]] document::Value filesChangedOf(const FilesChanged& changed, bool removed);

/// The folders under the game's directory, by their paths under it, in
/// order: every one but those whose name begins with a dot.
[[nodiscard]] std::vector<std::string> foldersBeside(const std::filesystem::path& game);

/// Moves the file or folder `from` under the game `game` describes to `to`,
/// a file with its sidecar. Refuses a path that leaves the directory, the
/// game's description and its Kest project, a target that is taken, a
/// folder into itself, and a glTF to another folder than its buffers'.
[[nodiscard]] result::Result<FilesChanged>
moveFile(const std::filesystem::path& game, std::string_view from, std::string_view to);

/// Removes the file or folder `path` under the game `game` describes, a
/// file with its sidecar, a folder with all it holds, and the description's
/// lines naming any of them. Refuses what `moveFile` does.
[[nodiscard]] result::Result<FilesChanged> removeFile(const std::filesystem::path& game, std::string_view path);

/// Makes the folder `folder` under the game's directory, and those it is
/// in; refused where it leaves the directory or is there already.
[[nodiscard]] result::Result<document::Value> makeFolder(const std::filesystem::path& game, std::string_view folder);

} // namespace rawframe::authoring_session
