#include "rawframe/authoring_session/link.h"

#include "line_client.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/session.h"
#include "rawframe/authoring_session/host.h"
#include "rawframe/document/json.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/world_tooling/server.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::authoring_session {

bool answered(std::string_view reply) {
    const auto kParsed = document::parse(reply);
    return kParsed.has_value() && kParsed->find("answer") != nullptr;
}

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
    auto client = std::make_unique<LineClient>(*link->provider_);
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

HostLink::HostLink() = default;

HostLink::~HostLink() {
    if (client_ != nullptr) {
        (void)client_->ask(R"({"kind":"authoring.end","id":null})");
        client_->close();
    }
}

std::unique_ptr<HostLink>
HostLink::open(std::string_view endpoint, const char* pinFile, const char* tokenFile, std::string& said) {
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
    auto provider = (*network)->provider(authoringProfile(1));
    if (!provider.has_value()) {
        said = "no provider for the authoring protocol";
        return nullptr;
    }
    std::unique_ptr<HostLink> link{new HostLink};
    link->network_ = std::move(*network);
    link->provider_ = std::move(*provider);
    auto client = std::make_unique<LineClient>(*link->provider_);
    if (!client->connected(endpoint)) {
        said = "the host could not be reached, or did not trust the pin";
        return nullptr;
    }
    document::Value hello = document::Value::object();
    hello.add("kind", document::Value::string("authoring.hello"));
    hello.add("id", document::Value::integer(0));
    hello.add("surfaceGeneration", document::Value::integer(authoring::kSurfaceGeneration));
    hello.add("token", document::Value::string(*kToken));
    auto welcome = client->ask(document::writeCompact(hello));
    if (!welcome.has_value()) {
        said = "the host closed before welcoming";
        return nullptr;
    }
    said = std::move(*welcome);
    if (!answered(said)) {
        return nullptr;
    }
    link->client_ = std::move(client);
    return link;
}

std::optional<std::string> HostLink::ask(std::string_view record) {
    return client_->ask(record);
}

bool HostLink::send(std::string_view record) {
    return client_->send(record);
}

std::optional<std::int64_t> HostLink::sentAs(const document::Value& id) const {
    return client_->sentAs(id);
}

std::vector<std::string> HostLink::heard(std::int64_t withinMilliseconds) {
    return client_->heard(withinMilliseconds);
}

bool HostLink::closed() const noexcept {
    return client_->closed();
}

JoinedSession::JoinedSession(std::unique_ptr<HostLink> link, std::string welcome)
    : link_(std::move(link)), welcome_(std::move(welcome)) {
}

std::unique_ptr<JoinedSession>
JoinedSession::open(std::string_view endpoint, const char* pinFile, const char* tokenFile, std::string& said) {
    std::unique_ptr<HostLink> link = HostLink::open(endpoint, pinFile, tokenFile, said);
    if (link == nullptr) {
        return nullptr;
    }
    return std::unique_ptr<JoinedSession>{new JoinedSession{std::move(link), said}};
}

std::string JoinedSession::answer(std::string_view line, bool& ended) {
    const auto kParsed = document::parse(line);
    const document::Value* kKind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
    const std::string kNamed =
        kKind != nullptr && kKind->kind() == document::Value::Kind::String ? *kKind->text() : std::string{};
    const document::Value* kAsked = kParsed.has_value() ? kParsed->find("id") : nullptr;
    const document::Value kId = kAsked != nullptr ? *kAsked : document::Value{};
    if (kNamed == "authoring.hello") {
        // The link said hello as it joined: the welcome is the answer,
        // under the asker's id.
        const auto kWelcome = document::parse(welcome_);
        document::Value welcomed = document::Value::object();
        for (const std::string& kName : kWelcome->names()) {
            welcomed.add(kName, kName == "id" ? kId : *kWelcome->find(kName));
        }
        return document::writeCompact(welcomed) + "\n";
    }
    if (kNamed == "authoring.cook") {
        // Answered when it ends, as a session's is: its progress and reply
        // come through `poll` (D885).
        if (link_->send(line)) {
            return {};
        }
        return authoring::writeRefusal(kId,
                                       result::fail(result::ErrorClass::Unavailable,
                                                    authoring::kAuthoringDomain,
                                                    authoring::code(authoring::AuthoringError::Internal),
                                                    "the host closed")
                                           .error());
    }
    std::string asked{line};
    if (const document::Value* kOperation = kParsed.has_value() ? kParsed->find("operation") : nullptr;
        kNamed == "authoring.cancel" && kOperation != nullptr) {
        // The host knows the operation by the number it went out under.
        if (const std::optional<std::int64_t> kNumber = link_->sentAs(*kOperation); kNumber.has_value()) {
            document::Value cancel = document::Value::object();
            for (const std::string& kName : kParsed->names()) {
                cancel.add(kName, kName == "operation" ? document::Value::integer(*kNumber) : *kParsed->find(kName));
            }
            asked = document::writeCompact(cancel);
        }
    }
    ended = kNamed == "authoring.end";
    const auto kReply = link_->ask(asked);
    if (!kReply.has_value()) {
        return authoring::writeRefusal(kId,
                                       result::fail(result::ErrorClass::Unavailable,
                                                    authoring::kAuthoringDomain,
                                                    authoring::code(authoring::AuthoringError::Internal),
                                                    "the host closed or did not answer")
                                           .error());
    }
    return *kReply + "\n";
}

bool JoinedSession::closed() const noexcept {
    return link_->closed();
}

std::vector<std::string> JoinedSession::poll() {
    std::vector<std::string> said;
    for (std::string& each : link_->heard(0)) {
        said.push_back(std::move(each) + "\n");
    }
    return said;
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
