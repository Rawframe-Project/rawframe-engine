#include "rawframe/authoring_session/link.h"

#include "rawframe/document/json.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/world_tooling/server.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace rawframe::authoring_session {

namespace {

/// How long a reply may take before the client gives up on the endpoint.
constexpr auto kReplyWithin = std::chrono::seconds(10);

/// A pin or token file's bytes at most, as the endpoint's token file.
constexpr std::size_t kMaximumLineFile = 4096;

/// The first line of a regular file of at most 4 KiB; none for anything
/// else, so a device or a large file is never read.
std::optional<std::string> firstLine(const char* path) {
    std::error_code failed;
    if (!std::filesystem::is_regular_file(path, failed) ||
        std::filesystem::file_size(path, failed) > kMaximumLineFile || failed) {
        return std::nullopt;
    }
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::string text(kMaximumLineFile, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    text.resize(std::min(text.find('\n'), text.size()));
    if (!text.empty() && text.back() == '\r') {
        text.pop_back();
    }
    return text;
}

} // namespace

bool answered(std::string_view reply) {
    const auto kParsed = document::parse(reply);
    return kParsed.has_value() && kParsed->find("answer") != nullptr;
}

class ToolingLink::Client {
public:
    explicit Client(network::Provider& provider) : provider_(provider) {
    }

    /// Waits until the connection is ready to carry records.
    bool connected(std::string_view endpoint) {
        auto connection = provider_.connect(network::Endpoint{std::string{endpoint}});
        if (!connection.has_value()) {
            return false;
        }
        connection_ = *connection;
        const auto kUntil = std::chrono::steady_clock::now() + kReplyWithin;
        while (std::chrono::steady_clock::now() < kUntil && !closed_) {
            pump();
            if (ready_) {
                auto stream = provider_.openStream(connection_, false);
                if (!stream.has_value()) {
                    return false;
                }
                stream_ = *stream;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    /// Sends one record and waits for its reply line; none when the
    /// endpoint closed or kept silent.
    std::optional<std::string> ask(std::string_view record) {
        std::string line{record};
        line += '\n';
        const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(line.data()), line.size()};
        if (!provider_.send(connection_, stream_, kBytes).has_value()) {
            return std::nullopt;
        }
        const auto kUntil = std::chrono::steady_clock::now() + kReplyWithin;
        while (std::chrono::steady_clock::now() < kUntil) {
            pump();
            const std::size_t kEnd = received_.find('\n');
            if (kEnd != std::string::npos) {
                std::string reply = received_.substr(0, kEnd);
                received_.erase(0, kEnd + 1);
                return reply;
            }
            if (closed_) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return std::nullopt;
    }

    void close() noexcept {
        provider_.close(connection_);
    }

private:
    void pump() {
        events_.clear();
        provider_.poll(events_, 64);
        for (const network::Event& event : events_) {
            if (event.kind == network::EventKind::Connected) {
                ready_ = true;
            } else if (event.kind == network::EventKind::StreamBytes) {
                received_.append(reinterpret_cast<const char*>(event.bytes.data()), event.bytes.size());
            } else if (event.kind == network::EventKind::Closed) {
                closed_ = true;
            }
        }
    }

    network::Provider& provider_;
    network::ConnectionId connection_;
    network::StreamId stream_;
    std::vector<network::Event> events_;
    std::string received_;
    bool ready_ = false;
    bool closed_ = false;
};

ToolingLink::ToolingLink() = default;

ToolingLink::~ToolingLink() {
    if (client_ != nullptr) {
        // Said for the endpoint; whether its reply or the close comes first,
        // the connection is over.
        (void)client_->ask(R"({"kind":"tooling.end","id":null})");
        client_->close();
    }
}

std::unique_ptr<ToolingLink>
ToolingLink::open(std::string_view endpoint, const char* pinFile, const char* tokenFile, std::string& said) {
    const auto kPin = firstLine(pinFile);
    const auto kToken = firstLine(tokenFile);
    if (!kPin.has_value() || !kToken.has_value()) {
        said = "the pin and token files must read";
        return nullptr;
    }
    auto pin = network_quic::parseFingerprint(*kPin);
    if (!pin.has_value()) {
        said = "the pin file holds no certificate fingerprint";
        return nullptr;
    }
    auto network = network_quic::QuicNetwork::create(network_quic::QuicSettings{.pin = *pin});
    if (!network.has_value()) {
        said = "QUIC could not start";
        return nullptr;
    }
    auto provider = (*network)->provider(world_tooling::toolingProfile(1));
    if (!provider.has_value()) {
        said = "no provider for the tooling protocol";
        return nullptr;
    }
    std::unique_ptr<ToolingLink> link{new ToolingLink};
    link->network_ = std::move(*network);
    link->provider_ = std::move(*provider);
    auto client = std::make_unique<Client>(*link->provider_);
    if (!client->connected(endpoint)) {
        said = "the endpoint could not be reached, or did not trust the pin";
        return nullptr;
    }
    document::Value hello = document::Value::object();
    hello.add("kind", document::Value::string("tooling.hello"));
    hello.add("id", document::Value::integer(0));
    hello.add("protocolVersion", document::Value::integer(world_tooling::kToolingProtocolVersion));
    hello.add("token", document::Value::string(*kToken));
    auto welcome = client->ask(document::writeCompact(hello));
    if (!welcome.has_value()) {
        said = "the endpoint closed before welcoming";
        return nullptr;
    }
    said = std::move(*welcome);
    if (!answered(said)) {
        return nullptr;
    }
    link->client_ = std::move(client);
    return link;
}

std::optional<std::string> ToolingLink::ask(std::string_view record) {
    return client_->ask(record);
}

std::optional<std::string> loopbackEndpoint(std::string_view endpoint) {
    const std::size_t kColon = endpoint.rfind(':');
    if (kColon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view kHost = endpoint.substr(0, kColon);
    const std::string_view kPortText = endpoint.substr(kColon + 1);
    unsigned port = 0;
    const auto [kPortEnd, kPortError] = std::from_chars(kPortText.data(), kPortText.data() + kPortText.size(), port);
    if (kPortError != std::errc{} || kPortEnd != kPortText.data() + kPortText.size() || port == 0 || port > 65535 ||
        kPortText.front() == '0') {
        return std::nullopt;
    }
    const std::string kPort = std::to_string(port);
    if (kHost == "[::1]") {
        return "[::1]:" + kPort;
    }
    // Four decimal parts of 0 to 255 without leading zeros, the first 127:
    // a literal every resolver reads as itself, never as a name.
    std::array<unsigned, 4> parts{};
    std::size_t at = 0;
    for (std::size_t part = 0; part < parts.size(); ++part) {
        const std::size_t kEnd = part + 1 < parts.size() ? kHost.find('.', at) : kHost.size();
        if (kEnd == std::string_view::npos || kEnd == at || kEnd - at > 3 || (kEnd - at > 1 && kHost[at] == '0')) {
            return std::nullopt;
        }
        const auto [kDigitsEnd, kError] = std::from_chars(kHost.data() + at, kHost.data() + kEnd, parts[part]);
        if (kError != std::errc{} || kDigitsEnd != kHost.data() + kEnd || parts[part] > 255) {
            return std::nullopt;
        }
        at = kEnd + 1;
    }
    if (parts[0] != 127) {
        return std::nullopt;
    }
    return std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]) + "." +
           std::to_string(parts[3]) + ":" + kPort;
}

} // namespace rawframe::authoring_session
