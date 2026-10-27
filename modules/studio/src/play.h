#pragma once

// A game Studio plays to preview it: a dedicated server for the
// game, and a client whose player joins it from a window of its own and
// serves a tooling endpoint with the view grant, on this machine. Studio
// starts them, attaches the client's endpoint as the chosen scene's
// preview once it says who it is, and stops them; a process still running
// when Studio goes is killed. Nothing here waits.

#include "rawframe/process/child.h"
#include "rawframe/result/result.h"
#include "records.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

struct PlaySettings {
    /// The dedicated server's and the client's programs.
    std::filesystem::path server;
    std::filesystem::path client;
    /// The game description both read.
    std::filesystem::path game;
    /// Settings each is given beside Studio's own (a game's content root,
    /// for one); empty for none.
    std::filesystem::path serverSettings;
    std::filesystem::path clientSettings;
    /// Where their settings, logs, token, and identities go, made afresh.
    std::filesystem::path directory;
    /// The game's content as Studio cooked it: each program's
    /// `content.root` where its settings name none, once it holds a
    /// manifest.
    std::filesystem::path content;
    /// The viewport's size in pixels, to play in it: the client's
    /// window kept hidden, its pictures drawn at this size and streamed to
    /// `picture()`; none to play in the client's own window.
    std::optional<std::array<std::uint32_t, 2>> viewport;
    /// The players, each a client of its own: the first as above,
    /// each other in a window of its own beside it, its log
    /// `client-<n>.log`; one to four.
    std::uint32_t players = 1;
    /// The link conditions each client is given, settings lines, empty for
    /// none.
    std::string link;
    /// The device the first client is tried as: its window's size,
    /// the mouse played as a finger; none for this machine as it is.
    std::optional<std::array<std::uint32_t, 2>> device;
    /// Played to be edited: the server's World edited, so it shows
    /// the documents and nothing plays, and follows them often.
    bool edited = false;
};

/// Makes `path` a directory of this user's alone, or refuses one that is
/// another's or a link: where Studio plays a game and cooks it.
[[nodiscard]] result::Status privateDirectory(const std::filesystem::path& path);

/// Settings: `given` as they are, then each of `defaults` the given do not
/// set, then each of `owned`, which are Studio's to say: a given setting
/// of one is refused by the program as a key twice, as it should be.
[[nodiscard]] std::string settingsOf(const std::string& given,
                                     std::initializer_list<std::pair<std::string_view, std::string>> defaults,
                                     std::initializer_list<std::pair<std::string_view, std::string>> owned);

class Play {
public:
    /// Writes the server's and the client's settings and a token, and
    /// starts the server on a port of the dynamic range, the client's
    /// endpoint to be on the next.
    [[nodiscard]] static result::Result<Play> start(const PlaySettings& settings);
    /// Starts the client once the server has said who it is, which the
    /// client pins; nothing before, or once started. Asked each frame.
    [[nodiscard]] result::Status advance();

    /// The client's endpoint as a preview, once it has said who it is and
    /// its player is in, when its endpoint answers at once; none before.
    /// Its log is read from where it was last read.
    [[nodiscard]] std::optional<Preview> preview();
    /// Whether the server and every client still run.
    [[nodiscard]] bool running() noexcept;
    /// Whether the server and every client have ended.
    [[nodiscard]] bool ended() noexcept;

    /// How many of the other players' clients were started.
    [[nodiscard]] std::size_t others() const noexcept {
        return others_.size();
    }
    /// Whether it is played to be edited, not played.
    [[nodiscard]] bool edited() const noexcept {
        return settings_.edited;
    }
    /// How many times the game has been launched.
    [[nodiscard]] int launches() const noexcept {
        return launches_;
    }
    /// Asks both to stop, as a Host stops on a stop request.
    void stop() noexcept;
    /// Whether every process it started has ended, or ends by `until`:
    /// as Studio ends, a process asked to stop is given a moment to end as
    /// a Host does, its records written, before dropping it kills it.
    bool ended(std::chrono::steady_clock::time_point until) noexcept;

    /// Where the client streams its pictures when it plays in the
    /// viewport.
    [[nodiscard]] std::filesystem::path picture() const {
        return directory_ / "picture";
    }
    /// Where its server and client write their logs, server.log and
    /// client.log, which Studio's Console reads.
    [[nodiscard]] const std::filesystem::path& directory() const noexcept {
        return directory_;
    }

private:
    /// Picks the server's port and the client's endpoint's, writes the
    /// settings naming them, and starts the server, letting go of whatever
    /// a launch before left.
    result::Status launch();
    /// The other players' clients, once the first's player is in.
    void startOthers();

    Play() = default;

    std::optional<process::Child> server_;
    std::optional<process::Child> client_;
    /// The other players' clients, started once the first's player is in.
    std::vector<process::Child> others_;
    std::filesystem::path directory_;
    PlaySettings settings_;
    /// How many times the server has been started, each on ports of its own.
    int launches_ = 0;
    std::uint16_t serverEndpointPort_ = 0;
    /// How far the client's log has been read, and whether it said its
    /// player is in.
    std::uintmax_t clientLogRead_ = 0;
    bool admitted_ = false;
    std::uint16_t endpointPort_ = 0;
};

} // namespace rawframe::studio
