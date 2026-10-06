#pragma once

// SPEC-0020's release records: an immutable ReleaseRecord binding one
// Release to its artifacts' exact digests, and a ChannelPointer saying which
// Release a channel of a subject points at now. Both are canonical records,
// closed schemas, signed detached under SPEC-0019's regime by the subject's
// publisher. A Release's identity is the SHA-256 of its record's exact
// bytes: no name or channel is ever the authority for one. A pointer whose
// sequence is not past the one a client holds is a replay; a higher one
// naming an older Release is a rollback, and accepted (D424).

#include "rawframe/base/sha256.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::release {

/// The media type of a Release's artifact that is a CompositionRecord: its
/// digest the CompositionId, which a launcher installs (D424).
inline constexpr std::string_view kCompositionMediaType = "application/vnd.rawframe.composition";

/// One artifact of a Release: for which platform, of what media type, and
/// its exact bytes by size and digest.
struct Artifact {
    std::string platform;
    std::string mediaType;
    std::uint64_t size = 0;
    base::Sha256Digest digest{};
};

struct ReleaseRecord {
    /// `publisher/name`, ADR-0023's grammar.
    std::string subject;
    /// Semantic Versioning 2.0.0, 1 to 64 bytes.
    std::string version;
    /// Unix seconds.
    std::int64_t createdAt = 0;
    /// 1 to 64.
    std::vector<Artifact> artifacts;
    /// Build receipts and Build or Composition evidence, 0 to 16.
    std::vector<base::Sha256Digest> receipts;
    /// The subject's Release before this one.
    std::optional<base::Sha256Digest> predecessor;
};

enum class Channel : std::uint8_t {
    Stable,
    Beta,
    Nightly
};

[[nodiscard]] std::string_view nameOf(Channel channel) noexcept;
[[nodiscard]] std::optional<Channel> channelNamed(std::string_view name) noexcept;

struct ChannelPointer {
    std::string subject;
    Channel channel = Channel::Stable;
    /// The Release it points at: its record's identity.
    base::Sha256Digest release{};
    /// Strictly increasing per subject and channel.
    std::int64_t sequence = 0;
    /// Unix seconds.
    std::int64_t updatedAt = 0;
};

/// The records' canonical bytes, at most 64 KiB, closed schemas, every
/// value in its grammar and bounds; refused (`RecordInvalid`) otherwise.
[[nodiscard]] result::Result<ReleaseRecord> readRelease(std::string_view text);
[[nodiscard]] result::Result<std::string> writeRelease(const ReleaseRecord& record);
[[nodiscard]] result::Result<ChannelPointer> readPointer(std::string_view text);
[[nodiscard]] result::Result<std::string> writePointer(const ChannelPointer& pointer);

/// A Release's identity: SHA-256 of its record's exact bytes.
[[nodiscard]] base::Sha256Digest releaseIdOf(std::string_view recordText) noexcept;

/// SPEC-0020's update check over records already verified as signed: the
/// pointer is of `subject` and `channel` (`UnknownSubject`), names exactly
/// `recordText` (`DigestMismatch`), whose record is of the same subject,
/// and comes after the sequence held, if any (`SequenceRegression`).
/// Returns the Release.
[[nodiscard]] result::Result<ReleaseRecord> check(const ChannelPointer& pointer,
                                                  std::string_view recordText,
                                                  std::string_view subject,
                                                  Channel channel,
                                                  std::optional<std::int64_t> heldSequence);

} // namespace rawframe::release
