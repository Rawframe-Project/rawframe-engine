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

std::string LineClient::numbered(const document::Value& record, std::int64_t id) {
    document::Value made = document::Value::object();
    for (const std::string& kName : record.names()) {
        if (kName != "id") {
            made.add(kName, *record.find(kName));
        }
    }
    made.add("id", document::Value::integer(id));
    return document::writeCompact(made) + "\n";
}

std::string LineClient::renumbered(const document::Value& line, const document::Value& id) {
    document::Value made = document::Value::object();
    for (const std::string& kName : line.names()) {
        made.add(kName, kName == "id" ? id : *line.find(kName));
    }
    return document::writeCompact(made);
}

std::optional<std::string> LineClient::ask(std::string_view record) {
    return ask(record, std::chrono::duration_cast<std::chrono::milliseconds>(kReplyWithin));
}

std::optional<std::string> LineClient::ask(std::string_view record, std::chrono::milliseconds within) {
    // Each question numbered afresh, and only the reply with its number
    // taken: one that came after its asker stopped waiting is left behind,
    // not read as the next question's answer.
    const auto kParsed = document::parse(record);
    const bool kNumbered = kParsed.has_value() && kParsed->kind() == document::Value::Kind::Object;
    const std::int64_t kId = ++asked_;
    const std::string kLine = kNumbered ? numbered(*kParsed, kId) : std::string{record} + "\n";
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(kLine.data()), kLine.size()};
    if (!provider_.send(connection_, stream_, kBytes).has_value()) {
        return std::nullopt;
    }
    const auto kUntil = std::chrono::steady_clock::now() + within;
    while (std::chrono::steady_clock::now() < kUntil) {
        pump();
        if (auto reply = take(kNumbered ? std::optional{kId} : std::nullopt); reply.has_value()) {
            silent_ = false;
            if (!kNumbered) {
                return reply;
            }
            // Answered under the caller's own id, where its record had
            // one, in its place.
            const document::Value* kAsked = kParsed->find("id");
            return renumbered(*document::parse(*reply), kAsked != nullptr ? *kAsked : document::Value{});
        }
        if (closed_) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    silent_ = !closed_;
    return std::nullopt;
}

bool LineClient::send(std::string_view record) {
    const auto kParsed = document::parse(record);
    if (!kParsed.has_value() || kParsed->kind() != document::Value::Kind::Object) {
        return false;
    }
    const std::int64_t kId = ++asked_;
    const std::string kLine = numbered(*kParsed, kId);
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(kLine.data()), kLine.size()};
    if (!provider_.send(connection_, stream_, kBytes).has_value()) {
        return false;
    }
    const document::Value* kAsked = kParsed->find("id");
    waiting_[kId] = kAsked != nullptr ? *kAsked : document::Value{};
    return true;
}

std::optional<std::int64_t> LineClient::sentAs(const document::Value& id) const {
    const std::string kId = document::writeCompact(id);
    for (const auto& [kNumber, kGiven] : waiting_) {
        if (document::writeCompact(kGiven) == kId) {
            return kNumber;
        }
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
        const document::Value* kAnswers = kLine.has_value() ? kLine->find("id") : nullptr;
        const std::optional<std::int64_t> kNumber = kAnswers != nullptr ? kAnswers->integer() : std::nullopt;
        // What is said of a record sent to be answered later, and its
        // reply, kept under the record's own id.
        if (const auto kWaiting = kNumber.has_value() ? waiting_.find(*kNumber) : waiting_.end();
            kWaiting != waiting_.end()) {
            unasked_.push_back(renumbered(*kLine, kWaiting->second));
            if (replies(*kLine)) {
                waiting_.erase(kWaiting);
            }
            continue;
        }
        if (!kLine.has_value() || !replies(*kLine)) {
            unasked_.push_back(std::move(line));
            continue;
        }
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
