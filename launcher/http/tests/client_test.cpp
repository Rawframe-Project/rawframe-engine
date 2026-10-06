// The launcher's client against a server in this process (D414): plain
// HTTP, so the exchange is the client's own; TLS is the scenario's, with
// certificates made for it (launcher/install/tests).

#include "rawframe/http/client.h"
#include "rawframe/http/errors.h"
#include "rawframe/test/test.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace rawframe;

namespace {

#if defined(_WIN32)
using Native = SOCKET;
void closeNative(Native native) {
    ::closesocket(native);
}
int waitFor(Native native, int milliseconds) {
    pollfd polled{native, POLLIN, 0};
    return ::WSAPoll(&polled, 1, milliseconds);
}
#else
using Native = int;
void closeNative(Native native) {
    ::close(native);
}
int waitFor(Native native, int milliseconds) {
    pollfd polled{native, POLLIN, 0};
    return ::poll(&polled, 1, milliseconds);
}
#endif

struct Reply {
    std::string text;
    /// Closes the connection after the reply, saying nothing of it.
    bool close = false;
    /// Never replies, then closes.
    bool silent = false;
};

/// A server on 127.0.0.1 giving `replies` in order, connection after
/// connection, and keeping each request's head.
class Server {
public:
    explicit Server(std::vector<Reply> replies) : replies_(std::move(replies)) {
#if defined(_WIN32)
        WSADATA started{};
        ::WSAStartup(MAKEWORD(2, 2), &started);
#endif
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        ::listen(listener_, 4);
        socklen_t length = sizeof(address);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread{[this] {
            serve();
        }};
    }
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    ~Server() {
        thread_.join();
        closeNative(listener_);
#if defined(_WIN32)
        ::WSACleanup();
#endif
    }

    [[nodiscard]] std::string url(std::string_view path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + std::string{path};
    }
    [[nodiscard]] int accepted() const noexcept {
        return accepted_.load();
    }
    /// After the client is done with it.
    [[nodiscard]] const std::vector<std::string>& requests() const noexcept {
        return requests_;
    }

private:
    void serve() {
        std::size_t next = 0;
        while (next < replies_.size() && waitFor(listener_, 5000) > 0) {
            const Native kConnection = ::accept(listener_, nullptr, nullptr);
            ++accepted_;
            std::string head;
            while (next < replies_.size()) {
                // One request's head.
                char letter = 0;
                while (head.find("\r\n\r\n") == std::string::npos && waitFor(kConnection, 5000) > 0 &&
                       ::recv(kConnection, &letter, 1, 0) == 1) {
                    head.push_back(letter);
                }
                if (head.find("\r\n\r\n") == std::string::npos) {
                    break;
                }
                requests_.push_back(head);
                head.clear();
                const Reply& kReply = replies_[next++];
                if (kReply.silent) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{600});
                    break;
                }
                ::send(kConnection, kReply.text.data(), static_cast<int>(kReply.text.size()), 0);
                if (kReply.close) {
                    break;
                }
            }
            closeNative(kConnection);
        }
    }

    std::vector<Reply> replies_;
    Native listener_{};
    std::uint16_t port_ = 0;
    std::atomic<int> accepted_{0};
    std::vector<std::string> requests_;
    std::thread thread_;
};

std::string textOf(const std::vector<std::byte>& body) {
    return std::string{reinterpret_cast<const char*>(body.data()), body.size()};
}

bool contextSays(const result::Error& error, std::string_view key, std::string_view value) {
    for (const auto& field : error.context()) {
        if (field.key == key && field.value == value) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<http::Client> client(std::uint32_t timeout = 5000, std::uint32_t redirects = 4) {
    auto made = http::Client::create({.timeoutMilliseconds = timeout, .redirects = redirects});
    return made.has_value() ? std::move(*made) : nullptr;
}

} // namespace

RAWFRAME_TEST(GetsShareTheConnectionTheServerKeeps) {
    std::unique_ptr<http::Client> fetching = client();
    RAWFRAME_EXPECT(fetching != nullptr);
    if (fetching == nullptr) {
        return;
    }
    std::string one;
    std::string two;
    {
        Server server{{Reply{.text = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"},
                       Reply{.text = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nworld\r\n0\r\n\r\n"}}};
        const auto kOne = fetching->get(server.url("/a"), 1024);
        const auto kTwo = fetching->get(server.url("/b?c=d"), 1024);
        one = kOne.has_value() ? textOf(*kOne) : "";
        two = kTwo.has_value() ? textOf(*kTwo) : "";
        RAWFRAME_EXPECT(server.accepted() == 1);
        fetching.reset();
        RAWFRAME_EXPECT(server.requests().size() == 2 &&
                        server.requests()[0].starts_with("GET /a HTTP/1.1\r\nHost: 127.0.0.1:") &&
                        server.requests()[1].starts_with("GET /b?c=d HTTP/1.1\r\n"));
    }
    RAWFRAME_EXPECT(one == "hello" && two == "world");
}

RAWFRAME_TEST(AKeptConnectionTheServerClosedIsOpenedAgain) {
    std::unique_ptr<http::Client> fetching = client();
    if (fetching == nullptr) {
        RAWFRAME_EXPECT(false);
        return;
    }
    Server server{{Reply{.text = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none", .close = true},
                   Reply{.text = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo"}}};
    const auto kOne = fetching->get(server.url("/"), 1024);
    // Let the close arrive before the next request goes.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    const auto kTwo = fetching->get(server.url("/"), 1024);
    RAWFRAME_EXPECT(kOne.has_value() && textOf(*kOne) == "one" && kTwo.has_value() && textOf(*kTwo) == "two");
    RAWFRAME_EXPECT(fetching->statistics().connections == 2 && server.accepted() == 2);
}

RAWFRAME_TEST(RedirectsAreFollowedSoFar) {
    std::unique_ptr<http::Client> fetching = client(5000, 1);
    if (fetching == nullptr) {
        RAWFRAME_EXPECT(false);
        return;
    }
    Server server{{Reply{.text = "HTTP/1.1 302 Found\r\nLocation: /moved\r\nContent-Length: 0\r\n\r\n"},
                   Reply{.text = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nhere"},
                   Reply{.text = "HTTP/1.1 301 Moved\r\nLocation: /a\r\nContent-Length: 0\r\n\r\n"},
                   Reply{.text = "HTTP/1.1 301 Moved\r\nLocation: /b\r\nContent-Length: 0\r\n\r\n"}}};
    const auto kMoved = fetching->get(server.url("/"), 1024);
    RAWFRAME_EXPECT(kMoved.has_value() && textOf(*kMoved) == "here");
    // A second redirect in a row is one past this client's one.
    const auto kLoop = fetching->get(server.url("/"), 1024);
    RAWFRAME_EXPECT(!kLoop.has_value() && kLoop.error().code() == http::code(http::HttpError::Status) &&
                    contextSays(kLoop.error(), "status", "301"));
}

RAWFRAME_TEST(AnAnswerOtherThan200IsRefusedWithItsStatus) {
    std::unique_ptr<http::Client> fetching = client();
    if (fetching == nullptr) {
        RAWFRAME_EXPECT(false);
        return;
    }
    Server server{{Reply{.text = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found"},
                   Reply{.text = "HTTP/1.1 200 OK\r\nContent-Length: 2000\r\n\r\n"}}};
    const auto kMissing = fetching->get(server.url("/missing"), 1024);
    RAWFRAME_EXPECT(!kMissing.has_value() && kMissing.error().code() == http::code(http::HttpError::Status) &&
                    contextSays(kMissing.error(), "status", "404"));
    const auto kLarge = fetching->get(server.url("/large"), 1024);
    RAWFRAME_EXPECT(!kLarge.has_value() && kLarge.error().code() == http::code(http::HttpError::TooLarge));
}

RAWFRAME_TEST(ASilentServerOrNoneIsAFailureNotAWait) {
    std::unique_ptr<http::Client> fetching = client(200);
    if (fetching == nullptr) {
        RAWFRAME_EXPECT(false);
        return;
    }
    std::string nobody;
    {
        Server server{{Reply{.silent = true}}};
        const auto kStarted = std::chrono::steady_clock::now();
        const auto kSilent = fetching->get(server.url("/"), 1024);
        RAWFRAME_EXPECT(!kSilent.has_value() && kSilent.error().code() == http::code(http::HttpError::Interrupted) &&
                        std::chrono::steady_clock::now() - kStarted < std::chrono::seconds{3});
        nobody = server.url("/");
    }
    // The server is gone: nothing listens on its port.
    const auto kNobody = fetching->get(nobody, 1024);
    RAWFRAME_EXPECT(!kNobody.has_value() && kNobody.error().code() == http::code(http::HttpError::Unreachable));
}

RAWFRAME_TEST(AuthoritiesThatCannotBeReadAreRefused) {
    const auto kMade = http::Client::create({.authorities = "no/such/authorities.pem"});
    RAWFRAME_EXPECT(!kMade.has_value() && kMade.error().code() == http::code(http::HttpError::NoAuthorities));
    RAWFRAME_EXPECT(!http::parseUrl("https://a.example:99999/").has_value());
}
