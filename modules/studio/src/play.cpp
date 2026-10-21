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

/// `text` without its lines setting any of `keys`: settings a viewport's
/// play owns (D789), which a game's own client settings may set too.
std::string without(std::string_view text, std::initializer_list<std::string_view> keys) {
    std::string kept;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t kEnd = std::min(text.find('\n', at), text.size());
        const std::string_view kLine = text.substr(at, kEnd - at);
        if (!std::ranges::any_of(keys, [&](std::string_view key) {
                return sets(kLine, key);
            })) {
            kept += std::string{kLine} + "\n";
        }
        at = kEnd + 1;
    }
    return kept;
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
    for (process::Child& other : others_) {
        if (!other.exited().has_value()) {
            other.kill();
        }
    }
    others_.clear();
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
    // What the last play left, the client's log among it: the Console reads
    // that file from its start before this play's client is launched, and
    // would show the last play's lines as this one's.
    for (const char* kLeft : {"server.fingerprint", "client.fingerprint", "picture", "client.log"}) {
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
                                     // Edited, ten times a second (D912).
                                     {"kest.reload_every", settings_.edited ? "12" : "60"},
                                     // Kept running on a machine too busy to keep up,
                                     // as its client is (D924).
                                     {"world.overload_ms", "0"}},
                                    {{"kest.game", kGame},
                                     {"network.quic.self_signed", "true"},
                                     {"network.quic.fingerprint_file", (kAt / "server.fingerprint").string()},
                                     {"replication.endpoint", "127.0.0.1:" + kPort},
                                     {"tooling.endpoint", "127.0.0.1:" + std::to_string(kServerEndpointPort)},
                                     {"tooling.token_file", (kAt / "token").string()},
                                     // Picked from (D456) and debugged (D460).
                                     {"tooling.grants", "inspect debug run"},
                                     // Layers replicated, so the client hides
                                     // one (D791); the client is told so too.
                                     {"kest.replicate_layers", "true"}}) +
                             (settings_.edited ? "world.edited = true\nreplication.interest = false\n"
                                                 "replication.egress_bytes_per_second = 67108864\n"
                                               : "")));
    // Played in Studio's viewport (D789, D790): its window kept hidden at
    // the viewport's size, so a point there is the same point here, and
    // each picture streamed; those settings Studio's, not the game's.
    std::string given = contents(settings_.clientSettings) + settings_.link;
    // As a phone or a tablet (D809): its size in a window of its own, and
    // the mouse its finger, whatever the game's settings say of either.
    if (settings_.device.has_value()) {
        given = without(given, {"window.width", "window.height", "input.touch_from_mouse"}) +
                "window.width = " + std::to_string((*settings_.device)[0]) +
                "\nwindow.height = " + std::to_string((*settings_.device)[1]) + "\ninput.touch_from_mouse = true\n";
    }
    std::string viewported;
    if (settings_.viewport.has_value()) {
        given = without(given,
                        {"window.visible",
                         "window.width",
                         "window.height",
                         "render.offscreen",
                         "render.read_every",
                         "render.width",
                         "render.height",
                         "render.stream",
                         "render.frame_rate",
                         "preview.views",
                         "preview.flight"});
        const std::string kWidth = std::to_string((*settings_.viewport)[0]);
        const std::string kHeight = std::to_string((*settings_.viewport)[1]);
        viewported = "window.visible = false\nwindow.width = " + kWidth + "\nwindow.height = " + kHeight +
                     "\nrender.offscreen = true\nrender.read_every = 1\nrender.width = " + kWidth +
                     "\nrender.height = " + kHeight + "\nrender.stream = " + (kAt / "picture").string() +
                     // Split three ways at most, in the viewport only (D797).
                     "\npreview.views = 3\n" +
                     // Edited, its camera flown as an editor's (D913).
                     (settings_.edited ? "preview.flight = true\n" : "") +
                     // At most sixty pictures a second, a display's: each one
                     // is read back and written whole to the stream, which a
                     // device that keeps up did at its host's 120 (D947).
                     "render.frame_rate = 60\n";
    }
    RAWFRAME_TRY(written(kAt / "client.conf",
                         settingsOf(kWithContent(given),
                                    {{"host.iteration_rate", "120"},
                                     {"kest.plan_only", "true"},
                                     // Its present systems' program followed as
                                     // the server's is, twice a second.
                                     {"kest.reload_every", "60"},
                                     {"bots.player", "true"},
                                     {"render.device", "any"},
                                     // Its styles and UI document followed twice a
                                     // second, an edited one given to the UI as it
                                     // plays (D634, D761).
                                     {"ui.reload_every", "60"},
                                     // And its content, a material Studio cooks
                                     // again drawn as it plays (D734).
                                     {"content.reload_every", "60"},
                                     // Kept running however far behind a slow
                                     // device leaves it: an author's preview, not
                                     // a player's game (D923).
                                     {"world.overload_ms", "0"}},
                                    {{"kest.game", kGame},
                                     {"network.quic.pin_file", (kAt / "server.fingerprint").string()},
                                     {"bots.endpoint", "127.0.0.1:" + kPort},
                                     {"network.quic.self_signed", "true"},
                                     {"network.quic.fingerprint_file", (kAt / "client.fingerprint").string()},
                                     {"tooling.endpoint", "127.0.0.1:" + std::to_string(kEndpointPort)},
                                     {"tooling.token_file", (kAt / "token").string()},
                                     {"tooling.grants", "view"},
                                     {"kest.replicate_layers", "true"}}) +
                             viewported));
    endpointPort_ = kEndpointPort;
    serverEndpointPort_ = kServerEndpointPort;
    // The other players (D804): clients as the first, each in a window of
    // its own set down and right of the last, with no tooling endpoint and
    // nothing of the viewport's.
    const std::string kOthersGiven = settings_.link + without(contents(settings_.clientSettings),
                                                              {"window.visible",
                                                               "window.x",
                                                               "window.y",
                                                               "render.offscreen",
                                                               "render.read_every",
                                                               "render.stream",
                                                               "preview.views"});
    for (std::uint32_t player = 2; player <= std::min<std::uint32_t>(settings_.players, 4); ++player) {
        const std::string kName = "client-" + std::to_string(player);
        RAWFRAME_TRY(written(kAt / (kName + ".conf"),
                             settingsOf(kWithContent(kOthersGiven),
                                        {{"host.iteration_rate", "120"},
                                         {"kest.plan_only", "true"},
                                         {"bots.player", "true"},
                                         {"render.device", "any"},
                                         {"window.width", "640"},
                                         {"window.height", "360"},
                                         {"world.overload_ms", "0"}},
                                        {{"kest.game", kGame},
                                         {"network.quic.pin_file", (kAt / "server.fingerprint").string()},
                                         {"bots.endpoint", "127.0.0.1:" + kPort},
                                         {"network.quic.self_signed", "true"},
                                         {"network.quic.fingerprint_file", (kAt / (kName + ".fingerprint")).string()},
                                         {"kest.replicate_layers", "true"},
                                         {"window.x", std::to_string(40 * player)},
                                         {"window.y", std::to_string(30 * player)}})));
    }
    // Where a debugger attaches, for rawframe-debug given the game (D462).
    RAWFRAME_TRY(written(kAt / authoring_session::kAttachFile,
                         authoring_session::attachText(authoring_session::AttachRecord{
                             .endpoint = "127.0.0.1:" + std::to_string(kServerEndpointPort),
                             .pinFile = (kAt / "server.fingerprint").string(),
                             .tokenFile = (kAt / "token").string()})));
    RAWFRAME_TRY_ASSIGN(server_,
                        process::Child::start({.program = settings_.server,
                                               .arguments = {"--config", (kAt / "server.conf").string()},
                                               .output = kAt / "server.log",
                                               .stopsWithThis = true}));
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
    if (!client_.has_value()) {
        RAWFRAME_TRY_ASSIGN(client_,
                            process::Child::start({.program = settings_.client,
                                                   .arguments = {"--config", (directory_ / "client.conf").string()},
                                                   .output = directory_ / "client.log",
                                                   .stopsWithThis = true}));
        return {};
    }
    return {};
}

void Play::startOthers() {
    if (!admitted_ || !others_.empty()) {
        return;
    }
    for (std::uint32_t player = 2; player <= std::min<std::uint32_t>(settings_.players, 4); ++player) {
        const std::string kName = "client-" + std::to_string(player);
        auto started = process::Child::start({.program = settings_.client,
                                              .arguments = {"--config", (directory_ / (kName + ".conf")).string()},
                                              .output = directory_ / (kName + ".log"),
                                              .stopsWithThis = true});
        if (started.has_value()) {
            others_.push_back(std::move(*started));
        }
    }
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
    startOthers();
    return Preview{.endpoint = "127.0.0.1:" + std::to_string(endpointPort_),
                   .pinFile = (directory_ / "client.fingerprint").string(),
                   .tokenFile = (directory_ / "token").string(),
                   .serverEndpoint = "127.0.0.1:" + std::to_string(serverEndpointPort_),
                   .serverPinFile = (directory_ / "server.fingerprint").string()};
}

bool Play::running() noexcept {
    return server_.has_value() && !server_->exited().has_value() &&
           (!client_.has_value() || !client_->exited().has_value()) &&
           std::ranges::none_of(others_, [](process::Child& other) {
               return other.exited().has_value();
           });
}

bool Play::ended() noexcept {
    return (!server_.has_value() || server_->exited().has_value()) &&
           (!client_.has_value() || client_->exited().has_value()) &&
           std::ranges::all_of(others_, [](process::Child& other) {
               return other.exited().has_value();
           });
}

void Play::stop() noexcept {
    for (process::Child& other : others_) {
        if (!other.exited().has_value()) {
            static_cast<void>(other.requestStop());
        }
    }
    for (std::optional<process::Child>* each : {&client_, &server_}) {
        if (each->has_value() && !(*each)->exited().has_value()) {
            static_cast<void>((*each)->requestStop());
        }
    }
}

} // namespace rawframe::studio
