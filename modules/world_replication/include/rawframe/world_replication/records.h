#pragma once

// Replication payloads, generation 1 (SPEC-0010 mapping protocol, SPEC-0041
// input window and state header). Mapping frames ride the control stream;
// state and input ride their datagram lanes. Every decoder is exact.

#include "rawframe/network/wire.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::world_replication {

/// A connection-local name for a replicated entity: never zero, never reused
/// within its replication epoch, never an EntityHandle's bits.
struct NetEntityId {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }
    friend constexpr bool operator==(NetEntityId, NetEntityId) noexcept = default;
    friend constexpr auto operator<=>(NetEntityId, NetEntityId) noexcept = default;
};

/// Payload types on the state lane (server to client).
inline constexpr std::uint64_t kStatePayload = 1;
inline constexpr std::uint64_t kPacePayload = 2;
/// Payload types on the input lane (client to server).
inline constexpr std::uint64_t kInputWindowPayload = 1;
inline constexpr std::uint64_t kStateAckPayload = 2;
inline constexpr std::uint64_t kChecksumPayload = 3;

/// `mapping_declare`, `mapping_ack`, `mapping_retire`, and
/// `mapping_retire_ack` all carry this. `owned` is set only on a declare,
/// for the entity the receiving connection plays.
struct MappingRecord {
    std::uint64_t replicationEpoch = 0;
    NetEntityId entity;
    bool owned = false;
};

[[nodiscard]] result::Status encodeMapping(network::Writer& writer, const MappingRecord& record);
[[nodiscard]] result::Result<MappingRecord> decodeMapping(std::span<const std::byte> payload);

/// The start of every state payload.
struct StateHeader {
    /// The committed tick the state was read at.
    std::uint64_t serverTick = 0;
    /// The newest input tick consumed for this connection by then.
    std::uint64_t consumedInputTick = 0;
    std::uint64_t recordCount = 0;
};

[[nodiscard]] result::Status encodeStateHeader(network::Writer& writer, const StateHeader& header);
[[nodiscard]] result::Result<StateHeader> decodeStateHeader(network::Reader& reader);

/// Each state record: which entity, which row of the replication table, then
/// the component's value in its codec's wire form.
struct StateRecordHead {
    NetEntityId entity;
    std::uint64_t component = 0;
};

[[nodiscard]] result::Status encodeStateRecordHead(network::Writer& writer, const StateRecordHead& head);
[[nodiscard]] result::Result<StateRecordHead> decodeStateRecordHead(network::Reader& reader,
                                                                    std::size_t componentCount);

/// SPEC-0041's pace signal: how far ahead of consumption this connection's
/// newest input arrives, and how far the server wants it.
struct Pace {
    std::int64_t measuredLead = 0;
    std::uint64_t targetLead = 0;
};

[[nodiscard]] result::Status encodePace(network::Writer& writer, const Pace& pace);
[[nodiscard]] result::Result<Pace> decodePace(std::span<const std::byte> payload);

/// The most commands one input window carries, and the most bytes each.
inline constexpr std::size_t kMaximumInputWindow = 16;
inline constexpr std::size_t kMaximumInputCommand = 256;

/// SPEC-0041's input window: the newest input tick, the commands for the
/// consecutive ticks ending there, oldest first, and the state the client
/// has applied.
struct InputWindow {
    std::uint64_t newestInputTick = 0;
    std::uint64_t ackedStateSequence = 0;
    std::uint64_t ackedServerTick = 0;
    std::vector<std::span<const std::byte>> commands;
};

/// Which state datagrams a client received (SPEC-0010's highest sequence and
/// selective bitmap): the newest sequence, and a bit for each of the 64
/// before it, bit i for `latest - 1 - i`. It chooses what the server sends
/// next and is not reliability: a lost acknowledgement costs only a resend.
struct StateAck {
    std::uint64_t latest = 0;
    std::uint64_t earlier = 0;
};

[[nodiscard]] result::Status encodeStateAck(network::Writer& writer, const StateAck& ack);
[[nodiscard]] result::Result<StateAck> decodeStateAck(std::span<const std::byte> payload);

/// SPEC-0041's PerceptionContext: the moment a client showed when it gave a
/// command, as the server tick of the older of the two states it showed
/// between and 65536ths of the way to the next. Follows the command's own
/// bytes in its entry, for a game whose input is perceived.
struct PerceptionContext {
    std::uint64_t baseTick = 0;
    std::uint16_t fraction = 0;
};

/// A perception context's bytes at most: a varint and two bytes.
inline constexpr std::size_t kMaximumPerceptionBytes = 10;

[[nodiscard]] result::Status encodePerception(network::Writer& writer, const PerceptionContext& perception);
/// Everything in `bytes` must be the context.
[[nodiscard]] result::Result<PerceptionContext> decodePerception(std::span<const std::byte> bytes);

/// A claimed moment as the server keeps it (SPEC-0041's perception_skew_max),
/// and whether it was moved.
struct KeptPerception {
    PerceptionContext moment;
    bool clamped = false;
};

/// Keeps a moment claimed by a command that arrived before tick `arrived`
/// within `skew` ticks of `lag`, how far a connection's claims have lagged
/// their arrival, and folds it into `lag` a sixty-fourth at a time: a claim
/// moves it by a sixty-fourth of the skew at most, so a connection lying the
/// same way every tick drags it about six ticks a second at 60 Hz, and one
/// cannot pick a different moment for each shot. The first
/// claim is kept as it is and starts `lag`; a moment of tick nought saw
/// nothing and is kept, touching nothing.
[[nodiscard]] KeptPerception
keepPerception(PerceptionContext claimed, std::uint64_t arrived, double skew, std::optional<double>& lag) noexcept;

/// SPEC-0041's checksum record: a client's hash of its whole predicted scope
/// at a confirmed server tick (checksum.h), and the fingerprint of the scope
/// it hashed.
struct ChecksumRecord {
    std::uint64_t tick = 0;
    std::uint64_t scope = 0;
    std::uint64_t checksum = 0;
};

[[nodiscard]] result::Status encodeChecksum(network::Writer& writer, const ChecksumRecord& record);
[[nodiscard]] result::Result<ChecksumRecord> decodeChecksum(std::span<const std::byte> payload);

[[nodiscard]] result::Status encodeInputWindow(network::Writer& writer, const InputWindow& window);
/// The decoded commands borrow from `payload`.
[[nodiscard]] result::Result<InputWindow> decodeInputWindow(std::span<const std::byte> payload);

} // namespace rawframe::world_replication
