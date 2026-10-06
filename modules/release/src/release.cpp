#include "rawframe/release/release.h"

#include "rawframe/content/identity.h"
#include "rawframe/content/product.h"
#include "rawframe/document/json.h"
#include "rawframe/release/errors.h"

#include <algorithm>
#include <array>
#include <span>

namespace rawframe::release {

namespace {

using document::Value;

constexpr std::size_t kMaximumRecord = std::size_t{64} * 1024;
constexpr std::size_t kMaximumArtifacts = 64;
constexpr std::size_t kMaximumReceipts = 16;
constexpr std::array<std::string_view, 3> kChannels = {"stable", "beta", "nightly"};

std::unexpected<result::Error> refuse(ReleaseError error, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kReleaseDomain, code(error), why).error()};
}

std::unexpected<result::Error> invalid(std::string_view why) {
    return refuse(ReleaseError::RecordInvalid, why);
}

/// A platform token: 1 to 64 lowercase letters, digits, `_`, `-`, or `.`.
bool platformToken(std::string_view text) noexcept {
    return !text.empty() && text.size() <= 64 && std::ranges::all_of(text, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_' || each == '-' ||
               each == '.';
    });
}

/// A media type: 1 to 128 bytes, `type/subtype`, each of RFC 6838's
/// restricted name characters.
bool mediaType(std::string_view text) noexcept {
    const auto kName = [](std::string_view name) {
        return !name.empty() && std::ranges::all_of(name, [](char each) {
            return (each >= 'a' && each <= 'z') || (each >= 'A' && each <= 'Z') || (each >= '0' && each <= '9') ||
                   std::string_view{"!#$&-^_.+"}.find(each) != std::string_view::npos;
        });
    };
    const std::size_t kSlash = text.find('/');
    return text.size() <= 128 && kSlash != std::string_view::npos && kName(text.substr(0, kSlash)) &&
           kName(text.substr(kSlash + 1));
}

std::optional<base::Sha256Digest> digestOf(const Value* value) {
    if (value == nullptr || value->kind() != Value::Kind::String) {
        return std::nullopt;
    }
    const auto kParsed = content::ContentDigest::parse(*value->text());
    return kParsed.has_value() ? std::optional{kParsed->bytes} : std::nullopt;
}

std::string textOf(const base::Sha256Digest& digest) {
    return content::ContentDigest{.bytes = digest}.text();
}

/// The record's members, exactly `names` (canonical records hold them in
/// order), from text at most 64 KiB in canonical form.
result::Result<Value>
recordOf(std::string_view text, std::span<const std::string_view> required, std::size_t optional) {
    if (text.size() > kMaximumRecord) {
        return invalid("a release record is at most 64 KiB");
    }
    auto parsed = document::parseCanonicalRecord(text, document::ReadLimits{.maximumBytes = kMaximumRecord});
    if (!parsed.has_value() || parsed->kind() != Value::Kind::Object) {
        return invalid("a release record is a canonical record");
    }
    const std::size_t kNames = parsed->names().size();
    if (kNames < required.size() || kNames > required.size() + optional ||
        !std::ranges::all_of(required, [&parsed](std::string_view name) {
            return parsed->find(name) != nullptr;
        })) {
        return invalid("a release record has exactly its schema's members");
    }
    const Value* schema = parsed->find("schema");
    if (schema->integer() != 1) {
        return invalid("a release record's schema is 1");
    }
    return std::move(*parsed);
}

result::Result<Artifact> artifactOf(const Value& value) {
    const Value* platform = value.find("platform");
    const Value* type = value.find("media_type");
    const Value* size = value.find("size");
    const std::optional<base::Sha256Digest> kDigest = digestOf(value.find("digest"));
    if (value.kind() != Value::Kind::Object || value.names().size() != 4 || platform == nullptr ||
        platform->kind() != Value::Kind::String || !platformToken(*platform->text()) || type == nullptr ||
        type->kind() != Value::Kind::String || !mediaType(*type->text()) || size == nullptr ||
        !size->integer().has_value() || *size->integer() <= 0 || !kDigest.has_value()) {
        return invalid("an artifact is {platform, media_type, size, digest} in their grammars, its size positive");
    }
    return Artifact{.platform = *platform->text(),
                    .mediaType = *type->text(),
                    .size = static_cast<std::uint64_t>(*size->integer()),
                    .digest = *kDigest};
}

} // namespace

std::string_view nameOf(Channel channel) noexcept {
    return kChannels[static_cast<std::size_t>(channel)];
}

std::optional<Channel> channelNamed(std::string_view name) noexcept {
    const auto kFound = std::ranges::find(kChannels, name);
    return kFound == kChannels.end() ? std::nullopt : std::optional{static_cast<Channel>(kFound - kChannels.begin())};
}

result::Result<ReleaseRecord> readRelease(std::string_view text) {
    constexpr std::array<std::string_view, 6> kRequired = {
        "schema", "subject", "version", "created_at", "artifacts", "receipts"};
    RAWFRAME_TRY_ASSIGN(const Value kRecord, recordOf(text, kRequired, 1));
    const Value* subject = kRecord.find("subject");
    const Value* version = kRecord.find("version");
    const Value* created = kRecord.find("created_at");
    const Value* artifacts = kRecord.find("artifacts");
    const Value* receipts = kRecord.find("receipts");
    const Value* predecessor = kRecord.find("predecessor");
    if (subject->kind() != Value::Kind::String || !content::validSubject(*subject->text()) ||
        version->kind() != Value::Kind::String || !content::validVersion(*version->text()) ||
        !created->integer().has_value() || artifacts->kind() != Value::Kind::Array || artifacts->items().empty() ||
        artifacts->items().size() > kMaximumArtifacts || receipts->kind() != Value::Kind::Array ||
        receipts->items().size() > kMaximumReceipts || (kRecord.names().size() == 7 && predecessor == nullptr)) {
        return invalid("a ReleaseRecord's subject, version, created_at, 1 to 64 artifacts, and 0 to 16 receipts are "
                       "in their grammars");
    }
    ReleaseRecord made{.subject = *subject->text(), .version = *version->text(), .createdAt = *created->integer()};
    for (const Value& each : artifacts->items()) {
        RAWFRAME_TRY_ASSIGN(Artifact artifact, artifactOf(each));
        made.artifacts.push_back(std::move(artifact));
    }
    for (const Value& each : receipts->items()) {
        const std::optional<base::Sha256Digest> kReceipt = digestOf(&each);
        if (!kReceipt.has_value()) {
            return invalid("a receipt is a digest");
        }
        made.receipts.push_back(*kReceipt);
    }
    if (predecessor != nullptr) {
        made.predecessor = digestOf(predecessor);
        if (!made.predecessor.has_value()) {
            return invalid("a predecessor is a digest");
        }
    }
    return made;
}

result::Result<std::string> writeRelease(const ReleaseRecord& record) {
    Value artifacts = Value::array();
    for (const Artifact& each : record.artifacts) {
        Value artifact = Value::object();
        artifact.add("platform", Value::string(each.platform));
        artifact.add("media_type", Value::string(each.mediaType));
        artifact.add("size", Value::integer(static_cast<std::int64_t>(each.size)));
        artifact.add("digest", Value::string(textOf(each.digest)));
        artifacts.push(std::move(artifact));
    }
    Value receipts = Value::array();
    for (const base::Sha256Digest& each : record.receipts) {
        receipts.push(Value::string(textOf(each)));
    }
    Value value = Value::object();
    value.add("schema", Value::integer(1));
    value.add("subject", Value::string(record.subject));
    value.add("version", Value::string(record.version));
    value.add("created_at", Value::integer(record.createdAt));
    value.add("artifacts", std::move(artifacts));
    value.add("receipts", std::move(receipts));
    if (record.predecessor.has_value()) {
        value.add("predecessor", Value::string(textOf(*record.predecessor)));
    }
    RAWFRAME_TRY_ASSIGN(std::string text, document::writeCanonicalRecord(value));
    // What is written must read back: the grammar is one.
    RAWFRAME_TRY(readRelease(text));
    return text;
}

result::Result<ChannelPointer> readPointer(std::string_view text) {
    constexpr std::array<std::string_view, 6> kRequired = {
        "schema", "subject", "channel", "release", "sequence", "updated_at"};
    RAWFRAME_TRY_ASSIGN(const Value kRecord, recordOf(text, kRequired, 0));
    const Value* subject = kRecord.find("subject");
    const Value* channel = kRecord.find("channel");
    const Value* sequence = kRecord.find("sequence");
    const Value* updated = kRecord.find("updated_at");
    const std::optional<base::Sha256Digest> kRelease = digestOf(kRecord.find("release"));
    const std::optional<Channel> kChannel =
        channel->kind() == Value::Kind::String ? channelNamed(*channel->text()) : std::nullopt;
    if (subject->kind() != Value::Kind::String || !content::validSubject(*subject->text()) || !kChannel.has_value() ||
        !kRelease.has_value() || !sequence->integer().has_value() || *sequence->integer() < 0 ||
        !updated->integer().has_value()) {
        return invalid("a ChannelPointer's subject, channel (stable, beta, or nightly), release, sequence, and "
                       "updated_at are in their grammars");
    }
    return ChannelPointer{.subject = *subject->text(),
                          .channel = *kChannel,
                          .release = *kRelease,
                          .sequence = *sequence->integer(),
                          .updatedAt = *updated->integer()};
}

result::Result<std::string> writePointer(const ChannelPointer& pointer) {
    Value value = Value::object();
    value.add("schema", Value::integer(1));
    value.add("subject", Value::string(pointer.subject));
    value.add("channel", Value::string(std::string{nameOf(pointer.channel)}));
    value.add("release", Value::string(textOf(pointer.release)));
    value.add("sequence", Value::integer(pointer.sequence));
    value.add("updated_at", Value::integer(pointer.updatedAt));
    RAWFRAME_TRY_ASSIGN(std::string text, document::writeCanonicalRecord(value));
    RAWFRAME_TRY(readPointer(text));
    return text;
}

base::Sha256Digest releaseIdOf(std::string_view recordText) noexcept {
    return base::sha256(recordText);
}

result::Result<ReleaseRecord> check(const ChannelPointer& pointer,
                                    std::string_view recordText,
                                    std::string_view subject,
                                    Channel channel,
                                    std::optional<std::int64_t> heldSequence) {
    if (pointer.subject != subject || pointer.channel != channel) {
        return refuse(ReleaseError::UnknownSubject, "the pointer is of another subject or channel than asked for");
    }
    if (releaseIdOf(recordText) != pointer.release) {
        return refuse(ReleaseError::DigestMismatch, "the pointer names another Release than the record given");
    }
    RAWFRAME_TRY_ASSIGN(ReleaseRecord record, readRelease(recordText));
    if (record.subject != subject) {
        return refuse(ReleaseError::UnknownSubject, "the record is of another subject than its pointer");
    }
    if (heldSequence.has_value() && pointer.sequence <= *heldSequence) {
        return refuse(ReleaseError::SequenceRegression, "the pointer is not past the one held: a replay");
    }
    return record;
}

} // namespace rawframe::release
