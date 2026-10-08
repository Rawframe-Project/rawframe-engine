#include "play.h"

#include "rawframe/authoring_session/attach.h"
#include "rawframe/studio/errors.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rawframe::studio {

namespace {

result::Status failed(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kStudioDomain, code(StudioError::PlayFailed), why).error()};
}

/// `text` as the whole of a new file at `path`, in the play directory its
/// owner alone may enter. What was there is removed first, a link and not
/// what it names, and the file is made anew or not at all ("x"), so no
/// link put in its place is followed.
result::Status written(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::remove(path, error);
    std::FILE* file = std::fopen(path.string().c_str(), "wbx");
    if (file == nullptr) {
        return failed("a play file could not be written");
    }
    const bool kWhole = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool kClosed = std::fclose(file) == 0;
    return kWhole && kClosed ? result::Status{} : failed("a play file could not be written");
}

/// Whether the settings `text` give `key` a value.
bool sets(std::string_view text, std::string_view key) {
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t kEnd = std::min(text.find('\n', at), text.size());
        std::string_view line = text.substr(at, kEnd - at);
        line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
        if (line.starts_with(key)) {
            std::string_view rest = line.substr(key.size());
            rest.remove_prefix(std::min(rest.find_first_not_of(" \t"), rest.size()));
            if (rest.starts_with('=')) {
                return true;
            }
        }
        at = kEnd + 1;
    }
    return false;
}

/// The file at `path` whole, or nothing for an empty path.
std::string contents(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::string text;
    if (std::FILE* file = std::fopen(path.string().c_str(), "rb")) {
        std::array<char, 4096> chunk{};
        std::size_t read = 0;
        while ((read = std::fread(chunk.data(), 1, chunk.size(), file)) > 0) {
            text.append(chunk.data(), read);
        }
        std::fclose(file);
    }
    return text.empty() || text.back() == '\n' ? text : text + "\n";
}

} // namespace

// The play directory, made if it is not there, a directory and no link,
// entered by its owner alone before anything is put in it: the token and
// the settings naming it are no one else's to read.
result::Status privateDirectory(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(std::filesystem::symlink_status(path, error))) {
        std::filesystem::create_directories(path, error);
        if (error) {
            return failed("the play directory could not be made");
        }
    }
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(path, error))) {
        return failed("the play directory is not a directory of its own");
    }
#if !defined(_WIN32)
    // Made by another user first, it is theirs: never written into (D462).
    struct stat held{};
    if (::lstat(path.c_str(), &held) != 0 || held.st_uid != ::getuid()) {
        return failed("the play directory is another user's");
    }
#endif
    std::filesystem::permissions(
        path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
    if (error) {
        return failed("the play directory could not be kept to its owner");
    }
    return {};
}

std::string settingsOf(const std::string& given,
                       std::initializer_list<std::pair<std::string_view, std::string>> defaults,
                       std::initializer_list<std::pair<std::string_view, std::string>> owned) {
    std::string text = given;
    for (const auto& [kKey, kValue] : defaults) {
        if (!sets(given, kKey)) {
            text += std::string{kKey} + " = " + kValue + "\n";
        }
    }
    for (const auto& [kKey, kValue] : owned) {
        text += std::string{kKey} + " = " + kValue + "\n";
    }
    return text;
}

result::Result<Play> Play::start(const PlaySettings& settings) {
    RAWFRAME_TRY(privateDirectory(settings.directory));
    std::random_device device;
    std::string token;
    constexpr std::string_view kDigits = "0123456789abcdef";
    for (int each = 0; each < 48; ++each) {
        token += kDigits[device() % 16];
    }
    RAWFRAME_TRY(written(settings.directory / "token", token + "\n"));
    Play play;
    play.directory_ = settings.directory;
    play.settings_ = settings;
    RAWFRAME_TRY(play.launch());
    return play;
}

result::Status Play::launch() {
    for (std::optional<process::Child>* each : {&client_, &server_}) {
        if (each->has_value() && !(*each)->exited().has_value()) {
            (*each)->kill();
        }
        each->reset();
    }
    ++launches_;
    admitted_ = false;
    clientLogRead_ = 0;
    std::random_device device;
    // Three ports below every system's range for outgoing connections
    // (Linux takes 32768 and up, Windows 49152 and up), so no socket of
    // another process holds one by chance: the server's, the client's
    // endpoint after it, and the server's endpoint, for picking (D456).
    // Below 30000, where the tests' tools/free_port.py draws, so the two
    // never meet; a port taken all the same is refused (D470) and the game
    // launched again on others.
    const auto kServerPort = static_cast<std::uint16_t>(20000 + (device() % 2500) * 4);
    const auto kEndpointPort = static_cast<std::uint16_t>(kServerPort + 1);
    const auto kServerEndpointPort = static_cast<std::uint16_t>(kServerPort + 2);
    const std::filesystem::path& kAt = directory_;
    for (const char* kLeft : {"server.fingerprint", "client.fingerprint"}) {
        std::error_code error;
        std::filesystem::remove(kAt / kLeft, error);
    }
    const std::string kGame = settings_.game.string();
    const std::string kPort = std::to_string(kServerPort);
    // The content Studio cooked, for a program whose settings name none
    // (D502).
    const auto kWithContent = [this](std::string given) {
        std::error_code error;
        if (!settings_.content.empty() && !sets(given, "content.root") &&
            std::filesystem::is_regular_file(settings_.content / "content.manifest", error)) {
            given += (given.empty() || given.ends_with('\n') ? "" : "\n") + std::string{"content.root = "} +
                     settings_.content.string() + "\n";
        }
        return given;
    };
    RAWFRAME_TRY(written(kAt / "server.conf",
                         settingsOf(kWithContent(contents(settings_.serverSettings)),
                                    {{"host.iteration_rate", "120"},
                                     {"world.tick_rate", "60"},
                                     // Its scenes followed once a second: an edit Studio
                                     // applies writes a scene, which the World then takes
                                     // (D410, D446), as ADR-0032 has a live edit go.
                                     {"kest.reload_every", "60"}},
                                    {{"kest.game", kGame},
                                     {"network.quic.self_signed", "true"},
                                     {"network.quic.fingerprint_file", (kAt / "server.fingerprint").string()},
                                     {"replication.endpoint", "127.0.0.1:" + kPort},
                                     {"tooling.endpoint", "127.0.0.1:" + std::to_string(kServerEndpointPort)},
                                     {"tooling.token_file", (kAt / "token").string()},
                                     // Picked from (D456) and debugged (D460).
                                     {"tooling.grants", "inspect debug"}})));
    RAWFRAME_TRY(written(kAt / "client.conf",
                         settingsOf(kWithContent(contents(settings_.clientSettings)),
                                    {{"host.iteration_rate", "120"},
                                     {"kest.plan_only", "true"},
                                     {"bots.count", "1"},
                                     {"bots.player", "true"},
                                     {"render.device", "any"}},
                                    {{"kest.game", kGame},
                                     {"network.quic.pin_file", (kAt / "server.fingerprint").string()},
                                     {"bots.endpoint", "127.0.0.1:" + kPort},
                                     {"network.quic.self_signed", "true"},
                                     {"network.quic.fingerprint_file", (kAt / "client.fingerprint").string()},
                                     {"tooling.endpoint", "127.0.0.1:" + std::to_string(kEndpointPort)},
                                     {"tooling.token_file", (kAt / "token").string()},
                                     {"tooling.grants", "view"}})));
    endpointPort_ = kEndpointPort;
    serverEndpointPort_ = kServerEndpointPort;
    // Where a debugger attaches, for rawframe-debug given the game (D462).
    RAWFRAME_TRY(written(kAt / authoring_session::kAttachFile,
                         authoring_session::attachText(authoring_session::AttachRecord{
                             .endpoint = "127.0.0.1:" + std::to_string(kServerEndpointPort),
                             .pinFile = (kAt / "server.fingerprint").string(),
                             .tokenFile = (kAt / "token").string()})));
    RAWFRAME_TRY_ASSIGN(server_,
                        process::Child::start({.program = settings_.server,
                                               .arguments = {"--config", (kAt / "server.conf").string()},
                                               .output = kAt / "server.log"}));
    return {};
}

result::Status Play::advance() {
    // A port another process took ends the server or the client as it
    // starts; until the client's player is in, the game is launched again
    // on other ports, a few times.
    constexpr int kLaunches = 5;
    const bool kServerEnded = server_.has_value() && server_->exited().has_value();
    const bool kClientEnded = client_.has_value() && client_->exited().has_value();
    if ((kServerEnded || kClientEnded) && !admitted_ && launches_ < kLaunches) {
        return launch();
    }
    std::error_code error;
    if (client_.has_value() || std::filesystem::file_size(directory_ / "server.fingerprint", error) == 0 || error) {
        return {};
    }
    RAWFRAME_TRY_ASSIGN(client_,
                        process::Child::start({.program = settings_.client,
                                               .arguments = {"--config", (directory_ / "client.conf").string()},
                                               .output = directory_ / "client.log"}));
    return {};
}

std::optional<Preview> Play::preview() {
    std::error_code error;
    if (std::filesystem::file_size(directory_ / "client.fingerprint", error) == 0 || error) {
        return std::nullopt;
    }
    // Attached before its player is in, a client still loading answers a
    // session slowly, and the session waits for it.
    if (!admitted_) {
        const std::uintmax_t kSize = std::filesystem::file_size(directory_ / "client.log", error);
        if (error || kSize <= clientLogRead_) {
            return std::nullopt;
        }
        if (std::FILE* file = std::fopen((directory_ / "client.log").string().c_str(), "rb")) {
            // Back a little, so a record split across reads is still found.
            constexpr std::uintmax_t kOverlap = 64;
            const std::uintmax_t kFrom = clientLogRead_ > kOverlap ? clientLogRead_ - kOverlap : 0;
            std::string read(static_cast<std::size_t>(kSize - kFrom), '\0');
            if (std::fseek(file, static_cast<long>(kFrom), SEEK_SET) == 0) {
                read.resize(std::fread(read.data(), 1, read.size(), file));
                admitted_ = read.find("\"code\":\"bots_admitted\"") != std::string::npos;
                clientLogRead_ = kFrom + read.size();
            }
            std::fclose(file);
        }
        if (!admitted_) {
            return std::nullopt;
        }
    }
    return Preview{.endpoint = "127.0.0.1:" + std::to_string(endpointPort_),
                   .pinFile = (directory_ / "client.fingerprint").string(),
                   .tokenFile = (directory_ / "token").string(),
                   .serverEndpoint = "127.0.0.1:" + std::to_string(serverEndpointPort_),
                   .serverPinFile = (directory_ / "server.fingerprint").string()};
}

bool Play::running() noexcept {
    return server_.has_value() && !server_->exited().has_value() &&
           (!client_.has_value() || !client_->exited().has_value());
}

bool Play::ended() noexcept {
    return (!server_.has_value() || server_->exited().has_value()) &&
           (!client_.has_value() || client_->exited().has_value());
}

void Play::stop() noexcept {
    for (std::optional<process::Child>* each : {&client_, &server_}) {
        if (each->has_value() && !(*each)->exited().has_value()) {
            static_cast<void>((*each)->requestStop());
        }
    }
}

} // namespace rawframe::studio
