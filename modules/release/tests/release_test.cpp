// SPEC-0020's records (D424): a ReleaseRecord and a ChannelPointer read
// and written byte for byte, every rejection the specification lists
// refused as malformed, and the update check: a pointer naming exactly its
// record accepted, a rollback (an older Release under a higher sequence)
// accepted, and a replayed pointer, another Release's record, and another
// subject's or channel's pointer refused, each by its own code.

#include "rawframe/release/errors.h"
#include "rawframe/release/release.h"
#include "rawframe/test/test.h"

#include <string>

using namespace rawframe;
using namespace rawframe::release;

namespace {

const std::string kDigest = "sha256:" + std::string(64, 'a');
const std::string kOther = "sha256:" + std::string(64, 'b');

/// A Release of studio/stalls with one artifact, written by hand in its
/// canonical form: members in code-point order, no white space.
std::string recordText(std::string_view version = "1.2.0", std::string_view extra = "") {
    return std::string{"{\"artifacts\":[{\"digest\":\""} + kDigest +
           "\",\"media_type\":\"application/vnd.rawframe.composition\",\"platform\":\"any\",\"size\":512}],"
           "\"created_at\":1791000000," +
           std::string{extra} + "\"receipts\":[\"" + kOther + "\"],\"schema\":1,\"subject\":\"studio/stalls\"," +
           "\"version\":\"" + std::string{version} + "\"}";
}

bool refusedAs(const auto& outcome, ReleaseError error) {
    return !outcome.has_value() && outcome.error().domain() == kReleaseDomain && outcome.error().code() == code(error);
}

ChannelPointer pointerTo(std::string_view record, std::int64_t sequence) {
    return ChannelPointer{.subject = "studio/stalls",
                          .channel = Channel::Stable,
                          .release = releaseIdOf(record),
                          .sequence = sequence,
                          .updatedAt = 1791000100};
}

} // namespace

RAWFRAME_TEST(ARecordReadsAndWritesByteForByte) {
    const std::string kText = recordText();
    const auto kRecord = readRelease(kText);
    RAWFRAME_EXPECT(kRecord.has_value());
    if (!kRecord.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kRecord->subject == "studio/stalls" && kRecord->version == "1.2.0" &&
                    kRecord->artifacts.size() == 1 && kRecord->artifacts[0].size == 512 &&
                    kRecord->receipts.size() == 1 && !kRecord->predecessor.has_value());
    RAWFRAME_EXPECT(writeRelease(*kRecord) == kText);
    // With a predecessor, which sorts among the members.
    const std::string kNext = recordText("1.3.0", "\"predecessor\":\"" + kDigest + "\",");
    const auto kFollowing = readRelease(kNext);
    RAWFRAME_EXPECT(kFollowing.has_value() && kFollowing->predecessor.has_value() &&
                    writeRelease(*kFollowing) == kNext);
}

RAWFRAME_TEST(WhatTheGrammarDoesNotHoldIsRefused) {
    const std::string kText = recordText();
    const auto kReplaced = [&kText](std::string_view from, std::string_view to) {
        std::string changed = kText;
        changed.replace(changed.find(from), from.size(), to);
        return changed;
    };
    for (const std::string& kBad : {
             kReplaced("\"schema\":1", "\"schema\":2"),
             kReplaced("\"schema\":1", "\"schema\":1,\"zz\":1"),
             kReplaced("{\"artifacts\"", "{ \"artifacts\""),
             kReplaced("\"size\":512", "\"size\":512.5"),
             kReplaced("\"size\":512", "\"size\":0"),
             kReplaced("studio/stalls", "Studio/stalls"),
             kReplaced("1.2.0", "1.2"),
             kReplaced("\"any\"", "\"Any Platform\""),
             kReplaced("application/vnd.rawframe.composition", "composition"),
             kReplaced(kDigest, "sha256:abc"),
             kReplaced("\"created_at\":1791000000,", ""),
             kReplaced("\"receipts\":[\"" + kOther + "\"]", "\"receipts\":[1]"),
             std::string{"{\"artifacts\":[],"} + kText.substr(kText.find("\"created_at\"")),
             std::string(70 * 1024, ' '),
         }) {
        RAWFRAME_EXPECT(refusedAs(readRelease(kBad), ReleaseError::RecordInvalid));
    }
    // A pointer of a channel past the closed set.
    const std::string kPointer = *writePointer(pointerTo(kText, 3));
    std::string unknown = kPointer;
    unknown.replace(unknown.find("stable"), 6, "canary");
    RAWFRAME_EXPECT(refusedAs(readPointer(unknown), ReleaseError::RecordInvalid));
    RAWFRAME_EXPECT(refusedAs(readPointer(kText), ReleaseError::RecordInvalid));
    RAWFRAME_EXPECT(!channelNamed("canary").has_value() && channelNamed("nightly") == Channel::Nightly);
}

RAWFRAME_TEST(APointerReadsAndWritesByteForByte) {
    const std::string kText = *writePointer(pointerTo(recordText(), 7));
    RAWFRAME_EXPECT(kText.starts_with("{\"channel\":\"stable\",\"release\":\"sha256:"));
    const auto kPointer = readPointer(kText);
    RAWFRAME_EXPECT(kPointer.has_value() && kPointer->sequence == 7 && kPointer->channel == Channel::Stable &&
                    writePointer(*kPointer) == kText);
}

RAWFRAME_TEST(TheUpdateCheckTakesRollbacksAndRefusesReplays) {
    const std::string kOld = recordText("1.2.0");
    const std::string kNew = recordText("1.3.0", "\"predecessor\":\"" + kDigest + "\",");
    // The first check holds no sequence; the next must pass it.
    const auto kFirst = check(pointerTo(kNew, 4), kNew, "studio/stalls", Channel::Stable, std::nullopt);
    RAWFRAME_EXPECT(kFirst.has_value() && kFirst->version == "1.3.0");
    // A rollback: the older Release, under a higher sequence.
    const auto kRolledBack = check(pointerTo(kOld, 5), kOld, "studio/stalls", Channel::Stable, 4);
    RAWFRAME_EXPECT(kRolledBack.has_value() && kRolledBack->version == "1.2.0");
    // A replay of the pointer held, or one before it.
    RAWFRAME_EXPECT(refusedAs(check(pointerTo(kNew, 4), kNew, "studio/stalls", Channel::Stable, 5),
                              ReleaseError::SequenceRegression));
    RAWFRAME_EXPECT(refusedAs(check(pointerTo(kNew, 5), kNew, "studio/stalls", Channel::Stable, 5),
                              ReleaseError::SequenceRegression));
    // A record the pointer does not name.
    RAWFRAME_EXPECT(
        refusedAs(check(pointerTo(kOld, 6), kNew, "studio/stalls", Channel::Stable, 5), ReleaseError::DigestMismatch));
    // Another subject's or channel's pointer.
    RAWFRAME_EXPECT(
        refusedAs(check(pointerTo(kNew, 6), kNew, "studio/runners", Channel::Stable, 5), ReleaseError::UnknownSubject));
    RAWFRAME_EXPECT(
        refusedAs(check(pointerTo(kNew, 6), kNew, "studio/stalls", Channel::Beta, 5), ReleaseError::UnknownSubject));
}
