// The debug adapter for a running game (ADR-0066's debugger bridge, D461):
// an editor starts it and speaks the Debug Adapter Protocol on its standard
// input and output, and it attaches to a game's tooling endpoint, which
// grants debug (D460). VS Code starts it through tools/vscode.
//
//   rawframe-debug
//
// Messages are framed as the protocol frames them: a Content-Length header,
// a blank line, and that many bytes of JSON.

#include "adapter.h"
#include "rawframe/document/json.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {

using rawframe::document::Value;

/// A message's bytes at most, either way.
constexpr std::size_t kMostMessage = std::size_t{1} << 20U;

/// The messages read from standard input, by a thread of their own, so the
/// game is asked how it is while the editor is quiet.
struct Inbox {
    std::mutex mutex;
    std::condition_variable arrived;
    std::deque<std::string> messages;
    bool closed = false;
};

/// One framed message from standard input; none at its end or past the
/// limit.
std::optional<std::string> readMessage() {
    std::size_t length = 0;
    bool sized = false;
    std::string line;
    for (int each = std::fgetc(stdin); each != EOF; each = std::fgetc(stdin)) {
        if (each != '\n') {
            line.push_back(static_cast<char>(each));
            continue;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            if (!sized || length > kMostMessage) {
                return std::nullopt;
            }
            std::string body(length, '\0');
            if (std::fread(body.data(), 1, length, stdin) != length) {
                return std::nullopt;
            }
            return body;
        }
        constexpr std::string_view kHeader = "Content-Length:";
        if (line.starts_with(kHeader)) {
            length = std::strtoull(line.c_str() + kHeader.size(), nullptr, 10);
            sized = true;
        }
        line.clear();
    }
    return std::nullopt;
}

void send(const Value& message) {
    const std::string kBody = rawframe::document::writeCompact(message);
    std::printf("Content-Length: %zu\r\n\r\n%s", kBody.size(), kBody.c_str());
    std::fflush(stdout);
}

} // namespace

int main() {
    Inbox inbox;
    std::thread reader([&inbox] {
        for (;;) {
            std::optional<std::string> message = readMessage();
            const std::scoped_lock kLock{inbox.mutex};
            if (!message.has_value()) {
                inbox.closed = true;
                inbox.arrived.notify_one();
                return;
            }
            inbox.messages.push_back(std::move(*message));
            inbox.arrived.notify_one();
        }
    });
    reader.detach();
    rawframe::debug_adapter::Adapter adapter;
    // The game is asked how it is four times a second, between requests.
    constexpr auto kPollEvery = std::chrono::milliseconds{250};
    auto nextPoll = std::chrono::steady_clock::now();
    while (!adapter.done()) {
        std::optional<std::string> message;
        {
            std::unique_lock lock{inbox.mutex};
            inbox.arrived.wait_until(lock, nextPoll, [&inbox] {
                return !inbox.messages.empty() || inbox.closed;
            });
            if (!inbox.messages.empty()) {
                message = std::move(inbox.messages.front());
                inbox.messages.pop_front();
            } else if (inbox.closed) {
                break;
            }
        }
        if (message.has_value()) {
            auto request = rawframe::document::parse(*message);
            if (request.has_value() && request->kind() == Value::Kind::Object) {
                for (const Value& each : adapter.handle(*request)) {
                    send(each);
                }
            }
        }
        if (std::chrono::steady_clock::now() >= nextPoll) {
            nextPoll = std::chrono::steady_clock::now() + kPollEvery;
            for (const Value& each : adapter.poll()) {
                send(each);
            }
        }
    }
    for (const Value& each : adapter.poll()) {
        send(each);
    }
    // Ended without the C library's cleanup, which would wait for standard
    // input, held by the reader in a read that may never return.
    std::fflush(stdout);
    std::_Exit(0);
}
