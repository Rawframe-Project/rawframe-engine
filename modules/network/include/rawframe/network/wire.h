#pragma once

// The Rawframe application wire protocol, generation 1 (SPEC-0010): QUIC
// variable-length integers, stream prefaces, stream frames, and datagram
// records. Readers never allocate and never read past their input; what they
// return borrows from it. Every value has exactly one accepted spelling.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rawframe::network {

/// The largest value a QUIC variable-length integer holds (RFC 9000 §16).
inline constexpr std::uint64_t kMaximumVarint = (std::uint64_t{1} << 62U) - 1U;

/// The stream format version of generation 1.
inline constexpr std::uint64_t kStreamFormatVersion = 1;

/// How many bytes the canonical encoding of `value` takes: 1, 2, 4, or 8.
/// `value` must not exceed kMaximumVarint.
[[nodiscard]] constexpr std::size_t varintSize(std::uint64_t value) noexcept {
    if (value < (std::uint64_t{1} << 6U)) {
        return 1;
    }
    if (value < (std::uint64_t{1} << 14U)) {
        return 2;
    }
    if (value < (std::uint64_t{1} << 30U)) {
        return 4;
    }
    return 8;
}

/// Reads values off the front of a byte range.
class Reader {
public:
    explicit Reader(std::span<const std::byte> input) noexcept : input_(input) {
    }

    /// A canonical varint. `truncated` if the input ends inside it,
    /// `non_canonical` if a shorter encoding exists.
    [[nodiscard]] result::Result<std::uint64_t> varint();
    /// A varint no greater than `maximum`, else `oversized`.
    [[nodiscard]] result::Result<std::uint64_t> varintAtMost(std::uint64_t maximum);
    /// The next `count` bytes, borrowed from the input.
    [[nodiscard]] result::Result<std::span<const std::byte>> bytes(std::uint64_t count);

    [[nodiscard]] std::size_t consumed() const noexcept {
        return at_;
    }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return input_.size() - at_;
    }
    [[nodiscard]] std::span<const std::byte> rest() const noexcept {
        return input_.subspan(at_);
    }

private:
    std::span<const std::byte> input_;
    std::size_t at_ = 0;
};

/// Writes values into a fixed byte range. Once a write does not fit, it and
/// every later write fail and nothing more is written.
class Writer {
public:
    explicit Writer(std::span<std::byte> output) noexcept : output_(output) {
    }

    /// The canonical encoding of `value`, which must not exceed
    /// kMaximumVarint (`oversized`).
    [[nodiscard]] result::Status varint(std::uint64_t value);
    [[nodiscard]] result::Status bytes(std::span<const std::byte> data);

    [[nodiscard]] std::span<const std::byte> written() const noexcept {
        return output_.first(at_);
    }

private:
    [[nodiscard]] result::Status room(std::size_t count);

    std::span<std::byte> output_;
    std::size_t at_ = 0;
    bool failed_ = false;
};

enum class StreamKind : std::uint64_t {
    Control = 0,
    EventLane = 1,
    Baseline = 2,
};

/// What begins every application stream: its kind, the format version, and
/// the fields that kind carries.
struct StreamPreface {
    StreamKind kind = StreamKind::Control;
    /// Event lanes: which lane, and which generation of it.
    std::uint64_t eventLaneId = 0;
    std::uint64_t eventLaneEpoch = 0;
    /// Baselines: which baseline, under which replication epoch.
    std::uint64_t baselineSequence = 0;
    std::uint64_t replicationEpoch = 0;
};

[[nodiscard]] result::Result<StreamPreface> readPreface(Reader& reader);
[[nodiscard]] result::Status writePreface(Writer& writer, const StreamPreface& preface);

/// Even frame types are critical; odd ones are extensions a reader may skip
/// once their whole length is in.
[[nodiscard]] constexpr bool criticalFrame(std::uint64_t type) noexcept {
    return (type & 1U) == 0;
}

/// One stream frame, its payload borrowed from the input.
struct Frame {
    std::uint64_t type = 0;
    std::span<const std::byte> payload;
};

/// Reads one frame whose payload is at most `maximumPayload` bytes
/// (`oversized` otherwise, decided before the payload is read). `truncated`
/// means the stream has not delivered the whole frame yet.
[[nodiscard]] result::Result<Frame> readFrame(Reader& reader, std::size_t maximumPayload);
[[nodiscard]] result::Status writeFrame(Writer& writer, std::uint64_t type, std::span<const std::byte> payload);

enum class DatagramLane : std::uint64_t {
    Input = 0,
    State = 1,
};

/// One complete datagram record, its payload borrowed from the datagram.
struct DatagramRecord {
    DatagramLane lane = DatagramLane::Input;
    std::uint64_t laneEpoch = 0;
    std::uint64_t sequence = 0;
    std::uint64_t payloadType = 0;
    std::span<const std::byte> payload;
};

/// Reads a whole datagram as one record: its payload must be at most
/// `maximumPayload` bytes and must end exactly where the datagram does.
[[nodiscard]] result::Result<DatagramRecord> readDatagram(std::span<const std::byte> datagram,
                                                          std::size_t maximumPayload);
[[nodiscard]] result::Status writeDatagram(Writer& writer, const DatagramRecord& record);

} // namespace rawframe::network
