#include "connect.h"

#include "rawframe/document/json.h"
#include "rawframe/execution/time.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/world_tooling/server.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace rawframe::author {

namespace {

/// How long a reply may take before the client gives up on the endpoint.
constexpr auto kReplyWithin = std::chrono::seconds(10);

std::optional<std::string> firstLine(const char* path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    text.resize(std::min(text.find('\n'), text.size()));
    if (!text.empty() && text.back() == '\r') {
        text.pop_back();
    }
    return text;
}

void complain(std::string_view why) {
    std::fprintf(stderr, "rawframe-author connect: %.*s\n", static_cast<int>(why.size()), why.data());
}

/// Whether a reply line is an answer rather than an error.
bool answered(std::string_view reply) {
    const auto kParsed = document::parse(reply);
    return kParsed.has_value() && kParsed->find("answer") != nullptr;
}

/// Whether a record asks the endpoint to end the connection.
bool ending(std::string_view record) {
    const auto kParsed = document::parse(record);
    const document::Value* kKind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
    return kKind != nullptr && kKind->text() != nullptr && *kKind->text() == "tooling.end";
}

} // namespace

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

int connect(const char* endpoint, const char* pinFile, const char* tokenFile) {
    std::string said;
    std::unique_ptr<ToolingLink> link = ToolingLink::open(endpoint, pinFile, tokenFile, said);
    if (link == nullptr && !said.starts_with('{')) {
        complain(said);
        return 1;
    }
    std::printf("%s\n", said.c_str());
    std::fflush(stdout);
    if (link == nullptr) {
        return 1;
    }
    int status = 0;
    std::string line;
    for (int each = std::fgetc(stdin); each != EOF; each = std::fgetc(stdin)) {
        if (each != '\n') {
            line.push_back(static_cast<char>(each));
            continue;
        }
        const auto kReply = link->ask(line);
        const bool kEnded = ending(line);
        line.clear();
        // The endpoint closes on end, and the close may outrun its reply.
        if (!kReply.has_value() && kEnded) {
            return status;
        }
        if (!kReply.has_value()) {
            complain("the endpoint closed");
            return 1;
        }
        std::printf("%s\n", kReply->c_str());
        std::fflush(stdout);
        status = answered(*kReply) ? status : 1;
        if (kEnded) {
            return status;
        }
    }
    return status;
}

} // namespace rawframe::author
