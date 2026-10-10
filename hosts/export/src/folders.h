#pragma once

// What an export writes into its folder (D396): the configurations and
// programs of a native folder, a web site, and a phone's package, the
// library's active Composition, and the iOS application made from the
// client's bundle (D587). main.cpp runs the tools that make the Build.

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::export_tool {

namespace fs = std::filesystem;

/// A file's bytes, or none where it cannot be read.
std::optional<std::string> readText(const fs::path& path);

/// Writes `text` as the whole of a file; false where it cannot.
bool writeText(const fs::path& path, std::string_view text);

/// What every export writes its configurations from.
struct Exported {
    fs::path output;
    /// The Composition's record, beside the library it names (the web's).
    std::string record;
    /// The game's subject, `<publisher>/<name>`.
    std::string subject;
    /// The origin and channel a native export follows, if any.
    std::optional<std::string> follow;
    std::string channel;
    std::string gameResource;
    std::string port;
    /// The client window's title.
    std::string title;
    /// The programs' suffix on this system (`.exe` on Windows).
    std::string suffix;
};

/// Keeps the Composition record at `record` in `library`, under its digest,
/// names it the active one in a fresh installed pointer, and removes it
/// from where the build tool wrote it; the files are added to `written`,
/// by their paths under `output`.
bool installRecord(const fs::path& output,
                   const fs::path& library,
                   const fs::path& record,
                   std::vector<std::string>& written);

/// The folder that plays on this machine: the server, the client, the
/// launcher, and their configurations.
bool writeNative(const Exported& exported, const std::array<fs::path, 4>& programs, std::vector<std::string>& written);

/// What a web export takes from the web build and the native one.
struct WebFiles {
    fs::path client;
    fs::path window;
    fs::path device;
    /// The page's own files: `play.html` and its modules.
    fs::path page;
    fs::path server;
};

/// The site under `web/` and its server under `server/` (D397).
bool writeWeb(const Exported& exported, const WebFiles& from, std::vector<std::string>& written);

/// A phone's platform, as an export packs a game for it.
struct Mobile {
    /// The folder's directory for it, holding `game/`.
    std::string directory;
    /// What a player installs, as the server's configuration names it.
    std::string what;
    /// The client's configuration's first words.
    std::string player;
};

/// What a phone's package carries under `<directory>/game/`, and the server
/// it plays on under `server/`, pinned to an identity made here (D555,
/// D587).
bool writeMobile(const Exported& exported,
                 const Mobile& mobile,
                 const std::string& address,
                 const fs::path& server,
                 std::vector<std::string>& written);

/// Whether `identifier` is an application's bundle identifier: letters,
/// digits, dashes, and dots.
bool bundleIdentifierLike(std::string_view identifier);

/// The iOS client's bundle copied to `application` under the output with
/// the game's `ios/game/` in it as `game/`, its Info.plist naming it
/// `identifier` and `title` in place of the client's own (D587).
bool writeApplication(const Exported& exported,
                      const fs::path& client,
                      const std::string& application,
                      const std::string& identifier,
                      std::vector<std::string>& written);

} // namespace rawframe::export_tool
