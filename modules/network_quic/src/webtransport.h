#pragma once

// The server side of WebTransport over HTTP/3 (ADR-0084 decision 3, D172):
// what one QUIC connection that negotiated `h3` carries, turned into one
// Rawframe connection. Pure: it reads the bytes MsQuic delivered and says
// what to send and what the owner hears; quic.cpp moves the bytes.
//
// One session per connection. The session is the client's extended CONNECT
// (RFC 9220, `:protocol webtransport`); inside it, the client's streams
// start with their WebTransport preface (0x41 or 0x54, then the session)
// and its datagrams with the session's quarter stream ID (RFC 9297). What
// the owner sees is only what follows: the same bytes a native QUIC
// connection would have carried, on the same QUIC stream numbers.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::network_quic {

/// HTTP/3 error codes this side closes a connection with (RFC 9114).
inline constexpr std::uint64_t kH3NoError = 0x100;
inline constexpr std::uint64_t kH3StreamCreationError = 0x103;
inline constexpr std::uint64_t kH3FrameUnexpected = 0x105;
inline constexpr std::uint64_t kH3FrameError = 0x106;
inline constexpr std::uint64_t kH3ExcessiveLoad = 0x107;
inline constexpr std::uint64_t kH3MissingSettings = 0x10a;

class WebTransportServer {
public:
    /// The most bytes a request's frame, or its decoded fields, may take.
    static constexpr std::size_t kLargestRequest = 16 * 1024;

    /// What one delivery on a stream came to. Any of it may be empty.
    struct Arrived {
        /// Bytes to send back on the same stream: the response to the
        /// session's request.
        std::vector<std::byte> reply;
        /// The session was accepted with this delivery.
        bool opened = false;
        /// The owner's bytes on this stream.
        std::vector<std::byte> bytes;
        /// The session ended: its request stream finished.
        bool closed = false;
        /// The peer broke HTTP/3: close the connection with this code.
        std::optional<std::uint64_t> failure;
    };

    /// The first bytes of this side's control stream: its type and the
    /// SETTINGS that say it speaks extended CONNECT, HTTP datagrams, and
    /// WebTransport.
    [[nodiscard]] static std::vector<std::byte> controlStream();

    /// Bytes that arrived on the stream with QUIC number `stream`, and
    /// whether it finished with them.
    [[nodiscard]] Arrived receive(std::uint64_t stream, std::span<const std::byte> bytes, bool finished);

    /// A datagram's payload for the session, or nothing to drop it.
    [[nodiscard]] std::optional<std::span<const std::byte>> datagram(std::span<const std::byte> datagram) const;

    /// What starts a stream this side opens in the session, and each
    /// datagram it sends. Only once the session is open.
    [[nodiscard]] std::vector<std::byte> streamPreface(bool unidirectional) const;
    [[nodiscard]] std::vector<std::byte> datagramPrefix() const;

    [[nodiscard]] bool open() const noexcept {
        return session_.has_value() && !closed_;
    }

private:
    enum class Phase : std::uint8_t {
        /// Waiting for a stream's type, or a request stream's first frame.
        Start,
        /// The peer's control stream: frames, SETTINGS first.
        Control,
        /// The session's request stream after its response: capsules,
        /// which this side reads and drops; the session ends with it.
        Session,
        /// A stream of the session: the owner's bytes.
        Owner,
        /// Bytes nothing here reads.
        Ignored,
    };

    struct StreamState {
        Phase phase = Phase::Start;
        std::vector<std::byte> pending;
        bool settingsSeen = false;
    };

    void start(std::uint64_t stream, StreamState& state, Arrived& arrived);
    void control(StreamState& state, Arrived& arrived);
    void request(std::uint64_t stream, StreamState& state, Arrived& arrived);

    std::map<std::uint64_t, StreamState> streams_;
    std::optional<std::uint64_t> session_;
    bool controlSeen_ = false;
    bool closed_ = false;
};

} // namespace rawframe::network_quic
