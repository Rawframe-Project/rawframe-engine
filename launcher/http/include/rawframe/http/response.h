#pragma once

// Reading an HTTP/1.1 response from a server no one vouches for: a status
// line, at most 64 KiB of headers, and a body by Content-Length, by chunks,
// or to the connection's end. Whatever is out of form is refused, never
// guessed at: a message that says its length twice, or two ways, is refused
// (RFC 9112 section 6.3, the smuggling rule).

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rawframe::http {

/// Headers at most, in bytes, status line included.
inline constexpr std::size_t kMaximumHeaderBytes = 64 * 1024;

struct Response {
    int status = 0;
    /// The Location header, when there is one.
    std::string location;
    /// Whether the connection may carry another request.
    bool keepAlive = false;
    std::vector<std::byte> body;
};

class ResponseReader {
public:
    /// A body longer than `ceiling` bytes is refused (`TooLarge`). `head`:
    /// the answer to a request that has no body.
    explicit ResponseReader(std::uint64_t ceiling, bool head = false) noexcept;

    /// Reads what came; returns how many bytes of it belong to this
    /// response, the rest being the next one's. Refuses (`Malformed`,
    /// `TooLarge`) what is out of form; after a refusal, read refuses again.
    [[nodiscard]] result::Result<std::size_t> read(std::span<const std::byte> bytes);

    /// The connection ended: for a body read to the end, the response is
    /// whole; otherwise refused (`Interrupted`).
    [[nodiscard]] result::Status ended();

    [[nodiscard]] bool whole() const noexcept {
        return stage_ == Stage::Whole;
    }

    /// The response, once whole.
    [[nodiscard]] Response& response() noexcept {
        return response_;
    }

private:
    enum class Stage : std::uint8_t {
        Head,
        Sized,
        ChunkSize,
        ChunkData,
        ChunkEnd,
        Trailers,
        ToEnd,
        Whole,
        Refused
    };

    [[nodiscard]] result::Status headDone();
    [[nodiscard]] result::Status line(std::string_view text);
    [[nodiscard]] result::Status grow(std::span<const std::byte> bytes);

    std::uint64_t ceiling_;
    bool head_;
    Stage stage_ = Stage::Head;
    std::string pending_;
    std::uint64_t remaining_ = 0;
    std::size_t headerBytes_ = 0;
    Response response_;
    // What the head said, as it is read.
    bool sized_ = false;
    std::uint64_t length_ = 0;
    bool chunked_ = false;
    bool close_ = false;
    bool keepAliveAsked_ = false;
    bool oldVersion_ = false;
};

} // namespace rawframe::http
