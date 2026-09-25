#pragma once

// The `graceful_close` payload, generation 1 (SPEC-0010, either direction):
// a side saying it is leaving before it closes, so the other can tell a
// server that stops or a player that quits from a lost connection. Decoding
// is exact, as for admission payloads.

#include "rawframe/network/wire.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rawframe::network {

enum class CloseNotice : std::uint64_t {
    /// The server is draining (SPEC-0012): play goes on for now, admission
    /// is closed, and the connection ends when the server stops.
    ServerStopping = 1,
    /// The client is leaving.
    ClientLeaving = 2,
};

inline constexpr std::uint64_t kLastCloseNotice = static_cast<std::uint64_t>(CloseNotice::ClientLeaving);

[[nodiscard]] result::Status encodeGracefulClose(Writer& writer, CloseNotice notice);
[[nodiscard]] result::Result<CloseNotice> decodeGracefulClose(std::span<const std::byte> payload);

} // namespace rawframe::network
