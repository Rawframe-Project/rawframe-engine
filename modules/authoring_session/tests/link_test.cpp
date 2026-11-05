// A tooling link against an endpoint that misbehaves, over QUIC on this
// machine's loopback interface: a line past the most the tooling protocol
// sends at once ends the link, and what the endpoint says unasked is kept
// to its last kMostUnheardBytes.

#include "rawframe/authoring_session/link.h"
#include "rawframe/document/json.h"
#include "rawframe/network/provider.h"
#include "rawframe/network_quic/certificate.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/test/test.h"
#include "rawframe/world_tooling/server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace rawframe;
using namespace rawframe::authoring_session;

namespace {

/// The most the tooling protocol sends at once, which a link takes as its
/// longest line.
const std::size_t kMostAtOnce = world_tooling::toolingProfile(1).maximumStreamSend;

/// A UDP port nothing holds right now, from the kernel.
std::uint16_t freePort() {
#if defined(_WIN32)
    // Windows' sockets start per process; starting them twice is counted.
    WSADATA started{};
    ::WSAStartup(MAKEWORD(2, 2), &started);
    const SOCKET kSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    int length = sizeof(sockaddr_in);
#else
    const int kSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    socklen_t length = sizeof(sockaddr_in);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(kSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    ::getsockname(kSocket, reinterpret_cast<sockaddr*>(&address), &length);
#if defined(_WIN32)
    ::closesocket(kSocket);
#else
    ::close(kSocket);
#endif
    return ntohs(address.sin_port);
}

/// The reply line to the record numbered `id`, its answer `answer`.
std::string replyLine(std::int64_t id, std::string_view answer) {
    return R"({"kind":"tooling.reply","id":)" + std::to_string(id) + R"(,"answer":)" + std::string{answer} + "}\n";
}

/// A tooling endpoint on its own thread that welcomes any hello and answers
/// every other record with the bytes `answer` gives for its kind and
/// number, line or not.
class Endpoint {
public:
    using Answer = std::function<std::string(std::string_view kind, std::int64_t id)>;

    explicit Endpoint(Answer answer) : answer_(std::move(answer)) {
        const auto kIdentity = network_quic::makeSelfSignedCertificate("rawframe-test", 1);
        auto network = network_quic::QuicNetwork::create(network_quic::QuicSettings{.certificate = *kIdentity});
        network_ = std::move(*network);
        provider_ = std::move(*network_->provider(world_tooling::toolingProfile(1)));
        port_ = freePort();
        RAWFRAME_EXPECT(provider_->listen({"127.0.0.1:" + std::to_string(port_)}).has_value());
        std::ofstream{"link_test.pin"} << network_quic::formatFingerprint(*network_quic::fingerprintOf(*kIdentity))
                                       << "\n";
        std::ofstream{"link_test.token"} << "a token the endpoint takes whatever it is\n";
        thread_ = std::thread{[this] {
            run();
        }};
    }
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    ~Endpoint() {
        stop_.store(true);
        thread_.join();
    }

    [[nodiscard]] std::unique_ptr<ToolingLink> link() const {
        std::string said;
        auto made = ToolingLink::open("127.0.0.1:" + std::to_string(port_), "link_test.pin", "link_test.token", said);
        RAWFRAME_EXPECT(made != nullptr);
        return made;
    }

private:
    void run() {
        std::vector<network::Event> events;
        std::string pending;
        std::string outgoing;
        std::optional<network::ConnectionId> connection;
        network::StreamId stream{};
        while (!stop_.load()) {
            events.clear();
            provider_->poll(events, 64);
            for (const network::Event& kEvent : events) {
                if (kEvent.kind == network::EventKind::StreamBytes) {
                    connection = kEvent.connection;
                    stream = kEvent.stream;
                    pending.append(reinterpret_cast<const char*>(kEvent.bytes.data()), kEvent.bytes.size());
                } else if (kEvent.kind == network::EventKind::Closed) {
                    connection.reset();
                    outgoing.clear();
                }
            }
            for (std::size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
                const auto kRecord = document::parse(std::string_view{pending}.substr(0, end));
                pending.erase(0, end + 1);
                const document::Value* kKind = kRecord.has_value() ? kRecord->find("kind") : nullptr;
                const document::Value* kId = kRecord.has_value() ? kRecord->find("id") : nullptr;
                const std::int64_t kNumber = kId != nullptr ? kId->integer().value_or(0) : 0;
                const std::string kNamed = kKind != nullptr && kKind->text() != nullptr ? *kKind->text() : "";
                outgoing += kNamed == "tooling.hello" ? replyLine(kNumber, R"({"kind":"tooling.welcome"})")
                                                      : answer_(kNamed, kNumber);
            }
            // Sent a piece at a time while what the peer has not acknowledged
            // stays within half of what a connection holds, so neither a
            // late acknowledgement nor a send past that holding stalls it.
            constexpr std::size_t kPiece = std::size_t{64} << 10U;
            constexpr std::size_t kInFlight = std::size_t{256} << 10U;
            while (connection.has_value() && !outgoing.empty() &&
                   provider_->pendingBytes(*connection, stream) + kPiece <= kInFlight) {
                const std::size_t kSending = std::min(outgoing.size(), kPiece);
                const auto kBytes = std::as_bytes(std::span{outgoing.data(), kSending});
                if (provider_->send(*connection, stream, kBytes).has_value()) {
                    outgoing.erase(0, kSending);
                } else {
                    outgoing.clear();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    Answer answer_;
    std::unique_ptr<network_quic::QuicNetwork> network_;
    std::unique_ptr<network::Provider> provider_;
    std::uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

/// A reply line whose bytes before its newline are exactly `bytes`.
std::string replyOfLength(std::int64_t id, std::size_t bytes) {
    const std::string kShell = replyLine(id, R"({"pad":""})");
    return replyLine(id, R"({"pad":")" + std::string(bytes - (kShell.size() - 1), 'x') + R"("})");
}

} // namespace

RAWFRAME_TEST(ALineAsLongAsTheProtocolSendsIsTakenAndOnePastItEndsTheLink) {
    const Endpoint kEndpoint{[](std::string_view kind, std::int64_t id) {
        // The longest line the protocol sends, then one byte more and no
        // newline at all.
        return kind == "test.longest" ? replyOfLength(id, kMostAtOnce) : std::string(kMostAtOnce + 1, 'x');
    }};
    const auto kLink = kEndpoint.link();
    if (kLink == nullptr) {
        return;
    }
    const auto kLongest = kLink->ask(R"({"kind":"test.longest","id":1})");
    RAWFRAME_EXPECT(kLongest.has_value() && kLongest->find(R"("pad":"xxx)") != std::string::npos);
    RAWFRAME_EXPECT(!kLink->overran() && !kLink->closed());
    RAWFRAME_EXPECT(!kLink->ask(R"({"kind":"test.past","id":2})").has_value());
    RAWFRAME_EXPECT(kLink->overran() && kLink->closed() && !kLink->silent());
}

/// A line said unasked, numbered `n`, its bytes before its newline
/// `bytes`.
std::string saidLine(std::size_t n, std::size_t bytes) {
    const std::string kShell = R"({"kind":"test.said","n":)" + std::to_string(n) + R"(,"pad":""})";
    return kShell.substr(0, kShell.size() - 2) + std::string(bytes - kShell.size(), 'x') + "\"}\n";
}

RAWFRAME_TEST(WhatAnEndpointSaysUnaskedIsKeptToItsLastBytes) {
    // Lines that each cost a sixty-fourth of the bound with their keeping,
    // two said before each reply, as connect and the adapter, which never
    // take them, would have them said across a session: of 70 said, the 64
    // last are kept.
    constexpr std::size_t kLine = (kMostUnheardBytes / 64) - kUnheardLineCost;
    constexpr std::size_t kAsked = 35;
    std::size_t said = 0;
    const Endpoint kEndpoint{[&said](std::string_view, std::int64_t id) {
        std::string lines = saidLine(said, kLine) + saidLine(said + 1, kLine);
        said += 2;
        return lines + replyLine(id, R"({"kind":"test.done"})");
    }};
    const auto kLink = kEndpoint.link();
    if (kLink == nullptr) {
        return;
    }
    const auto kAskAll = [&kLink] {
        bool all = true;
        for (std::size_t each = 0; each < kAsked; ++each) {
            all = kLink->ask(R"({"kind":"test.say","id":1})").has_value() && all;
        }
        return all;
    };
    RAWFRAME_EXPECT(kAskAll());
    const std::vector<std::string> kHeard = kLink->heard();
    RAWFRAME_EXPECT(kHeard.size() == 64);
    RAWFRAME_EXPECT(!kHeard.empty() && kHeard.front() + "\n" == saidLine(6, kLine));
    RAWFRAME_EXPECT(!kHeard.empty() && kHeard.back() + "\n" == saidLine(69, kLine));
    // Taken, they are no longer counted: the next 70 again leave 64.
    RAWFRAME_EXPECT(kAskAll());
    const std::vector<std::string> kNext = kLink->heard();
    RAWFRAME_EXPECT(kNext.size() == 64 && kNext.front() + "\n" == saidLine(76, kLine));
}
