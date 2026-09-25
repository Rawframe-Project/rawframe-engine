#include "rawframe/build/build.h"

#include "compress.h"
#include "rawframe/build/chunking.h"
#include "rawframe/build/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/document/json.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <vector>

namespace rawframe::build {

namespace {

using document::Value;

std::unexpected<result::Error> refuse(BuildError error, std::string_view why, std::string_view where = {}) {
    auto failed = result::fail(result::ErrorClass::InvalidArgument, kBuildDomain, code(error), why).error();
    if (!where.empty()) {
        failed = std::move(failed).withContext("at", std::string{where});
    }
    return std::unexpected<result::Error>{std::move(failed)};
}

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    const std::string kText{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    const auto kBytes = std::as_bytes(std::span{kText.data(), kText.size()});
    return std::vector<std::byte>{kBytes.begin(), kBytes.end()};
}

/// Written beside its place and renamed into it, so a reader sees all of it
/// or none.
bool writeAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const std::filesystem::path kPart = path.string() + ".part";
    {
        std::ofstream file{kPart, std::ios::binary | std::ios::trunc};
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            return false;
        }
    }
    std::filesystem::rename(kPart, path, error);
    return !error;
}

std::span<const std::byte> bytesOf(std::string_view text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

std::string hexOf(const base::Bits128& value) {
    std::array<char, base::kBits128HexDigits> text{};
    base::formatBits128Hex(value, text);
    return std::string{text.data(), text.size()};
}

/// SPEC-0017's content-addressed path: `sha256/<two hex>/<sixty-two hex>`.
std::filesystem::path blobPath(const std::filesystem::path& output, const content::ContentDigest& digest) {
    const std::string kHex = digest.text().substr(7);
    return output / "sha256" / kHex.substr(0, 2) / kHex.substr(2);
}

bool token(std::string_view text, std::size_t maximum) noexcept {
    return !text.empty() && text.size() <= maximum && std::ranges::all_of(text, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_' || each == '.' ||
               each == '-';
    });
}

bool within(const std::filesystem::path& inner, const std::filesystem::path& outer) {
    const std::filesystem::path kInner = std::filesystem::weakly_canonical(inner);
    const std::filesystem::path kOuter = std::filesystem::weakly_canonical(outer);
    const auto kMismatch = std::ranges::mismatch(kOuter, kInner);
    return kMismatch.in1 == kOuter.end();
}

/// What the receipt says of one artifact.
struct Proven {
    std::string representation;
    std::string digest;
    std::int64_t byteLength = 0;
};

/// The receipt's artifacts by resource, once it proves the manifest beside
/// it: a cook receipt of format 1, no failures, naming `manifest` by digest.
result::Result<std::map<std::string, Proven>> readReceipt(const std::filesystem::path& cooked,
                                                          std::span<const std::byte> manifest) {
    const auto kBytes = readFile(cooked / "cook.receipt");
    if (!kBytes.has_value()) {
        return refuse(BuildError::NoProof, "a Build is packed only from a cook's receipt", "cook.receipt");
    }
    auto parsed =
        document::parseCanonical(std::string_view{reinterpret_cast<const char*>(kBytes->data()), kBytes->size()});
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error().withContext("at", "cook.receipt")};
    }
    const Value& receipt = *parsed;
    const auto kText = [&receipt](std::string_view name) -> std::string {
        const Value* value = receipt.find(name);
        return value != nullptr && value->kind() == Value::Kind::String ? *value->text() : std::string{};
    };
    const Value* failures = receipt.find("failures");
    const Value* format = receipt.find("formatVersion");
    if (kText("kind") != "cook.receipt" || format == nullptr || format->integer() != 1 || failures == nullptr ||
        failures->integer() != 0) {
        return refuse(BuildError::NoProof, "the receipt is not a cook receipt of no failures", "cook.receipt");
    }
    if (kText("manifest") != content::ContentDigest::of(manifest).text()) {
        return refuse(BuildError::NoProof, "the receipt proves another manifest than the one beside it", "manifest");
    }
    const Value* artifacts = receipt.find("artifacts");
    if (artifacts == nullptr || artifacts->kind() != Value::Kind::Array) {
        return refuse(BuildError::NoProof, "the receipt lists no artifacts", "artifacts");
    }
    std::map<std::string, Proven> proven;
    for (const Value& each : artifacts->items()) {
        const Value* id = each.find("resourceId");
        const Value* representation = each.find("representation");
        const Value* digest = each.find("digest");
        const Value* length = each.find("byteLength");
        if (id == nullptr || representation == nullptr || digest == nullptr || length == nullptr ||
            id->kind() != Value::Kind::String || representation->kind() != Value::Kind::String ||
            digest->kind() != Value::Kind::String || !length->integer().has_value() ||
            !proven
                 .emplace(*id->text(),
                          Proven{.representation = *representation->text(),
                                 .digest = *digest->text(),
                                 .byteLength = *length->integer()})
                 .second) {
            return refuse(BuildError::NoProof, "a receipt artifact is malformed or listed twice", "artifacts");
        }
    }
    return proven;
}

result::Status checkIdentity(const BuildIdentity& identity) {
    const bool kSide = identity.side == "client" || identity.side == "server";
    const bool kConfiguration = identity.configuration == "build.debug" ||
                                identity.configuration == "build.development" ||
                                identity.configuration == "build.shipping";
    if (!validSubject(identity.subject) || !validVersion(identity.version) || !validVersion(identity.engine) ||
        !token(identity.platform, 32) || !token(identity.architecture, 32) || !kSide || !kConfiguration ||
        !token(identity.profile, 64)) {
        return refuse(BuildError::BadIdentity, "a Build identity field is outside its grammar");
    }
    return {};
}

bool semverIdentifier(std::string_view part, bool numericNeedsNoLeadingZero) noexcept {
    if (part.empty() || !std::ranges::all_of(part, [](char each) {
            return (each >= '0' && each <= '9') || (each >= 'a' && each <= 'z') || (each >= 'A' && each <= 'Z') ||
                   each == '-';
        })) {
        return false;
    }
    const bool kNumeric = std::ranges::all_of(part, [](char each) {
        return each >= '0' && each <= '9';
    });
    return !numericNeedsNoLeadingZero || !kNumeric || part.size() == 1 || part.front() != '0';
}

bool dotted(std::string_view text, bool numericNeedsNoLeadingZero) noexcept {
    while (true) {
        const std::size_t kDot = text.find('.');
        if (!semverIdentifier(text.substr(0, kDot), numericNeedsNoLeadingZero)) {
            return false;
        }
        if (kDot == std::string_view::npos) {
            return true;
        }
        text.remove_prefix(kDot + 1);
    }
}

} // namespace

bool validVersion(std::string_view text) noexcept {
    if (text.empty() || text.size() > 64) {
        return false;
    }
    std::string_view core = text;
    const std::size_t kPlus = core.find('+');
    if (kPlus != std::string_view::npos) {
        if (!dotted(core.substr(kPlus + 1), false)) {
            return false;
        }
        core = core.substr(0, kPlus);
    }
    const std::size_t kDash = core.find('-');
    if (kDash != std::string_view::npos) {
        if (!dotted(core.substr(kDash + 1), true)) {
            return false;
        }
        core = core.substr(0, kDash);
    }
    int numbers = 0;
    while (true) {
        const std::size_t kDot = core.find('.');
        const std::string_view kNumber = core.substr(0, kDot);
        if (kNumber.empty() ||
            !std::ranges::all_of(kNumber,
                                 [](char each) {
                                     return each >= '0' && each <= '9';
                                 }) ||
            (kNumber.size() > 1 && kNumber.front() == '0')) {
            return false;
        }
        ++numbers;
        if (kDot == std::string_view::npos) {
            break;
        }
        core.remove_prefix(kDot + 1);
    }
    return numbers == 3;
}

bool validSubject(std::string_view text) noexcept {
    const std::size_t kSlash = text.find('/');
    if (kSlash == std::string_view::npos) {
        return false;
    }
    const auto kSegment = [](std::string_view segment) {
        return !segment.empty() && segment.front() != '-' && segment.back() != '-' &&
               std::ranges::all_of(segment, [](char each) {
                   return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '-';
               });
    };
    return kSegment(text.substr(0, kSlash)) && kSegment(text.substr(kSlash + 1));
}

result::Result<BuildReport> packBuild(const BuildRequest& request) {
    RAWFRAME_TRY(checkIdentity(request.identity));
    if (within(request.output, request.cooked) || within(request.cooked, request.output)) {
        return refuse(BuildError::BadRequest, "a Build and the cook's output are separate places");
    }
    // The proof first: the receipt, and the manifest it names.
    const auto kManifestBytes = readFile(request.cooked / "content.manifest");
    if (!kManifestBytes.has_value()) {
        return refuse(BuildError::NoProof, "the cook's output has no manifest", "content.manifest");
    }
    RAWFRAME_TRY_ASSIGN(const auto kProven, readReceipt(request.cooked, *kManifestBytes));
    RAWFRAME_TRY_ASSIGN(const std::vector<content::ManifestEntry> kEntries,
                        content::readManifest(std::string_view{reinterpret_cast<const char*>(kManifestBytes->data()),
                                                               kManifestBytes->size()}));
    if (kEntries.size() != kProven.size()) {
        return refuse(BuildError::ArtifactMismatch, "the manifest and the receipt list different artifacts");
    }

    BuildReport report;
    Value resources = Value::array();
    Value chunks = Value::object();
    for (const content::ManifestEntry& entry : kEntries) {
        const std::string kId = hexOf(entry.id.value);
        const auto kFound = kProven.find(kId);
        if (kFound == kProven.end() || kFound->second.representation != entry.representation.text() ||
            kFound->second.digest != entry.digest.text() ||
            kFound->second.byteLength != static_cast<std::int64_t>(entry.byteLength)) {
            return refuse(BuildError::ArtifactMismatch, "the receipt does not prove this artifact", kId);
        }
        const auto kBytes = readFile(request.cooked / entry.locator);
        if (!kBytes.has_value() || kBytes->size() != entry.byteLength ||
            !content::sameDigest(content::ContentDigest::of(*kBytes), entry.digest)) {
            return refuse(BuildError::ArtifactMismatch, "an artifact's bytes are not what was proven", entry.locator);
        }
        Value resource = Value::object();
        resource.add("resource", Value::string(kId));
        resource.add("type", Value::string(hexOf(entry.type.value)));
        resource.add("representation", Value::string(std::string{entry.representation.text()}));
        resource.add("digest", Value::string(entry.digest.text()));
        resource.add("size", Value::integer(static_cast<std::int64_t>(entry.byteLength)));
        resources.push(std::move(resource));

        // Each chunk compressed when that makes it smaller, raw otherwise,
        // its blob stored by the blob's digest.
        Value list = Value::array();
        const std::span<const std::byte> kAll{*kBytes};
        std::size_t start = 0;
        for (const std::size_t kEnd : chunkEnds(kAll)) {
            const std::span<const std::byte> kChunk = kAll.subspan(start, kEnd - start);
            const content::ContentDigest kContent = content::ContentDigest::of(kChunk);
            const std::optional<std::vector<std::byte>> kCompressed = compressChunk(kChunk);
            const std::span<const std::byte> kBlobBytes =
                kCompressed.has_value() ? std::span<const std::byte>{*kCompressed} : kChunk;
            const content::ContentDigest kBlobDigest = content::ContentDigest::of(kBlobBytes);
            const std::filesystem::path kBlob = blobPath(request.output, kBlobDigest);
            const auto kExisting = readFile(kBlob);
            if (kExisting.has_value() && content::sameDigest(content::ContentDigest::of(*kExisting), kBlobDigest)) {
                ++report.blobsReused;
            } else if (writeAtomically(kBlob, kBlobBytes)) {
                ++report.blobsWritten;
            } else {
                return refuse(BuildError::WriteFailed, "a blob cannot be written", kBlob.string());
            }
            Value chunk = Value::object();
            chunk.add("content", Value::string(kContent.text()));
            chunk.add("size", Value::integer(static_cast<std::int64_t>(kChunk.size())));
            chunk.add("blob", Value::string(kBlobDigest.text()));
            chunk.add("blob_size", Value::integer(static_cast<std::int64_t>(kBlobBytes.size())));
            chunk.add("codec", Value::string(kCompressed.has_value() ? "zstd" : "raw"));
            list.push(std::move(chunk));
            start = kEnd;
        }
        chunks.add(kId, std::move(list));
        ++report.resources;
    }

    const BuildIdentity& kIdentity = request.identity;
    Value identity = Value::object();
    identity.add("subject", Value::string(kIdentity.subject));
    identity.add("version", Value::string(kIdentity.version));
    identity.add("engine", Value::string(kIdentity.engine));
    identity.add("platform", Value::string(kIdentity.platform));
    identity.add("architecture", Value::string(kIdentity.architecture));
    identity.add("side", Value::string(kIdentity.side));
    identity.add("configuration", Value::string(kIdentity.configuration));
    identity.add("profile", Value::string(kIdentity.profile));
    identity.add("resources", std::move(resources));
    RAWFRAME_TRY_ASSIGN(const std::string kIdentityBytes, document::writeCanonicalRecord(identity));
    report.root = base::sha256(bytesOf(kIdentityBytes));

    Value manifest = Value::object();
    manifest.add("schema", Value::integer(1));
    manifest.add("identity", std::move(identity));
    manifest.add("chunks", std::move(chunks));
    RAWFRAME_TRY_ASSIGN(const std::string kManifest, document::writeCanonicalRecord(manifest));
    report.manifest = content::ContentDigest::of(bytesOf(kManifest));

    // How it was packed, apart from what it is: SPEC-0021's packaging
    // receipt, never needed to verify the Build.
    const auto kCookReceipt = readFile(request.cooked / "cook.receipt");
    Value receipt = Value::object();
    receipt.add("schema", Value::integer(1));
    receipt.add("manifest", Value::string(report.manifest.text()));
    receipt.add("manifest_size", Value::integer(static_cast<std::int64_t>(kManifest.size())));
    receipt.add("root", Value::string(content::ContentDigest{.bytes = report.root}.text()));
    receipt.add("cook_receipt", Value::string(content::ContentDigest::of(*kCookReceipt).text()));
    receipt.add("packer", Value::string("rawframe.build.2"));
    RAWFRAME_TRY_ASSIGN(const std::string kReceipt, document::writeCanonicalRecord(receipt));

    // Blobs are in; the old receipt out, the manifest in, the receipt last.
    std::error_code error;
    std::filesystem::remove(request.output / "packaging.receipt", error);
    if (!writeAtomically(request.output / "build.manifest", bytesOf(kManifest)) ||
        !writeAtomically(request.output / "packaging.receipt", bytesOf(kReceipt))) {
        return refuse(BuildError::WriteFailed, "the manifest or its receipt cannot be written");
    }
    return report;
}

} // namespace rawframe::build
