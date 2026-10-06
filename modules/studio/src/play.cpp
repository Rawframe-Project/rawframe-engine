#include "play.h"

#include "rawframe/studio/errors.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>

namespace rawframe::studio {

namespace {

result::Status failed(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kStudioDomain, code(StudioError::PlayFailed), why).error()};
}

/// `text` as the whole of the file at `path`, readable by its owner alone
/// where the system has owners.
result::Status written(const std::filesystem::path& path, const std::string& text) {
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        return failed("a play file could not be written");
    }
    const bool kWhole = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool kClosed = std::fclose(file) == 0;
    std::error_code error;
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, error);
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
    std::error_code error;
    std::filesystem::create_directories(settings.directory, error);
    if (error) {
        return std::unexpected<result::Error>{failed("the play directory could not be made").error()};
    }
    std::random_device device;
    // Two ports of the dynamic range, the endpoint's after the server's.
    const auto kServerPort = static_cast<std::uint16_t>(49152 + (device() % 16000) * 2);
    const auto kEndpointPort = static_cast<std::uint16_t>(kServerPort + 1);
    std::string token;
    constexpr std::string_view kDigits = "0123456789abcdef";
    for (int each = 0; each < 48; ++each) {
        token += kDigits[device() % 16];
    }
    const std::filesystem::path& kAt = settings.directory;
    const std::string kGame = settings.game.string();
    const std::string kPort = std::to_string(kServerPort);
    RAWFRAME_TRY(written(kAt / "token", token + "\n"));
    RAWFRAME_TRY(written(kAt / "server.conf",
                         settingsOf(contents(settings.serverSettings),
                                    {{"host.iteration_rate", "120"}, {"world.tick_rate", "60"}},
                                    {{"kest.game", kGame},
                                     {"network.quic.self_signed", "true"},
                                     {"network.quic.fingerprint_file", (kAt / "server.fingerprint").string()},
                                     {"replication.endpoint", "127.0.0.1:" + kPort}})));
    RAWFRAME_TRY(written(kAt / "client.conf",
                         settingsOf(contents(settings.clientSettings),
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
    Play play;
    play.directory_ = kAt;
    play.endpointPort_ = kEndpointPort;
    RAWFRAME_TRY_ASSIGN(play.server_,
                        process::Child::start({.program = settings.server,
                                               .arguments = {"--config", (kAt / "server.conf").string()},
                                               .output = kAt / "server.log"}));
    RAWFRAME_TRY_ASSIGN(play.client_,
                        process::Child::start({.program = settings.client,
                                               .arguments = {"--config", (kAt / "client.conf").string()},
                                               .output = kAt / "client.log"}));
    return play;
}

std::optional<Preview> Play::preview() const {
    std::error_code error;
    if (std::filesystem::file_size(directory_ / "client.fingerprint", error) == 0 || error) {
        return std::nullopt;
    }
    return Preview{"127.0.0.1:" + std::to_string(endpointPort_),
                   (directory_ / "client.fingerprint").string(),
                   (directory_ / "token").string()};
}

bool Play::running() noexcept {
    return server_.has_value() && client_.has_value() && !server_->exited().has_value() &&
           !client_->exited().has_value();
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
