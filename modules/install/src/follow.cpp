// SPEC-0020's update check as a library follows a channel (D424): every
// record verified before it is read, the sequence the library holds kept
// only once the Composition it names is installed.

#include "files.h"
#include "rawframe/content/identity.h"
#include "rawframe/content/library.h"
#include "rawframe/content/product.h"
#include "rawframe/install/errors.h"
#include "rawframe/install/installation.h"
#include "rawframe/release/errors.h"
#include "rawframe/signature/signature.h"

#include <algorithm>
#include <charconv>
#include <string>

namespace rawframe::install {

namespace {

/// SPEC-0020's ceiling on its records; an envelope is far smaller.
constexpr std::uint64_t kMaximumRecord = std::uint64_t{64} << 10U;
constexpr std::uint64_t kMaximumEnvelope = 1024;
/// SPEC-0021's ceiling on a CompositionRecord.
constexpr std::uint64_t kMaximumComposition = std::uint64_t{1} << 20U;
constexpr std::uint64_t kMaximumSequence = 32;

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::unexpected<result::Error> refused(release::ReleaseError error, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, release::kReleaseDomain, release::code(error), why).error()};
}

/// The record at `path` of `origin`, its signature beside it verified
/// against `keys` before anything reads it.
result::Result<std::string>
signedRecord(Origin& origin, const std::string& path, const signature::PublisherKeySet& keys) {
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kRecord, origin.record(path, kMaximumRecord));
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kSignature,
                        origin.record(path + std::string{content::kSignatureSuffix}, kMaximumEnvelope));
    RAWFRAME_TRY_ASSIGN(const signature::Envelope kEnvelope, signature::readEnvelope(textOf(kSignature)));
    if (const result::Status kVerified = signature::verifyPublished(keys, kRecord, kEnvelope); !kVerified) {
        return std::unexpected<result::Error>{kVerified.error().clone().withContext("record", path)};
    }
    return std::string{textOf(kRecord)};
}

} // namespace

result::Result<Followed> Installation::follow(std::string_view subject, release::Channel channel, Origin& origin) {
    RAWFRAME_TRY_ASSIGN(const signature::PublisherKeySet kKeys, library_.keys(content::publisherOf(subject)));
    const std::string kPointerPath = content::channelPathOf(subject, release::nameOf(channel));
    RAWFRAME_TRY_ASSIGN(const std::string kPointerText, signedRecord(origin, kPointerPath, kKeys));
    RAWFRAME_TRY_ASSIGN(const release::ChannelPointer kPointer, release::readPointer(kPointerText));
    // The sequence followed last, if this library has followed the channel.
    const std::filesystem::path kSequencePath = root_ / (kPointerPath + std::string{content::kSequenceSuffix});
    std::optional<std::int64_t> held;
    if (std::error_code error; std::filesystem::exists(kSequencePath, error)) {
        RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kHeld,
                            readFile(kSequencePath, kMaximumSequence, "the followed sequence cannot be read"));
        const std::string_view kText = textOf(kHeld);
        std::int64_t value = 0;
        const auto [kEnd, kError] = std::from_chars(kText.data(), kText.data() + kText.size(), value);
        if (kError != std::errc{} || kEnd != kText.data() + kText.size()) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::DataLoss,
                                                               kInstallDomain,
                                                               code(InstallError::LibraryInvalid),
                                                               "the followed sequence is not a number")
                                                      .error()};
        }
        held = value;
    }
    // The pointer the library already follows offers nothing new: a launcher
    // that follows before every play is up to date, not replayed (D434).
    // Nothing is installed or kept, so this is as safe as a refusal.
    if (held == kPointer.sequence) {
        return Followed{.release = kPointer.release, .sequence = kPointer.sequence, .current = true};
    }
    RAWFRAME_TRY_ASSIGN(const std::string kReleaseText,
                        signedRecord(origin, content::releasePathOf(kPointer.release), kKeys));
    RAWFRAME_TRY_ASSIGN(const release::ReleaseRecord kRelease,
                        release::check(kPointer, kReleaseText, subject, channel, held));
    const auto kArtifact =
        std::ranges::find(kRelease.artifacts, release::kCompositionMediaType, &release::Artifact::mediaType);
    if (kArtifact == kRelease.artifacts.end()) {
        return refused(release::ReleaseError::NoArtifact, "the Release names no Composition");
    }
    if (kArtifact->size > kMaximumComposition) {
        return refused(release::ReleaseError::SizeMismatch, "the Release's Composition is past its ceiling");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kComposition,
                        origin.record(content::compositionPathOf(kArtifact->digest), kArtifact->size));
    if (kComposition.size() != kArtifact->size) {
        return refused(release::ReleaseError::SizeMismatch, "the Composition is not the Release's size");
    }
    if (content::compositionIdOf(textOf(kComposition)) != kArtifact->digest) {
        return refused(release::ReleaseError::DigestMismatch, "the Composition is not the Release's digest");
    }
    RAWFRAME_TRY_ASSIGN(const UpdateReport kReport, update(textOf(kComposition), origin));
    const std::string kSequence = std::to_string(kPointer.sequence);
    RAWFRAME_TRY(publishFile(root_ / content::kStagingName / "sequence.part",
                             kSequencePath,
                             std::as_bytes(std::span{kSequence.data(), kSequence.size()})));
    return Followed{
        .version = kRelease.version, .release = kPointer.release, .sequence = kPointer.sequence, .update = kReport};
}

} // namespace rawframe::install
