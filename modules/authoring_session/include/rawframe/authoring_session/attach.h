#pragma once

// Where a game Studio plays can be debugged from (D462): Studio plays each
// game in a directory of its own, named by the game, and writes there the
// tooling endpoint a debugger attaches to; `rawframe-debug`, given the game,
// reads it. Neither knows the other's ports.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::authoring_session {

/// A game's tooling endpoint granting debug, the file holding the
/// fingerprint of its certificate, and the file holding its token.
struct AttachRecord {
    std::string endpoint;
    std::string pinFile;
    std::string tokenFile;
};

/// The directory a game is played in by default, named by the digest of the
/// game's absolute path, under a directory of the user's own: the runtime
/// directory (`XDG_RUNTIME_DIR`), else the cache (`XDG_CACHE_HOME`, else
/// `~/.cache`); Windows' temporary directory, which is the user's.
[[nodiscard]] std::filesystem::path playDirectoryOf(const std::filesystem::path& game);

/// The record as the file holds it, one JSON object, and its file's name.
[[nodiscard]] std::string attachText(const AttachRecord& record);
inline constexpr std::string_view kAttachFile = "debug.attach";

/// The record a play directory holds, trusted only from a directory that is
/// no link, its user's own and no one else's to enter, as Studio makes it:
/// another user could make one first, in a shared temporary directory, and
/// name an endpoint of theirs. None otherwise, `why` saying so.
[[nodiscard]] std::optional<AttachRecord> attachIn(const std::filesystem::path& directory, std::string& why);

} // namespace rawframe::authoring_session
