#include "rawframe/cook/cook.h"

#include "rawframe/content/manifest.h"
#include "rawframe/cook/errors.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <system_error>

namespace rawframe::cook {

namespace {

using document::Record;
using document::Value;

constexpr std::array<std::string_view, 4> kSidecarFields = {"schema", "resourceId", "importer", "settings"};
constexpr std::string_view kSidecarSuffix = ".rfmeta";
/// The largest source a cook reads, and the largest artifact it keeps.
constexpr std::uintmax_t kLargestFile = std::uintmax_t{1} << 30U;

result::Error failure(CookError error, std::string_view why, std::string_view where) {
    return result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(error), why)
        .error()
        .withContext("path", where);
}

std::string hexOf(std::span<const std::byte> bytes) {
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string made;
    for (const std::byte kByte : bytes) {
        made.push_back(kHex[std::to_integer<unsigned>(kByte) >> 4U]);
        made.push_back(kHex[std::to_integer<unsigned>(kByte) & 0xFU]);
    }
    return made;
}

std::string hexOf(base::Bits128 value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return std::string{digits.data(), digits.size()};
}

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t kSize = std::filesystem::file_size(path, error);
    if (error || kSize > kLargestFile) {
        return std::nullopt;
    }
    std::ifstream file{path, std::ios::binary};
    std::vector<std::byte> bytes(static_cast<std::size_t>(kSize));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::char_traits<char>::eof()) {
        return std::nullopt;
    }
    return bytes;
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

bool writeText(const std::filesystem::path& path, std::string_view text) {
    return writeAtomically(path, std::as_bytes(std::span{text.data(), text.size()}));
}

/// One source, read and ready to cook.
struct Planned {
    std::string source;
    content::ResourceId id;
    const Importer* importer = nullptr;
    std::string settings;
    std::vector<std::byte> bytes;
    base::Sha256Digest digest{};
};

/// The cook key: every input the artifact depends on (ADR-0024), and the
/// tool that made it.
base::Sha256Digest keyOf(const CookRequest& request, const Planned& planned) {
    base::Sha256 key;
    const auto kField = [&key](std::string_view text) {
        const auto kLength = static_cast<std::uint32_t>(text.size());
        const std::array<std::byte, 4> kPrefix = {static_cast<std::byte>(kLength >> 24U),
                                                  static_cast<std::byte>(kLength >> 16U),
                                                  static_cast<std::byte>(kLength >> 8U),
                                                  static_cast<std::byte>(kLength)};
        key.update(kPrefix);
        key.update(text);
    };
    kField("rawframe.cook.key.v1");
    key.update(request.toolchain);
    kField(planned.importer->identity);
    kField(planned.settings);
    key.update(planned.digest);
    kField(request.target);
    kField("primary");
    return key.finish();
}

/// A cached artifact, if the cache has one for this key that verifies: the
/// key names a digest, and the object under it has that digest.
std::optional<Artifact> fromCache(const std::filesystem::path& cache, const std::string& key) {
    const auto kRecord = readFile(cache / "keys" / key);
    if (!kRecord.has_value()) {
        return std::nullopt;
    }
    const std::string kText{reinterpret_cast<const char*>(kRecord->data()), kRecord->size()};
    auto parsed = document::parseCanonical(kText);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    const Value* type = parsed->find("type");
    const Value* representation = parsed->find("representation");
    const Value* digest = parsed->find("digest");
    if (type == nullptr || representation == nullptr || digest == nullptr || type->text() == nullptr ||
        representation->text() == nullptr || digest->text() == nullptr) {
        return std::nullopt;
    }
    const auto kDigest = content::ContentDigest::parse(*digest->text());
    const auto kRepresentation = content::RepresentationId::parse(*representation->text());
    const base::Bits128Parse kType = base::parseBits128Hex(*type->text());
    if (!kDigest.has_value() || !kRepresentation.has_value() || !kType.parsed) {
        return std::nullopt;
    }
    auto bytes = readFile(cache / "objects" / hexOf(kDigest->bytes));
    if (!bytes.has_value() || !content::sameDigest(content::ContentDigest::of(*bytes), *kDigest)) {
        return std::nullopt;
    }
    return Artifact{
        .type = content::ResourceTypeId{kType.value}, .representation = *kRepresentation, .bytes = std::move(*bytes)};
}

void toCache(const std::filesystem::path& cache, const std::string& key, const Artifact& artifact) {
    const content::ContentDigest kDigest = content::ContentDigest::of(artifact.bytes);
    Value record = Value::object();
    record.add("type", Value::string(hexOf(artifact.type.value)));
    record.add("representation", Value::string(std::string{artifact.representation.text()}));
    record.add("digest", Value::string(kDigest.text()));
    // A cache is only ever a saving: failing to fill it fails nothing.
    if (writeAtomically(cache / "objects" / hexOf(kDigest.bytes), artifact.bytes)) {
        static_cast<void>(writeText(cache / "keys" / key, document::write(record)));
    }
}

} // namespace

result::Result<base::Sha256Digest> digestOfFile(const std::filesystem::path& path) {
    const auto kBytes = readFile(path);
    if (!kBytes.has_value()) {
        return std::unexpected<result::Error>{failure(CookError::BadRequest, "the file cannot be read", "")};
    }
    return base::sha256(*kBytes);
}

result::Result<CookReport> cookSources(const CookRequest& request) {
    std::error_code error;
    const std::filesystem::path kSources = std::filesystem::canonical(request.sources, error);
    if (error || !std::filesystem::is_directory(kSources)) {
        return std::unexpected<result::Error>{
            failure(CookError::BadRequest, "the sources are not a directory", request.sources.string())};
    }
    const std::filesystem::path kOutput = std::filesystem::weakly_canonical(request.output, error);
    const auto kRelative = kOutput.lexically_relative(kSources);
    if (error || kOutput == kSources || (!kRelative.empty() && *kRelative.begin() != "..")) {
        return std::unexpected<result::Error>{
            failure(CookError::BadRequest, "the output is inside the sources", request.output.string())};
    }

    // Every sidecar, in path order: never in the order a directory lists.
    std::vector<std::string> sidecars;
    for (auto entry = std::filesystem::recursive_directory_iterator{kSources, error};
         !error && entry != std::filesystem::recursive_directory_iterator{};
         entry.increment(error)) {
        const std::string kName = entry->path().filename().string();
        if (entry->is_regular_file() && kName.ends_with(kSidecarSuffix) && kName.size() > kSidecarSuffix.size()) {
            sidecars.push_back(entry->path().lexically_relative(kSources).generic_string());
        }
    }
    if (error) {
        return std::unexpected<result::Error>{
            failure(CookError::BadRequest, "the sources cannot be listed", request.sources.string())};
    }
    std::ranges::sort(sidecars);

    CookReport report;
    std::vector<Planned> plan;
    for (const std::string& sidecar : sidecars) {
        const auto kText = readFile(kSources / sidecar);
        if (!kText.has_value()) {
            report.failures.push_back(failure(CookError::BadSidecar, "the sidecar cannot be read", sidecar));
            continue;
        }
        auto parsed =
            document::parseCanonical(std::string_view{reinterpret_cast<const char*>(kText->data()), kText->size()});
        if (!parsed.has_value()) {
            report.failures.push_back(std::move(parsed).error().withContext("path", sidecar));
            continue;
        }
        const Value* schema = parsed->find("schema");
        if (schema == nullptr || schema->integer() != 1) {
            report.failures.push_back(failure(CookError::BadSidecar, "a sidecar's schema is 1", sidecar));
            continue;
        }
        auto record = Record::of(*parsed, kSidecarFields, "$");
        if (!record.has_value()) {
            report.failures.push_back(std::move(record).error().withContext("path", sidecar));
            continue;
        }
        Planned planned;
        planned.source = sidecar.substr(0, sidecar.size() - kSidecarSuffix.size());
        const auto kId = record->text("resourceId");
        const base::Bits128Parse kParsed = kId.has_value() ? base::parseBits128Hex(*kId) : base::Bits128Parse{};
        if (!kParsed.parsed || kParsed.value == base::Bits128{}) {
            report.failures.push_back(failure(CookError::BadSidecar, "a resource identity, not nought", sidecar));
            continue;
        }
        planned.id = content::ResourceId{kParsed.value};
        const auto kImporter = record->text("importer");
        const auto kFound = kImporter.has_value()
                                ? std::ranges::find(request.importers, *kImporter, &Importer::identity)
                                : request.importers.end();
        if (kFound == request.importers.end()) {
            report.failures.push_back(failure(CookError::UnknownImporter, "no importer of that identity", sidecar));
            continue;
        }
        planned.importer = &*kFound;
        auto settingsValue = record->optional("settings", Value::Kind::Object);
        if (!settingsValue.has_value()) {
            report.failures.push_back(std::move(settingsValue).error().withContext("path", sidecar));
            continue;
        }
        auto settings = planned.importer->normalize(*settingsValue);
        if (!settings.has_value()) {
            report.failures.push_back(std::move(settings).error().withContext("path", sidecar));
            continue;
        }
        planned.settings = std::move(*settings);
        auto bytes = readFile(kSources / planned.source);
        if (!bytes.has_value()) {
            report.failures.push_back(failure(CookError::MissingSource, "the sidecar's source is missing", sidecar));
            continue;
        }
        planned.bytes = std::move(*bytes);
        planned.digest = base::sha256(planned.bytes);
        plan.push_back(std::move(planned));
    }
    std::ranges::sort(plan, {}, &Planned::id);
    for (std::size_t index = 1; index < plan.size(); ++index) {
        if (plan[index].id == plan[index - 1].id) {
            report.failures.push_back(
                failure(CookError::DuplicateResource, "two sidecars claim one resource", plan[index].source));
        }
    }

    std::vector<content::ManifestEntry> entries;
    Value inputs = Value::array();
    Value artifacts = Value::array();
    for (const Planned& planned : plan) {
        const std::string kKey = hexOf(keyOf(request, planned));
        std::optional<Artifact> artifact = request.cache ? fromCache(*request.cache, kKey) : std::nullopt;
        const bool kReused = artifact.has_value();
        if (!kReused) {
            // Twice, and the same both times, or it is not published.
            auto first = planned.importer->cook(planned.bytes, planned.settings);
            if (!first.has_value()) {
                report.failures.push_back(std::move(first).error().withContext("path", planned.source));
                continue;
            }
            const auto kSecond = planned.importer->cook(planned.bytes, planned.settings);
            if (!kSecond.has_value() || kSecond->bytes != first->bytes) {
                report.failures.push_back(
                    failure(CookError::Nondeterministic, "two cooks of one source differ", planned.source));
                continue;
            }
            artifact = std::move(*first);
            if (request.cache) {
                toCache(*request.cache, kKey, *artifact);
            }
        }
        const content::ContentDigest kDigest = content::ContentDigest::of(artifact->bytes);
        const std::string kLocator = "objects/" + hexOf(kDigest.bytes);
        const auto kExisting = readFile(kOutput / kLocator);
        const bool kPresent =
            kExisting.has_value() && content::sameDigest(content::ContentDigest::of(*kExisting), kDigest);
        if (!kPresent && !writeAtomically(kOutput / kLocator, artifact->bytes)) {
            report.failures.push_back(failure(CookError::WriteFailed, "an artifact cannot be written", kLocator));
            continue;
        }
        (kReused ? report.reused : report.cooked) += 1;
        entries.push_back(content::ManifestEntry{.id = planned.id,
                                                 .type = artifact->type,
                                                 .representation = artifact->representation,
                                                 .byteLength = artifact->bytes.size(),
                                                 .digest = kDigest,
                                                 .locator = kLocator});
        Value input = Value::object();
        input.add("resourceId", Value::string(hexOf(planned.id.value)));
        input.add("source", Value::string(planned.source));
        input.add("importer", Value::string(std::string{planned.importer->identity}));
        input.add("sourceDigest", Value::string("sha256:" + hexOf(planned.digest)));
        input.add("key", Value::string("sha256:" + kKey));
        input.add("reused", Value::boolean(kReused));
        inputs.push(std::move(input));
        Value made = Value::object();
        made.add("resourceId", Value::string(hexOf(planned.id.value)));
        made.add("representation", Value::string(std::string{artifact->representation.text()}));
        made.add("digest", Value::string(kDigest.text()));
        made.add("byteLength", Value::integer(static_cast<std::int64_t>(artifact->bytes.size())));
        artifacts.push(std::move(made));
    }
    if (!report.failures.empty()) {
        return report;
    }
    Value receipt = Value::object();
    receipt.add("kind", Value::string("cook.receipt"));
    receipt.add("formatVersion", Value::integer(1));
    receipt.add("toolchain", Value::string("sha256:" + hexOf(request.toolchain)));
    receipt.add("target", Value::string(request.target));
    receipt.add("determinism", Value::string("double_cook"));
    receipt.add("inputs", std::move(inputs));
    receipt.add("artifacts", std::move(artifacts));
    receipt.add("failures", Value::integer(0));
    // The old receipt out, the manifest in, the receipt last: a receipt is
    // only ever beside the manifest it proves.
    std::filesystem::remove(kOutput / "cook.receipt", error);
    if (!writeText(kOutput / "content.manifest", content::writeManifest(entries)) ||
        !writeText(kOutput / "cook.receipt", document::write(receipt))) {
        report.failures.push_back(failure(CookError::WriteFailed, "the manifest or receipt cannot be written", ""));
    }
    return report;
}

} // namespace rawframe::cook
