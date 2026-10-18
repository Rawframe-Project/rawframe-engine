#include "line_client.h"

#include "rawframe/document/json.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <system_error>
#include <thread>
#include <utility>

namespace rawframe::authoring_session {

namespace {

/// How long a reply may take before the client gives up on the endpoint.
constexpr auto kReplyWithin = std::chrono::seconds(10);

/// A pin or token file's bytes at most, as the endpoint's token file.
constexpr std::size_t kMaximumLineFile = 4096;

/// Whether a line is a reply, of either protocol, rather than something
/// said unasked.
bool replies(const document::Value& line) {
    const document::Value* kKind = line.find("kind");
    return kKind != nullptr && kKind->kind() == document::Value::Kind::String &&
           (*kKind->text() == "authoring.reply" || *kKind->text() == "tooling.reply");
}

} // namespace

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

bool LineClient::connected(std::string_view endpoint) {
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

std::optional<std::string> LineClient::ask(std::string_view record) {
    // Each question numbered afresh, and only the reply with its number
    // taken: one that came after its asker stopped waiting is left behind,
    // not read as the next question's answer (D467).
    const auto kParsed = document::parse(record);
    const bool kNumbered = kParsed.has_value() && kParsed->kind() == document::Value::Kind::Object;
    const std::int64_t kId = ++asked_;
    std::string line{record};
    if (kNumbered) {
        document::Value numbered = document::Value::object();
        for (const std::string& kName : kParsed->names()) {
            if (kName != "id") {
                numbered.add(kName, *kParsed->find(kName));
            }
        }
        numbered.add("id", document::Value::integer(kId));
        line = document::writeCompact(numbered);
    }
    line += '\n';
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(line.data()), line.size()};
    if (!provider_.send(connection_, stream_, kBytes).has_value()) {
        return std::nullopt;
    }
    const auto kUntil = std::chrono::steady_clock::now() + kReplyWithin;
    while (std::chrono::steady_clock::now() < kUntil) {
        pump();
        if (auto reply = take(kNumbered ? std::optional{kId} : std::nullopt); reply.has_value()) {
            if (!kNumbered) {
                return reply;
            }
            // Answered under the caller's own id, where its record had
            // one, in its place.
            const auto kReply = document::parse(*reply);
            const document::Value* kAsked = kParsed->find("id");
            document::Value answered = document::Value::object();
            for (const std::string& kName : kReply->names()) {
                if (kName != "id") {
                    answered.add(kName, *kReply->find(kName));
                } else if (kAsked != nullptr) {
                    answered.add(kName, *kAsked);
                }
            }
            return document::writeCompact(answered);
        }
        if (closed_) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

std::vector<std::string> LineClient::heard(std::int64_t withinMilliseconds) {
    const auto kUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(withinMilliseconds);
    for (;;) {
        pump();
        static_cast<void>(take(std::nullopt));
        if (!unasked_.empty() || closed_ || std::chrono::steady_clock::now() >= kUntil) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::exchange(unasked_, {});
}

std::optional<std::string> LineClient::take(std::optional<std::int64_t> id) {
    for (std::size_t end = received_.find('\n'); end != std::string::npos; end = received_.find('\n')) {
        std::string line = received_.substr(0, end);
        received_.erase(0, end + 1);
        const auto kLine = document::parse(line);
        if (!kLine.has_value() || !replies(*kLine)) {
            unasked_.push_back(std::move(line));
            continue;
        }
        const document::Value* kAnswers = kLine->find("id");
        if (!id.has_value() || (kAnswers != nullptr && kAnswers->integer() == *id)) {
            return line;
        }
    }
    return std::nullopt;
}

void LineClient::pump() {
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

} // namespace rawframe::authoring_session
