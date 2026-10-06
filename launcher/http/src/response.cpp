#include "rawframe/http/response.h"

#include "rawframe/http/errors.h"

#include <algorithm>

namespace rawframe::http {

namespace {

/// A chunk's size line at most.
constexpr std::size_t kMaximumChunkLine = 4096;
/// What a body's buffer takes before its bytes come, at most.
constexpr std::uint64_t kMaximumReserve = 64ULL * 1024 * 1024;

std::unexpected<result::Error> refused(HttpError error, std::string_view why) {
    const result::ErrorClass kClass =
        error == HttpError::Interrupted ? result::ErrorClass::Unavailable : result::ErrorClass::InvalidArgument;
    return std::unexpected<result::Error>{result::fail(kClass, kHttpDomain, code(error), why).error()};
}

char lower(char letter) noexcept {
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

bool same(std::string_view one, std::string_view other) noexcept {
    return one.size() == other.size() && std::ranges::equal(one, other, [](char first, char second) {
               return lower(first) == lower(second);
           });
}

bool token(char letter) noexcept {
    constexpr std::string_view kMarks = "!#$%&'*+-.^_`|~";
    return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') ||
           kMarks.find(letter) != std::string_view::npos;
}

std::string_view trimmed(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// A decimal count, all digits, without overflow.
bool decimal(std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty() || text.size() > 18) {
        return false;
    }
    value = 0;
    for (const char kDigit : text) {
        if (kDigit < '0' || kDigit > '9') {
            return false;
        }
        value = (value * 10) + static_cast<std::uint64_t>(kDigit - '0');
    }
    return true;
}

/// The comma-separated tokens of a header's value, each checked by `each`.
template <typename Each> void tokens(std::string_view value, Each&& each) {
    while (!value.empty()) {
        const std::size_t kComma = value.find(',');
        each(trimmed(value.substr(0, kComma)));
        value = kComma == std::string_view::npos ? std::string_view{} : value.substr(kComma + 1);
    }
}

} // namespace

ResponseReader::ResponseReader(std::uint64_t ceiling, bool head) noexcept : ceiling_(ceiling), head_(head) {
}

result::Result<std::size_t> ResponseReader::read(std::span<const std::byte> bytes) {
    if (stage_ == Stage::Refused) {
        return refused(HttpError::Malformed, "the response was already refused");
    }
    std::size_t at = 0;
    while (at < bytes.size() && stage_ != Stage::Whole) {
        const auto kLetter = static_cast<char>(bytes[at]);
        result::Status done;
        switch (stage_) {
        case Stage::Head:
        case Stage::Trailers:
        case Stage::ChunkSize:
        case Stage::ChunkEnd: {
            ++at;
            if (stage_ == Stage::Head || stage_ == Stage::Trailers) {
                if (++headerBytes_ > kMaximumHeaderBytes) {
                    done = refused(HttpError::Malformed, "a response's headers are at most 64 KiB");
                    break;
                }
            } else if (pending_.size() >= kMaximumChunkLine) {
                done = refused(HttpError::Malformed, "a chunk's size line is at most 4 KiB");
                break;
            }
            if (kLetter != '\n') {
                pending_.push_back(kLetter);
                break;
            }
            if (!pending_.empty() && pending_.back() == '\r') {
                pending_.pop_back();
            }
            const std::string kLine = std::move(pending_);
            pending_.clear();
            done = line(kLine);
            break;
        }
        case Stage::Sized:
        case Stage::ChunkData: {
            const auto kTaken = static_cast<std::size_t>(std::min<std::uint64_t>(remaining_, bytes.size() - at));
            done = grow(bytes.subspan(at, kTaken));
            at += kTaken;
            remaining_ -= kTaken;
            if (done.has_value() && remaining_ == 0) {
                stage_ = stage_ == Stage::Sized ? Stage::Whole : Stage::ChunkEnd;
            }
            break;
        }
        case Stage::ToEnd:
            done = grow(bytes.subspan(at));
            at = bytes.size();
            break;
        case Stage::Whole:
        case Stage::Refused:
            break;
        }
        if (!done.has_value()) {
            stage_ = Stage::Refused;
            return std::unexpected<result::Error>{std::move(done).error()};
        }
    }
    return at;
}

result::Status ResponseReader::ended() {
    if (stage_ == Stage::ToEnd) {
        stage_ = Stage::Whole;
    }
    if (stage_ != Stage::Whole) {
        stage_ = Stage::Refused;
        return refused(HttpError::Interrupted, "the connection ended before the response was whole");
    }
    return {};
}

result::Status ResponseReader::line(std::string_view text) {
    switch (stage_) {
    case Stage::ChunkEnd:
        if (!text.empty()) {
            return refused(HttpError::Malformed, "a chunk's data ends its line");
        }
        stage_ = Stage::ChunkSize;
        return {};
    case Stage::ChunkSize: {
        const std::string_view kSize = text.substr(0, text.find(';'));
        if (kSize.empty() || kSize.size() > 15) {
            return refused(HttpError::Malformed, "a chunk's size is 1 to 15 hexadecimal digits");
        }
        std::uint64_t size = 0;
        for (const char kDigit : kSize) {
            const char kLower = lower(kDigit);
            const bool kNumber = kLower >= '0' && kLower <= '9';
            if (!kNumber && (kLower < 'a' || kLower > 'f')) {
                return refused(HttpError::Malformed, "a chunk's size is hexadecimal");
            }
            size = (size * 16) + static_cast<std::uint64_t>(kNumber ? kLower - '0' : kLower - 'a' + 10);
        }
        if (size > ceiling_ - std::min<std::uint64_t>(ceiling_, response_.body.size())) {
            return refused(HttpError::TooLarge, "the body is longer than its ceiling");
        }
        remaining_ = size;
        stage_ = size == 0 ? Stage::Trailers : Stage::ChunkData;
        return {};
    }
    case Stage::Trailers:
        // Trailers are read and set aside: nothing here needs one.
        if (text.empty()) {
            stage_ = Stage::Whole;
        }
        return {};
    default:
        break;
    }
    // The head.
    if (response_.status == 0) {
        // HTTP/1.x SSS reason
        if (text.size() < 12 || !(text.starts_with("HTTP/1.1 ") || text.starts_with("HTTP/1.0 ")) ||
            (text.size() > 12 && text[12] != ' ')) {
            return refused(HttpError::Malformed, "a response starts HTTP/1.1 and a status");
        }
        std::uint64_t status = 0;
        if (!decimal(text.substr(9, 3), status) || status < 100 || status > 599) {
            return refused(HttpError::Malformed, "a status is 100 to 599");
        }
        response_.status = static_cast<int>(status);
        sized_ = false;
        chunked_ = false;
        close_ = false;
        keepAliveAsked_ = false;
        oldVersion_ = text[7] == '0';
        return {};
    }
    if (text.empty()) {
        return headDone();
    }
    const std::size_t kColon = text.find(':');
    if (kColon == std::string_view::npos || kColon == 0 || !std::ranges::all_of(text.substr(0, kColon), token)) {
        return refused(HttpError::Malformed, "a header is a name, a colon, and a value");
    }
    const std::string_view kName = text.substr(0, kColon);
    const std::string_view kValue = trimmed(text.substr(kColon + 1));
    if (same(kName, "content-length")) {
        std::uint64_t length = 0;
        if (!decimal(kValue, length) || (sized_ && length != length_)) {
            return refused(HttpError::Malformed, "a response says its length once, in digits");
        }
        sized_ = true;
        length_ = length;
    } else if (same(kName, "transfer-encoding")) {
        if (chunked_ || !same(kValue, "chunked")) {
            return refused(HttpError::Malformed, "a body is sent whole or in chunks, never encoded otherwise");
        }
        chunked_ = true;
    } else if (same(kName, "connection")) {
        tokens(kValue, [this](std::string_view each) {
            close_ = close_ || same(each, "close");
            keepAliveAsked_ = keepAliveAsked_ || same(each, "keep-alive");
        });
    } else if (same(kName, "location")) {
        response_.location = std::string{kValue};
    }
    return {};
}

result::Status ResponseReader::headDone() {
    const int kStatus = response_.status;
    if (kStatus == 101) {
        return refused(HttpError::Malformed, "nothing was asked to switch protocols");
    }
    if (kStatus < 200) {
        // An interim answer: the response proper follows.
        response_ = Response{};
        return {};
    }
    if (sized_ && chunked_) {
        return refused(HttpError::Malformed, "a response says its length two ways");
    }
    response_.keepAlive = !close_ && (!oldVersion_ || keepAliveAsked_);
    if (head_ || kStatus == 204 || kStatus == 304) {
        stage_ = Stage::Whole;
        return {};
    }
    if (chunked_) {
        stage_ = Stage::ChunkSize;
        return {};
    }
    if (sized_) {
        if (length_ > ceiling_) {
            return refused(HttpError::TooLarge, "the body is longer than its ceiling");
        }
        response_.body.reserve(static_cast<std::size_t>(std::min(length_, kMaximumReserve)));
        remaining_ = length_;
        stage_ = length_ == 0 ? Stage::Whole : Stage::Sized;
        return {};
    }
    // Neither: the body runs to the connection's end, which is then spent.
    response_.keepAlive = false;
    stage_ = Stage::ToEnd;
    return {};
}

result::Status ResponseReader::grow(std::span<const std::byte> bytes) {
    if (bytes.size() > ceiling_ - std::min<std::uint64_t>(ceiling_, response_.body.size())) {
        return refused(HttpError::TooLarge, "the body is longer than its ceiling");
    }
    response_.body.insert(response_.body.end(), bytes.begin(), bytes.end());
    return {};
}

} // namespace rawframe::http
