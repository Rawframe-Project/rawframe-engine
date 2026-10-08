#include "rawframe/cook/cook.h"

#include "rawframe/content/manifest.h"
#include "rawframe/cook/errors.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <system_error>
#include <tuple>

namespace rawframe::cook {

namespace {

using document::Value;

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

/// A read as a cache record or a receipt writes it.
Value valueOf(const Reads::Read& read) {
    Value made = Value::object();
    made.add("path", Value::string(read.path));
    if (read.listing) {
        made.add("suffix", Value::string(read.suffix));
    }
    made.add("digest", Value::string("sha256:" + hexOf(read.digest)));
    return made;
}

Value valueOf(std::span<const Reads::Read> reads) {
    Value made = Value::array();
    for (const Reads::Read& read : reads) {
        made.push(valueOf(read));
    }
    return made;
}

/// Every read a cache record names, done again, if each sees what it saw:
/// none when one would not, or is not a read at all.
std::optional<std::vector<Reads::Read>> readAgain(const std::filesystem::path& sources, const Value* reads) {
    if (reads == nullptr || reads->kind() != Value::Kind::Array) {
        return std::nullopt;
    }
    Reads again{sources, {}};
    for (const Value& read : reads->items()) {
        const Value* path = read.find("path");
        const Value* suffix = read.find("suffix");
        const Value* digest = read.find("digest");
        if (path == nullptr || path->text() == nullptr || digest == nullptr || digest->text() == nullptr ||
            (suffix != nullptr && suffix->text() == nullptr)) {
            return std::nullopt;
        }
        base::Sha256Digest seen{};
        if (suffix != nullptr) {
            const auto kNames = again.files(*path->text(), *suffix->text());
            if (!kNames.has_value()) {
                return std::nullopt;
            }
            seen = Reads::digestOfListing(*kNames);
        } else {
            const auto kBytes = again.file(*path->text());
            if (!kBytes.has_value()) {
                return std::nullopt;
            }
            seen = base::sha256(*kBytes);
        }
        if (*digest->text() != "sha256:" + hexOf(seen)) {
            return std::nullopt;
        }
    }
    return again.reads();
}

/// One source, read and ready to cook.
struct Planned {
    std::string source;
    content::ResourceId id;
    const Importer* importer = nullptr;
    std::string settings;
    std::vector<std::byte> bytes;
    base::Sha256Digest digest{};
    std::map<std::string, content::ResourceId, std::less<>> subassets;
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
    // What the importer runs besides this tool, where it runs anything.
    if (planned.importer->tools != base::Sha256Digest{}) {
        key.update(planned.importer->tools);
    }
    kField(planned.settings);
    key.update(planned.digest);
    // The subasset map: what the artifacts name each other by.
    for (const auto& [kSubasset, kId] : planned.subassets) {
        kField(kSubasset);
        kField(hexOf(kId.value));
    }
    kField(request.target);
    kField("primary");
    return key.finish();
}

/// A cached object's bytes, if they are there and have `digest`.
std::optional<std::vector<std::byte>> cachedObject(const std::filesystem::path& cache, const Value* digest) {
    const auto kDigest =
        digest != nullptr && digest->text() != nullptr ? content::ContentDigest::parse(*digest->text()) : std::nullopt;
    if (!kDigest.has_value()) {
        return std::nullopt;
    }
    auto bytes = readFile(cache / "objects" / hexOf(kDigest->bytes));
    if (!bytes.has_value() || !content::sameDigest(content::ContentDigest::of(*bytes), *kDigest)) {
        return std::nullopt;
    }
    return bytes;
}

/// What a cache record says an object is, if it says it.
std::optional<std::pair<content::ResourceTypeId, content::RepresentationId>> kindOf(const Value& record) {
    const Value* type = record.find("type");
    const Value* representation = record.find("representation");
    if (type == nullptr || representation == nullptr || type->text() == nullptr || representation->text() == nullptr) {
        return std::nullopt;
    }
    const auto kRepresentation = content::RepresentationId::parse(*representation->text());
    const base::Bits128Parse kType = base::parseBits128Hex(*type->text());
    if (!kRepresentation.has_value() || !kType.parsed) {
        return std::nullopt;
    }
    return std::pair{content::ResourceTypeId{kType.value}, *kRepresentation};
}

/// An importer's report as a cache record or a receipt writes it (D319).
Value valueOf(std::span<const std::pair<std::string, std::int64_t>> report) {
    Value made = Value::object();
    for (const auto& [kName, kCount] : report) {
        made.add(kName, Value::integer(kCount));
    }
    return made;
}

/// A cached artifact, if the cache has one for this key that verifies: the
/// key names a digest, the object under it has that digest, each subasset's
/// too, and every read that made it would read the same now.
struct Cached {
    Artifact artifact;
    std::vector<Reads::Read> reads;
};

std::optional<Cached>
fromCache(const std::filesystem::path& sources, const std::filesystem::path& cache, const std::string& key) {
    const auto kRecord = readFile(cache / "keys" / key);
    if (!kRecord.has_value()) {
        return std::nullopt;
    }
    const std::string kText{reinterpret_cast<const char*>(kRecord->data()), kRecord->size()};
    auto parsed = document::parseCanonical(kText);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    const auto kKind = kindOf(*parsed);
    auto bytes = cachedObject(cache, parsed->find("digest"));
    const Value* subassets = parsed->find("subassets");
    if (!kKind.has_value() || !bytes.has_value() || subassets == nullptr || subassets->kind() != Value::Kind::Array) {
        return std::nullopt;
    }
    Artifact artifact{.type = kKind->first, .representation = kKind->second, .bytes = std::move(*bytes)};
    const Value* report = parsed->find("report");
    if (report == nullptr || report->kind() != Value::Kind::Object) {
        return std::nullopt;
    }
    for (std::size_t at = 0; at < report->names().size(); ++at) {
        const std::optional<std::int64_t> kCount = report->items()[at].integer();
        if (!kCount.has_value()) {
            return std::nullopt;
        }
        artifact.report.emplace_back(report->names()[at], *kCount);
    }
    for (const Value& subasset : subassets->items()) {
        const Value* named = subasset.find("key");
        const auto kSubassetKind = kindOf(subasset);
        auto subassetBytes = cachedObject(cache, subasset.find("digest"));
        if (named == nullptr || named->text() == nullptr || !kSubassetKind.has_value() || !subassetBytes.has_value()) {
            return std::nullopt;
        }
        artifact.subassets.push_back(Subasset{.key = *named->text(),
                                              .type = kSubassetKind->first,
                                              .representation = kSubassetKind->second,
                                              .bytes = std::move(*subassetBytes)});
    }
    auto reads = readAgain(sources, parsed->find("reads"));
    if (!reads.has_value()) {
        return std::nullopt;
    }
    return Cached{.artifact = std::move(artifact), .reads = std::move(*reads)};
}

void toCache(const std::filesystem::path& cache,
             const std::string& key,
             const Artifact& artifact,
             std::span<const Reads::Read> reads) {
    // A cache is only ever a saving: failing to fill it fails nothing.
    const auto kKept = [&cache](Value& record,
                                content::ResourceTypeId type,
                                const content::RepresentationId& representation,
                                std::span<const std::byte> bytes) {
        const content::ContentDigest kDigest = content::ContentDigest::of(bytes);
        record.add("type", Value::string(hexOf(type.value)));
        record.add("representation", Value::string(std::string{representation.text()}));
        record.add("digest", Value::string(kDigest.text()));
        return writeAtomically(cache / "objects" / hexOf(kDigest.bytes), bytes);
    };
    Value record = Value::object();
    bool kept = kKept(record, artifact.type, artifact.representation, artifact.bytes);
    Value subassets = Value::array();
    for (const Subasset& subasset : artifact.subassets) {
        Value made = Value::object();
        made.add("key", Value::string(subasset.key));
        kept = kept && kKept(made, subasset.type, subasset.representation, subasset.bytes);
        subassets.push(std::move(made));
    }
    record.add("subassets", std::move(subassets));
    record.add("report", valueOf(artifact.report));
    record.add("reads", valueOf(reads));
    if (kept) {
        static_cast<void>(writeText(cache / "keys" / key, document::write(record)));
    }
}

/// Every sidecar under `sources`, relative to it, in path order: never in
/// the order a directory lists.
result::Result<std::vector<std::string>> sidecarsUnder(const std::filesystem::path& sources) {
    std::error_code error;
    std::vector<std::string> sidecars;
    for (auto entry = std::filesystem::recursive_directory_iterator{sources, error};
         !error && entry != std::filesystem::recursive_directory_iterator{};
         entry.increment(error)) {
        const std::string kName = entry->path().filename().string();
        if (entry->is_regular_file() && kName.ends_with(content::kSidecarSuffix) &&
            kName.size() > content::kSidecarSuffix.size()) {
            sidecars.push_back(entry->path().lexically_relative(sources).generic_string());
        }
    }
    if (error) {
        return std::unexpected<result::Error>{
            failure(CookError::BadRequest, "the sources cannot be listed", sources.string())};
    }
    std::ranges::sort(sidecars);
    return sidecars;
}

} // namespace

result::Result<base::Sha256Digest> digestOfFile(const std::filesystem::path& path) {
    const auto kBytes = readFile(path);
    if (!kBytes.has_value()) {
        return std::unexpected<result::Error>{failure(CookError::BadRequest, "the file cannot be read", "")};
    }
    return base::sha256(*kBytes);
}

Reads::Reads(std::filesystem::path sources,
             std::filesystem::path directory,
             std::map<std::string, content::ResourceId, std::less<>> subassets)
    : sources_(std::move(sources)), directory_(std::move(directory)), subassets_(std::move(subassets)) {
}

result::Result<content::ResourceId> Reads::subasset(std::string_view key) {
    const auto kFound = subassets_.find(key);
    if (kFound == subassets_.end() && fresh_) {
        const auto kAssigned = assigned_.find(key);
        if (kAssigned != assigned_.end()) {
            return kAssigned->second;
        }
        return assigned_.emplace(std::string{key}, fresh_()).first->second;
    }
    if (kFound == subassets_.end()) {
        return std::unexpected<result::Error>{
            failure(CookError::UnmappedSubasset, "a subasset the sidecar maps to no resource", "")
                .withContext("subasset", std::string{key})};
    }
    return kFound->second;
}

result::Result<std::filesystem::path> Reads::resolve(std::string_view path) const {
    const std::filesystem::path kAsked{path};
    std::error_code error;
    const std::filesystem::path kResolved =
        kAsked.is_absolute() ? std::filesystem::path{}
                             : std::filesystem::weakly_canonical(sources_ / directory_ / kAsked, error);
    const std::filesystem::path kRelative = kResolved.lexically_relative(sources_);
    if (path.empty() || kResolved.empty() || error || kRelative.empty() || *kRelative.begin() == "..") {
        return std::unexpected<result::Error>{failure(CookError::BadRead, "a read outside the sources", path)};
    }
    return kRelative;
}

result::Result<std::span<const std::byte>> Reads::file(std::string_view path) {
    RAWFRAME_TRY_ASSIGN(const std::filesystem::path kRelative, resolve(path));
    const std::string kKey = kRelative.generic_string();
    auto found = files_.find(kKey);
    if (found == files_.end()) {
        auto bytes = readFile(sources_ / kRelative);
        if (!bytes.has_value()) {
            return std::unexpected<result::Error>{failure(CookError::BadRead, "a read of what cannot be read", kKey)};
        }
        found = files_.emplace(kKey, std::move(*bytes)).first;
    }
    return std::span<const std::byte>{found->second};
}

result::Result<std::vector<std::string>> Reads::files(std::string_view path, std::string_view suffix) {
    RAWFRAME_TRY_ASSIGN(const std::filesystem::path kRelative, resolve(path));
    std::pair<std::string, std::string> key{kRelative.generic_string(), std::string{suffix}};
    auto found = listings_.find(key);
    if (found == listings_.end()) {
        const std::filesystem::path kDirectory = sources_ / kRelative;
        std::error_code error;
        std::vector<std::string> names;
        if (!std::filesystem::is_directory(kDirectory, error)) {
            return std::unexpected<result::Error>{
                failure(CookError::BadRead, "a listing of what is not a directory", key.first)};
        }
        for (auto entry = std::filesystem::recursive_directory_iterator{kDirectory, error};
             !error && entry != std::filesystem::recursive_directory_iterator{};
             entry.increment(error)) {
            if (entry->is_regular_file() && entry->path().filename().string().ends_with(suffix)) {
                names.push_back(entry->path().lexically_relative(kDirectory).generic_string());
            }
        }
        if (error) {
            return std::unexpected<result::Error>{
                failure(CookError::BadRead, "a directory that cannot be listed", key.first)};
        }
        std::ranges::sort(names);
        found = listings_.emplace(std::move(key), std::move(names)).first;
    }
    return found->second;
}

std::vector<Reads::Read> Reads::reads() const {
    std::vector<Read> made;
    for (const auto& [kPath, kBytes] : files_) {
        made.push_back(Read{.path = kPath, .digest = base::sha256(kBytes)});
    }
    for (const auto& [kKey, kNames] : listings_) {
        made.push_back(
            Read{.path = kKey.first, .suffix = kKey.second, .listing = true, .digest = digestOfListing(kNames)});
    }
    std::ranges::sort(made, [](const Read& left, const Read& right) {
        return std::tie(left.path, left.listing, left.suffix) < std::tie(right.path, right.listing, right.suffix);
    });
    return made;
}

void Reads::assignWith(std::function<content::ResourceId()> fresh) {
    fresh_ = std::move(fresh);
}

base::Sha256Digest Reads::digestOfListing(std::span<const std::string> names) {
    base::Sha256 digest;
    for (const std::string& name : names) {
        digest.update(name);
        digest.update("\n");
    }
    return digest.finish();
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

    RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kSidecars, sidecarsUnder(kSources));

    CookReport report;
    std::vector<Planned> plan;
    for (const std::string& sidecar : kSidecars) {
        const auto kText = readFile(kSources / sidecar);
        if (!kText.has_value()) {
            report.failures.push_back(failure(CookError::BadSidecar, "the sidecar cannot be read", sidecar));
            continue;
        }
        auto read = content::readSidecar(std::string_view{reinterpret_cast<const char*>(kText->data()), kText->size()});
        if (!read.has_value()) {
            report.failures.push_back(failure(CookError::BadSidecar, read.error().description(), sidecar));
            continue;
        }
        Planned planned;
        planned.source = sidecar.substr(0, sidecar.size() - content::kSidecarSuffix.size());
        planned.id = read->id;
        const auto kFound = std::ranges::find(request.importers, read->importer, &Importer::identity);
        if (kFound == request.importers.end()) {
            report.failures.push_back(failure(CookError::UnknownImporter, "no importer of that identity", sidecar));
            continue;
        }
        planned.importer = &*kFound;
        planned.subassets = std::move(read->subassets);
        const Value* settingsValue = read->settings ? &*read->settings : nullptr;
        auto settings = planned.importer->normalize(settingsValue);
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
        const std::size_t kIndex = static_cast<std::size_t>(&planned - plan.data());
        if (request.step && !request.step(CookStep{.index = kIndex, .count = plan.size(), .source = planned.source})) {
            report.stopped = true;
            return report;
        }
        const std::string kKey = hexOf(keyOf(request, planned));
        std::optional<Cached> cached = request.cache ? fromCache(kSources, *request.cache, kKey) : std::nullopt;
        const bool kReused = cached.has_value();
        std::optional<Artifact> artifact;
        std::vector<Reads::Read> readsMade;
        if (kReused) {
            artifact = std::move(cached->artifact);
            readsMade = std::move(cached->reads);
        } else {
            // Twice, and the same both times, or it is not published. One
            // Reads for both, so both see one input.
            Reads reads{kSources, std::filesystem::path{planned.source}.parent_path(), planned.subassets};
            auto first = planned.importer->cook(planned.bytes, planned.settings, reads);
            if (!first.has_value()) {
                report.failures.push_back(std::move(first).error().withContext("path", planned.source));
                continue;
            }
            const auto kSecond = planned.importer->cook(planned.bytes, planned.settings, reads);
            if (!kSecond.has_value() || *kSecond != *first) {
                report.failures.push_back(
                    failure(CookError::Nondeterministic, "two cooks of one source differ", planned.source));
                continue;
            }
            // Each subasset once, in key order, under a resource its sidecar
            // maps it to.
            const auto kUnmapped = std::ranges::find_if(first->subassets, [&reads](const Subasset& subasset) {
                return !reads.subasset(subasset.key).has_value();
            });
            if (kUnmapped != first->subassets.end()) {
                report.failures.push_back(reads.subasset(kUnmapped->key).error().withContext("path", planned.source));
                continue;
            }
            if (std::ranges::adjacent_find(first->subassets, std::ranges::greater_equal{}, &Subasset::key) !=
                first->subassets.end()) {
                report.failures.push_back(
                    failure(CookError::BadReference, "an importer's subassets out of key order", planned.source));
                continue;
            }
            artifact = std::move(*first);
            readsMade = reads.reads();
            if (request.cache) {
                toCache(*request.cache, kKey, *artifact, readsMade);
            }
        }
        // The source's resource, then each subasset's.
        const auto kPublished = [&](const content::ResourceId& id,
                                    content::ResourceTypeId type,
                                    const content::RepresentationId& representation,
                                    std::span<const std::byte> bytes,
                                    std::string_view subasset) {
            const content::ContentDigest kDigest = content::ContentDigest::of(bytes);
            const std::string kLocator = "objects/" + hexOf(kDigest.bytes);
            const auto kExisting = readFile(kOutput / kLocator);
            const bool kPresent =
                kExisting.has_value() && content::sameDigest(content::ContentDigest::of(*kExisting), kDigest);
            if (!kPresent && !writeAtomically(kOutput / kLocator, bytes)) {
                report.failures.push_back(failure(CookError::WriteFailed, "an artifact cannot be written", kLocator));
                return false;
            }
            entries.push_back(content::ManifestEntry{.id = id,
                                                     .type = type,
                                                     .representation = representation,
                                                     .byteLength = bytes.size(),
                                                     .digest = kDigest,
                                                     .locator = kLocator});
            Value made = Value::object();
            made.add("resourceId", Value::string(hexOf(id.value)));
            if (!subasset.empty()) {
                made.add("subasset", Value::string(std::string{subasset}));
            }
            made.add("representation", Value::string(std::string{representation.text()}));
            made.add("digest", Value::string(kDigest.text()));
            made.add("byteLength", Value::integer(static_cast<std::int64_t>(bytes.size())));
            artifacts.push(std::move(made));
            return true;
        };
        bool published = kPublished(planned.id, artifact->type, artifact->representation, artifact->bytes, "");
        for (const Subasset& subasset : artifact->subassets) {
            published = published && kPublished(planned.subassets.find(subasset.key)->second,
                                                subasset.type,
                                                subasset.representation,
                                                subasset.bytes,
                                                subasset.key);
        }
        if (!published) {
            continue;
        }
        (kReused ? report.reused : report.cooked) += 1;
        Value input = Value::object();
        input.add("resourceId", Value::string(hexOf(planned.id.value)));
        input.add("source", Value::string(planned.source));
        input.add("importer", Value::string(std::string{planned.importer->identity}));
        input.add("sourceDigest", Value::string("sha256:" + hexOf(planned.digest)));
        input.add("reads", valueOf(readsMade));
        if (!artifact->report.empty()) {
            input.add("report", valueOf(artifact->report));
        }
        for (const auto& [kName, kCount] : artifact->report) {
            report.variants += kName == "variants" ? kCount : 0;
        }
        input.add("key", Value::string("sha256:" + kKey));
        input.add("reused", Value::boolean(kReused));
        inputs.push(std::move(input));
    }
    // SPEC-0026's variants_per_package, across every material cooked.
    if (report.variants > request.maximumVariants) {
        report.failures.push_back(failure(CookError::OverLimit, "more material variants than the package's ceiling", "")
                                      .withContext("variants", std::to_string(report.variants)));
    }
    // A subasset's resource is no other's.
    std::ranges::sort(entries, {}, &content::ManifestEntry::id);
    for (std::size_t index = 1; index < entries.size() && report.failures.empty(); ++index) {
        if (entries[index].id == entries[index - 1].id) {
            report.failures.push_back(failure(CookError::DuplicateResource,
                                              "two sources or subassets claim one resource",
                                              hexOf(entries[index].id.value)));
        }
    }
    if (!report.failures.empty()) {
        return report;
    }
    // The receipt names the exact manifest it proves, by digest.
    const std::string kManifest = content::writeManifest(entries);
    const auto kManifestBytes = std::as_bytes(std::span{kManifest.data(), kManifest.size()});
    Value receipt = Value::object();
    receipt.add("kind", Value::string("cook.receipt"));
    receipt.add("formatVersion", Value::integer(1));
    receipt.add("toolchain", Value::string("sha256:" + hexOf(request.toolchain)));
    receipt.add("target", Value::string(request.target));
    receipt.add("determinism", Value::string("double_cook"));
    receipt.add("manifest", Value::string(content::ContentDigest::of(kManifestBytes).text()));
    receipt.add("variants", Value::integer(report.variants));
    receipt.add("variantsHeadroom", Value::integer(request.maximumVariants - report.variants));
    receipt.add("inputs", std::move(inputs));
    receipt.add("artifacts", std::move(artifacts));
    receipt.add("failures", Value::integer(0));
    // The old receipt out, the manifest in, the receipt last: a receipt is
    // only ever beside the manifest it proves.
    std::filesystem::remove(kOutput / "cook.receipt", error);
    if (!writeText(kOutput / "content.manifest", kManifest) ||
        !writeText(kOutput / "cook.receipt", document::write(receipt))) {
        report.failures.push_back(failure(CookError::WriteFailed, "the manifest or receipt cannot be written", ""));
    }
    return report;
}

result::Result<MapReport> mapSubassets(const std::filesystem::path& sources,
                                       std::span<const Importer> importers,
                                       const std::function<content::ResourceId()>& fresh) {
    std::error_code error;
    const std::filesystem::path kSources = std::filesystem::canonical(sources, error);
    if (error || !std::filesystem::is_directory(kSources)) {
        return std::unexpected<result::Error>{
            failure(CookError::BadRequest, "the sources are not a directory", sources.string())};
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kSidecars, sidecarsUnder(kSources));
    MapReport report;
    for (const std::string& path : kSidecars) {
        const auto kText = readFile(kSources / path);
        auto read =
            kText.has_value()
                ? content::readSidecar(std::string_view{reinterpret_cast<const char*>(kText->data()), kText->size()})
                : result::Result<content::Sidecar>{std::unexpected<result::Error>{
                      failure(CookError::BadSidecar, "the sidecar cannot be read", path)}};
        if (!read.has_value()) {
            report.failures.push_back(std::move(read).error().withContext("path", path));
            continue;
        }
        const auto kImporter = std::ranges::find(importers, read->importer, &Importer::identity);
        if (kImporter == importers.end()) {
            report.failures.push_back(failure(CookError::UnknownImporter, "no importer of that identity", path));
            continue;
        }
        auto settings = kImporter->normalize(read->settings ? &*read->settings : nullptr);
        const std::string kSource = path.substr(0, path.size() - content::kSidecarSuffix.size());
        const auto kBytes = readFile(kSources / kSource);
        if (!settings.has_value() || !kBytes.has_value()) {
            report.failures.push_back(settings.has_value()
                                          ? failure(CookError::MissingSource, "the sidecar's source is missing", path)
                                          : std::move(settings).error().withContext("path", path));
            continue;
        }
        // One cook, giving what it asks of an identity and what it makes of
        // a subasset one; the artifacts are left.
        Reads reads{kSources, std::filesystem::path{kSource}.parent_path(), read->subassets};
        reads.assignWith(fresh);
        auto cooked = kImporter->cook(*kBytes, *settings, reads);
        if (!cooked.has_value()) {
            report.failures.push_back(std::move(cooked).error().withContext("path", kSource));
            continue;
        }
        for (const Subasset& subasset : cooked->subassets) {
            static_cast<void>(reads.subasset(subasset.key));
        }
        if (reads.assigned().empty()) {
            continue;
        }
        std::vector<std::string> added;
        for (const auto& [kKey, kId] : reads.assigned()) {
            read->subassets.emplace(kKey, kId);
            added.push_back(kKey);
        }
        if (!writeText(kSources / path, content::writeSidecar(*read))) {
            report.failures.push_back(failure(CookError::WriteFailed, "the sidecar cannot be written", path));
            continue;
        }
        report.written.emplace_back(path, std::move(added));
    }
    return report;
}

} // namespace rawframe::cook
