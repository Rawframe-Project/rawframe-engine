#pragma once

// The `graceful_close` payload, generation 1 (SPEC-0010, either direction):
// a side saying it is leaving before it closes, so the other can tell a
// server that stops or a player that quits from a lost connection; and the
// session termination a server sends one player on the engine's event lane
// before it closes that connection (ADR-0073, D267). Decoding is exact, as
// for admission payloads.

#include "rawframe/network/wire.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

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

/// ADR-0073's closed vocabulary: who ended the session.
enum class TerminationReason : std::uint64_t {
    Operator = 1,
    /// The game's own trusted code.
    Game = 2,
};

inline constexpr std::uint64_t kLastTerminationReason = static_cast<std::uint64_t>(TerminationReason::Game);
/// The display note's bytes at most.
inline constexpr std::size_t kMaximumTerminationNote = 256;

struct Termination {
    TerminationReason reason = TerminationReason::Game;
    /// Words for the player, UTF-8 as its sender wrote them; possibly none.
    std::string note;
};

/// The engine's event lane, from the server, and its one message.
inline constexpr std::uint64_t kEngineLane = 0;
inline constexpr std::uint64_t kSessionTerminated = 1;
/// A termination's record at most: its reason and note with their lengths.
inline constexpr std::size_t kMaximumEngineRecord = kMaximumTerminationNote + 16;

[[nodiscard]] result::Status encodeTermination(Writer& writer, const Termination& termination);
[[nodiscard]] result::Result<Termination> decodeTermination(std::span<const std::byte> payload);

} // namespace rawframe::network
