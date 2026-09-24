#pragma once

#include "rawframe/diagnostics/router.h"
#include "rawframe/diagnostics/sink.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::diagnostics {

// SPEC-0047 ceilings, all hard ceilings enforced by truncation or refusal.
inline constexpr std::size_t kMaximumRecordLineBytes = 8192;
inline constexpr std::size_t kMaximumFieldCount = 32;
inline constexpr std::size_t kMaximumStringValueBytes = 512;
inline constexpr std::size_t kMaximumMessageBytes = 1024;
inline constexpr std::size_t kMaximumTruncatedEntries = 8;

/// What the stream header records once per file (SPEC-0047, the stream header).
struct StreamInfo {
    std::string_view processIdentity;
    std::int64_t processStartWallUnixNanoseconds = 0;
    std::string_view buildReceipt;
    std::string_view profile;
    /// `debug`, `development`, or `shipping`, checked at construction; written
    /// as `build.<name>`.
    std::string_view configuration;
    std::string_view targetRole;
    std::string_view session;
};

/// Called by drain with finished bytes. Returns false when the destination
/// failed; the sink then keeps nothing and counts the loss.
using WriteBytes = bool (*)(void* context, std::span<const char> bytes) noexcept;

/// Writes SPEC-0047 newline-delimited records. `accept` serializes, truncates,
/// and redacts on the emitting thread into a bounded buffer and never performs
/// I/O; `drain` hands the finished bytes to a writer. A full buffer drops the
/// newest record and counts it; the count is written as its own record at the
/// next drain.
class NdjsonSink final : public Sink {
public:
    /// `bufferBytes` is allocated once, here. `clearance` is the most sensitive
    /// record this sink's destination may hold; anything above is refused and
    /// counted (ADR-0063).
    NdjsonSink(StreamInfo stream,
               Sensitivity clearance,
               std::size_t bufferBytes,
               MonotonicClock clock = &steadyClockNanoseconds);

    void accept(const Record& record) noexcept override;

    /// Writes everything accepted so far, the stream header first on the first
    /// drain. Returns false if the writer failed.
    bool drain(WriteBytes write, void* context) noexcept;

    /// The next drain writes to a new file, so it starts with the stream header
    /// again (SPEC-0047: the first line after rotation is a stream record).
    void startNewFile() noexcept;

    [[nodiscard]] std::uint64_t droppedRecords() const noexcept;
    [[nodiscard]] std::uint64_t refusedRecords() const noexcept;

private:
    StreamInfo stream_;
    Sensitivity clearance_;
    MonotonicClock clock_;
    mutable std::mutex mutex_;
    std::vector<char> pending_;    // filled by accept, capacity fixed at construction
    std::vector<char> draining_;   // swapped with pending_ by drain, written outside the lock
    std::uint64_t dropped_ = 0;    // total, under mutex_
    std::uint64_t unreported_ = 0; // dropped since the last drain reported it
    std::uint64_t refused_ = 0;
    std::uint64_t refusedUnreported_ = 0;
    bool headerWritten_ = false;
    std::uint64_t fileStartNanoseconds_ = 0; // the header's time: when the file began
};

/// Serializes one record as a SPEC-0047 line, including its final LF, into
/// `destination`, and returns the byte count, or zero if the record is refused
/// (its sensitivity exceeds `clearance`). `destination` must hold
/// kMaximumRecordLineBytes. Exposed for tests and for sinks that write
/// elsewhere.
[[nodiscard]] std::size_t
serializeRecord(const Record& record, Sensitivity clearance, std::span<char> destination) noexcept;

} // namespace rawframe::diagnostics
