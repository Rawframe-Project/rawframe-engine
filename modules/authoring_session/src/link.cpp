#include "rawframe/authoring_session/link.h"

#include "line_client.h"
#include "previewing.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/session.h"
#include "rawframe/authoring_session/host.h"
#include "rawframe/document/json.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/world_tooling/server.h"
#include "replies.h"

#include <algorithm>
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

namespace {

/// The records that act on a played game's preview alone (D906), which a
/// host joined cannot reach: the game plays on this machine.
constexpr std::array<std::string_view, 15> kPreviewed = {"authoring.preview",
                                                         "authoring.pick",
                                                         "authoring.mark",
                                                         "authoring.preview_input",
                                                         "authoring.hide_layers",
                                                         "authoring.view_mode",
                                                         "authoring.split",
                                                         "authoring.overlays",
                                                         "authoring.run",
                                                         "authoring.step",
                                                         "authoring.traffic",
                                                         "authoring.profile",
                                                         "authoring.drawn_frame",
                                                         "authoring.live_entities",
                                                         "authoring.live_entity"};

} // namespace

JoinedSession::JoinedSession(std::unique_ptr<HostLink> link, std::string welcome)
    : link_(std::move(link)), welcome_(std::move(welcome)) {
}

JoinedSession::~JoinedSession() = default;

void JoinedSession::looked(const document::Value& said, std::string_view scene) {
    const document::Value* kView = said.find("view");
    const std::optional<authoring::SceneView> kRead =
        kView != nullptr && !kView->isNull() ? authoring::readSceneView(*kView) : std::nullopt;
    if (!kRead.has_value()) {
        return;
    }
    views_.insert_or_assign(std::string{scene}, *kRead);
    if (preview_ != nullptr && preview_->scene() == scene) {
        static_cast<void>(preview_->forward(std::string{scene}, kRead, {}));
    }
}

std::string JoinedSession::answerHere(std::string_view line) {
    document::Value idRead;
    const auto kRecord = authoring::readSessionRecord(line, idRead);
    if (!kRecord.has_value()) {
        return authoring::writeRefusal(idRead, kRecord.error());
    }
    const authoring::SessionRecord& kAsked = *kRecord;
    if (preview_ == nullptr) {
        preview_ = std::make_unique<Preview>();
    }
    const auto kFound = views_.find(kAsked.scene);
    const std::optional<authoring::SceneView> kView =
        kFound != views_.end() ? std::optional{kFound->second} : std::nullopt;
    const auto kRefused = [](std::string_view why) {
        return std::unexpected{result::fail(result::ErrorClass::FailedPrecondition,
                                            authoring::kAuthoringDomain,
                                            authoring::code(authoring::AuthoringError::UnsupportedOperation),
                                            why)
                                   .error()};
    };
    const auto kAnswer = [&]() -> result::Result<document::Value> {
        switch (kAsked.verb) {
        case authoring::SessionVerb::Preview: {
            RAWFRAME_TRY(preview_->open(kAsked));
            document::Value made = document::Value::object();
            made.add("kind", document::Value::string("authoring.preview"));
            made.add("reopened", document::Value::boolean(false));
            made.add("previewing", document::Value::boolean(preview_->forward(kAsked.scene, kView, {})));
            return made;
        }
        case authoring::SessionVerb::Pick:
            // No document is held here to grab a handle in or turn the
            // view of: what was met is answered, for the author to choose.
            return preview_->picked(kAsked, nullptr);
        case authoring::SessionVerb::Mark:
            return preview_->marked(kAsked);
        case authoring::SessionVerb::PreviewInput:
            return preview_->inputted(kAsked);
        case authoring::SessionVerb::HideLayers:
            return preview_->hid(kAsked);
        case authoring::SessionVerb::ViewMode:
            return preview_->moded(kAsked);
        case authoring::SessionVerb::Split:
            return preview_->split(kAsked, kView);
        case authoring::SessionVerb::Overlays:
            return preview_->overlaid(kAsked);
        case authoring::SessionVerb::Run:
        case authoring::SessionVerb::Step:
            return preview_->ran(kAsked);
        case authoring::SessionVerb::Traffic:
            return preview_->trafficOf(kAsked);
        case authoring::SessionVerb::Profile:
        case authoring::SessionVerb::DrawnFrame:
            // A capture is a file of the game's, which the host keeps.
            if (!kAsked.capture.empty()) {
                return kRefused("a capture is kept beside the game, which the host holds");
            }
            return kAsked.verb == authoring::SessionVerb::Profile ? preview_->profileOf(kAsked)
                                                                  : preview_->drawnOf(kAsked);
        case authoring::SessionVerb::LiveEntities:
        case authoring::SessionVerb::LiveEntity:
            return preview_->live(kAsked);
        default:
            return kRefused("the record is the host's");
        }
    }();
    return kAnswer.has_value() ? authoring::writeReply(kAsked.id, *kAnswer)
                               : authoring::writeRefusal(kAsked.id, kAnswer.error());
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
    if (std::ranges::find(kPreviewed, kNamed) != kPreviewed.end()) {
        return answerHere(line);
    }
    // Where this author looks, as said, for the preview held here.
    const document::Value* kScene = kParsed.has_value() ? kParsed->find("scene") : nullptr;
    const std::string kSceneNamed =
        kScene != nullptr && kScene->kind() == document::Value::Kind::String ? *kScene->text() : std::string{};
    if ((kNamed == "authoring.present" || kNamed == "authoring.view") && !kSceneNamed.empty()) {
        looked(*kParsed, kSceneNamed);
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
    // And as the host answers, where a record turned it (frame, undo).
    if (const auto kSaid = document::parse(*kReply); kSaid.has_value() && !kSceneNamed.empty()) {
        if (const document::Value* kAnswered = kSaid->find("answer"); kAnswered != nullptr) {
            looked(*kAnswered, kSceneNamed);
            // The boxes the host made of each author's choice, outlined in
            // the preview held here (D907), which says whether it drew them.
            const document::Value* kOutlines = kAnswered->find("outlines");
            if (kNamed == "authoring.outline" && kOutlines != nullptr && preview_ != nullptr) {
                document::Value answer = document::Value::object();
                answer.add("kind", document::Value::string("authoring.outlines"));
                answer.add("shown", document::Value::boolean(preview_->outline(kSceneNamed, *kOutlines)));
                answer.add("outlines", *kOutlines);
                return authoring::writeReply(kId, std::move(answer));
            }
        }
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
