#pragma once

// A game Studio plays to preview it (D445): a dedicated server for the
// game, and a client whose player joins it from a window of its own and
// serves a tooling endpoint with the view grant, on this machine. Studio
// starts them, attaches the client's endpoint as the chosen scene's
// preview once it says who it is, and stops them; a process still running
// when Studio goes is killed. Nothing here waits.

#include "rawframe/process/child.h"
#include "rawframe/result/result.h"
#include "records.h"

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
};

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
    /// Whether both still run.
    [[nodiscard]] bool running() noexcept;
    /// Whether both have ended.
    [[nodiscard]] bool ended() noexcept;

    /// How many times the game has been launched.
    [[nodiscard]] int launches() const noexcept {
        return launches_;
    }
    /// Asks both to stop, as a Host stops on a stop request.
    void stop() noexcept;

private:
    /// Picks the server's port and the client's endpoint's, writes the
    /// settings naming them, and starts the server, letting go of whatever
    /// a launch before left.
    result::Status launch();

    Play() = default;

    std::optional<process::Child> server_;
    std::optional<process::Child> client_;
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
