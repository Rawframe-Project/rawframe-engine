#include "rawframe/content/manifest.h"

#include "rawframe/content/errors.h"
#include "rawframe/document/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <array>

namespace rawframe::content {

namespace {

using document::Record;
using document::Value;

constexpr std::array<std::string_view, 3> kDocumentFields = {"kind", "formatVersion", "entries"};
constexpr std::array<std::string_view, 6> kEntryFields = {
    "resourceId", "resourceTypeId", "representation", "byteLength", "digest", "locator"};

std::unexpected<result::Error> refuse(ContentError error, std::string_view path, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kContentDomain, code(error), why)
            .error()
            .withContext("path", path)};
}

bool locatorCharacter(char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
           character == '-' || character == '.';
}

} // namespace

bool validLocator(std::string_view locator, const ManifestLimits& limits) noexcept {
    if (locator.empty() || locator.size() > limits.maximumLocatorBytes) {
        return false;
    }
    std::size_t segments = 0;
    std::size_t start = 0;
    while (start <= locator.size()) {
        const std::size_t kEnd = std::min(locator.find('/', start), locator.size());
        const std::string_view kSegment = locator.substr(start, kEnd - start);
        if (kSegment.empty() || kSegment == "." || kSegment == ".." ||
            !std::ranges::all_of(kSegment, locatorCharacter)) {
            return false;
        }
        if (++segments > limits.maximumLocatorSegments) {
            return false;
        }
        start = kEnd + 1;
    }
    return true;
}

result::Result<std::vector<ManifestEntry>> readManifest(std::string_view text, const ManifestLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Value kRoot, document::parseCanonical(text, {.maximumBytes = limits.maximumBytes}));
    const Value* version = kRoot.find("formatVersion");
    if (version == nullptr || version->integer() != 1) {
        return refuse(ContentError::UnsupportedManifestVersion, "$.formatVersion", "the format version is 1");
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(kRoot, kDocumentFields, "$"));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    if (kKind != "content.manifest") {
        return refuse(ContentError::ManifestInvalid, "$.kind", "the kind is content.manifest");
    }
    RAWFRAME_TRY_ASSIGN(const Value* entries, kRecord.required("entries", Value::Kind::Array));
    if (entries->items().size() > limits.maximumEntries) {
        return refuse(ContentError::ManifestInvalid, "$.entries", "more entries than allowed");
    }
    std::vector<ManifestEntry> read;
    read.reserve(entries->items().size());
    for (std::size_t index = 0; index < entries->items().size(); ++index) {
        const std::string kPath = "$.entries[" + std::to_string(index) + "]";
        RAWFRAME_TRY_ASSIGN(const Record kEntry, Record::of(entries->items()[index], kEntryFields, kPath));
        RAWFRAME_TRY_ASSIGN(const std::string_view kId, kEntry.text("resourceId"));
        const base::Bits128Parse kParsedId = base::parseBits128Hex(kId);
        if (!kParsedId.parsed || kParsedId.value == base::Bits128{}) {
            return refuse(ContentError::InvalidResourceId,
                          kEntry.pathOf("resourceId"),
                          "a resource identity is 32 lowercase hexadecimal digits, not all nought");
        }
        RAWFRAME_TRY_ASSIGN(const std::string_view kType, kEntry.text("resourceTypeId"));
        const base::Bits128Parse kParsedType = base::parseBits128Hex(kType);
        if (!kParsedType.parsed || kParsedType.value == base::Bits128{}) {
            return refuse(ContentError::InvalidResourceType,
                          kEntry.pathOf("resourceTypeId"),
                          "a resource type is 32 lowercase hexadecimal digits, not all nought");
        }
        RAWFRAME_TRY_ASSIGN(const std::string_view kRepresentation, kEntry.text("representation"));
        std::optional<RepresentationId> representation = RepresentationId::parse(kRepresentation);
        if (!representation.has_value()) {
            return refuse(ContentError::UnsupportedRepresentation,
                          kEntry.pathOf("representation"),
                          "a representation is lowercase dotted words");
        }
        RAWFRAME_TRY_ASSIGN(const std::int64_t kLength, kEntry.integer("byteLength"));
        if (kLength < 0 || static_cast<std::uint64_t>(kLength) > limits.maximumResourceBytes) {
            return refuse(
                ContentError::ResourceTooLarge, kEntry.pathOf("byteLength"), "a length within the resource limit");
        }
        RAWFRAME_TRY_ASSIGN(const std::string_view kDigest, kEntry.text("digest"));
        const std::optional<ContentDigest> kParsedDigest = ContentDigest::parse(kDigest);
        if (!kParsedDigest.has_value()) {
            return refuse(ContentError::ManifestInvalid,
                          kEntry.pathOf("digest"),
                          "a digest is sha256: and 64 lowercase hexadecimal digits");
        }
        RAWFRAME_TRY_ASSIGN(const std::string_view kLocator, kEntry.text("locator"));
        if (!validLocator(kLocator, limits)) {
            return refuse(ContentError::InvalidLocator,
                          kEntry.pathOf("locator"),
                          "a locator is relative segments of lowercase letters, digits, _, -, and .");
        }
        read.push_back(ManifestEntry{.id = ResourceId{kParsedId.value},
                                     .type = ResourceTypeId{kParsedType.value},
                                     .representation = std::move(*representation),
                                     .byteLength = static_cast<std::uint64_t>(kLength),
                                     .digest = *kParsedDigest,
                                     .locator = std::string{kLocator}});
    }
    std::ranges::sort(read, {}, &ManifestEntry::id);
    const auto kDuplicate = std::ranges::adjacent_find(read, {}, &ManifestEntry::id);
    if (kDuplicate != read.end()) {
        return refuse(ContentError::DuplicateResource, "$.entries", "a resource is listed once");
    }
    return read;
}

std::string writeManifest(std::span<const ManifestEntry> entries) {
    std::vector<const ManifestEntry*> sorted;
    for (const ManifestEntry& entry : entries) {
        sorted.push_back(&entry);
    }
    std::ranges::sort(sorted, {}, [](const ManifestEntry* entry) {
        return entry->id;
    });
    const auto kHex = [](base::Bits128 value) {
        std::array<char, base::kBits128HexDigits> digits{};
        base::formatBits128Hex(value, digits);
        return std::string{digits.data(), digits.size()};
    };
    Value root = Value::object();
    root.add("kind", Value::string("content.manifest"));
    root.add("formatVersion", Value::integer(1));
    Value list = Value::array();
    for (const ManifestEntry* entry : sorted) {
        Value made = Value::object();
        made.add("resourceId", Value::string(kHex(entry->id.value)));
        made.add("resourceTypeId", Value::string(kHex(entry->type.value)));
        made.add("representation", Value::string(std::string{entry->representation.text()}));
        made.add("byteLength", Value::integer(static_cast<std::int64_t>(entry->byteLength)));
        made.add("digest", Value::string(entry->digest.text()));
        made.add("locator", Value::string(entry->locator));
        list.push(std::move(made));
    }
    root.add("entries", std::move(list));
    return document::write(root);
}

} // namespace rawframe::content
