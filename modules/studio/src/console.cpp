#include "console.h"

#include "rawframe/document/json.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <utility>

namespace rawframe::studio {

namespace {

using document::Value;

/// The longest record kept waiting for its end; one longer is dropped, so
/// a file with no line ends holds no more than this.
constexpr std::size_t kLongestRecord = 1024 * 1024;

std::optional<std::int64_t> numberOf(std::string_view digits) {
    std::int64_t value = 0;
    const auto [kEnd, kError] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (kError != std::errc{} || kEnd != digits.data() + digits.size() || digits.empty()) {
        return std::nullopt;
    }
    return value;
}

std::string_view textOf(const Value* value) {
    return value != nullptr && value->text() != nullptr ? std::string_view{*value->text()} : std::string_view{};
}

} // namespace

std::optional<Diagnostic> gamePlaceIn(std::string_view text) {
    for (std::size_t at = text.find("game/"); at != std::string_view::npos; at = text.find("game/", at + 1)) {
        // `game/<path>:<line>:<column>`, the path ending at its first colon.
        const std::size_t kLineAt = text.find(':', at);
        const std::size_t kColumnAt = kLineAt == std::string_view::npos ? kLineAt : text.find(':', kLineAt + 1);
        if (kColumnAt == std::string_view::npos) {
            return std::nullopt;
        }
        std::size_t columnEnd = kColumnAt + 1;
        while (columnEnd < text.size() && text[columnEnd] >= '0' && text[columnEnd] <= '9') {
            ++columnEnd;
        }
        const auto kLine = numberOf(text.substr(kLineAt + 1, kColumnAt - kLineAt - 1));
        const auto kColumn = numberOf(text.substr(kColumnAt + 1, columnEnd - kColumnAt - 1));
        if (kLine.has_value() && kColumn.has_value()) {
            return Diagnostic{.file = std::string{text.substr(at, kLineAt - at)},
                              .line = *kLine,
                              .column = *kColumn,
                              .message = std::string{text.substr(std::min(columnEnd + 2, text.size()))}};
        }
    }
    return std::nullopt;
}

std::optional<ConsoleLine> consoleLineOf(std::string_view record, ConsoleLine::Source source) {
    const auto kParsed = document::parse(record);
    if (!kParsed.has_value() || textOf(kParsed->find("kind")) != "log") {
        return std::nullopt;
    }
    const std::string_view kSeverity = textOf(kParsed->find("severity"));
    ConsoleLine line{.source = source,
                     .level = kSeverity == "error" || kSeverity == "fatal" ? ConsoleLine::Level::Error
                              : kSeverity == "warning"                     ? ConsoleLine::Level::Warning
                                                                           : ConsoleLine::Level::Info};
    line.text = std::string{textOf(kParsed->find("code"))} + ": " + std::string{textOf(kParsed->find("message"))};
    // What went wrong, said in its fields, after what the record is; and a
    // place in the game's own files any of them names.
    if (const Value* fields = kParsed->find("fields"); fields != nullptr) {
        for (const std::string_view kName : {"error", "report", "diagnostic", "status"}) {
            const std::string_view kSaid = textOf(fields->find(kName));
            if (kSaid.empty()) {
                continue;
            }
            if (line.text.size() < 400) {
                line.text += "; " + std::string{kSaid.substr(0, 400)};
            }
            if (!line.at.has_value()) {
                line.at = gamePlaceIn(kSaid);
            }
        }
    }
    return line;
}

LogTail::LogTail(std::filesystem::path file, ConsoleLine::Source source) : file_(std::move(file)), source_(source) {
}

std::vector<ConsoleLine> LogTail::read(std::size_t most) {
    std::vector<ConsoleLine> lines;
    std::error_code error;
    const std::uintmax_t kSize = std::filesystem::file_size(file_, error);
    if (error) {
        return lines;
    }
    // A file written again from its start, by the next game played.
    if (kSize < offset_) {
        offset_ = 0;
        partial_.clear();
    }
    if (kSize == offset_) {
        return lines;
    }
    std::FILE* file = std::fopen(file_.string().c_str(), "rb");
    if (file == nullptr) {
        return lines;
    }
    std::string read(static_cast<std::size_t>(std::min<std::uintmax_t>(kSize - offset_, most)), '\0');
    if (std::fseek(file, static_cast<long>(offset_), SEEK_SET) == 0) {
        read.resize(std::fread(read.data(), 1, read.size(), file));
        offset_ += read.size();
    } else {
        read.clear();
    }
    std::fclose(file);
    partial_ += read;
    std::size_t start = 0;
    for (std::size_t end = partial_.find('\n'); end != std::string::npos; end = partial_.find('\n', start)) {
        if (auto line = consoleLineOf(std::string_view{partial_}.substr(start, end - start), source_)) {
            lines.push_back(std::move(*line));
        }
        start = end + 1;
    }
    partial_.erase(0, start);
    if (partial_.size() > kLongestRecord) {
        partial_.clear();
    }
    return lines;
}

} // namespace rawframe::studio
