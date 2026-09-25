#include "rawframe/content/composition_record.h"

#include "rawframe/content/errors.h"
#include "rawframe/content/identity.h"
#include "rawframe/content/product.h"
#include "rawframe/document/json.h"

#include <algorithm>

namespace rawframe::content {

namespace {

using document::Value;

constexpr std::size_t kMaximumRecord = std::size_t{1024} * 1024;
constexpr std::size_t kMaximumMods = 256;
constexpr std::size_t kMaximumPackages = 4'096;

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kContentDomain, code(ContentError::ManifestInvalid), why)
            .error()};
}

bool profileToken(std::string_view text) noexcept {
    return !text.empty() && text.size() <= 64 && std::ranges::all_of(text, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_' || each == '.' ||
               each == '-';
    });
}

result::Result<BuildReference> referenceOf(const Value& value) {
    const Value* subject = value.find("subject");
    const Value* version = value.find("version");
    const Value* build = value.find("build");
    if (value.kind() != Value::Kind::Object || value.names().size() != 3 || subject == nullptr || version == nullptr ||
        build == nullptr || subject->kind() != Value::Kind::String || version->kind() != Value::Kind::String ||
        build->kind() != Value::Kind::String || !validSubject(*subject->text()) || !validVersion(*version->text())) {
        return refuse("a Build reference is {subject, version, build} in their grammars");
    }
    const auto kRoot = ContentDigest::parse(*build->text());
    if (!kRoot.has_value()) {
        return refuse("a Build reference's build is a root hash");
    }
    return BuildReference{.subject = *subject->text(), .version = *version->text(), .build = kRoot->bytes};
}

result::Result<std::vector<BuildReference>> referencesOf(const Value* list, std::size_t maximum) {
    if (list == nullptr || list->kind() != Value::Kind::Array || list->items().size() > maximum) {
        return refuse("mods and packages are lists within their ceilings");
    }
    std::vector<BuildReference> made;
    for (const Value& each : list->items()) {
        RAWFRAME_TRY_ASSIGN(BuildReference reference, referenceOf(each));
        if (!made.empty() && made.back().subject >= reference.subject) {
            return refuse("a list is in subject order, each subject once");
        }
        made.push_back(std::move(reference));
    }
    return made;
}

Value valueOf(const BuildReference& reference) {
    Value value = Value::object();
    value.add("subject", Value::string(reference.subject));
    value.add("version", Value::string(reference.version));
    value.add("build", Value::string(ContentDigest{.bytes = reference.build}.text()));
    return value;
}

} // namespace

result::Result<CompositionRecord> readComposition(std::string_view text) {
    if (text.size() > kMaximumRecord) {
        return refuse("a CompositionRecord is at most 1 MiB");
    }
    auto parsed = document::parseCanonicalRecord(text, document::ReadLimits{.maximumBytes = kMaximumRecord});
    if (!parsed.has_value()) {
        return refuse("a CompositionRecord is a canonical record");
    }
    const Value& record = *parsed;
    const Value* schema = record.find("schema");
    const Value* game = record.find("game");
    const Value* profile = record.find("profile");
    const Value* created = record.find("created_at");
    if (record.names().size() != 6 || schema == nullptr || schema->integer() != 1 || game == nullptr ||
        profile == nullptr || profile->kind() != Value::Kind::String || !profileToken(*profile->text()) ||
        created == nullptr || !created->integer().has_value()) {
        return refuse("a CompositionRecord is {schema: 1, game, mods, packages, profile, created_at}");
    }
    CompositionRecord made;
    RAWFRAME_TRY_ASSIGN(made.game, referenceOf(*game));
    RAWFRAME_TRY_ASSIGN(made.mods, referencesOf(record.find("mods"), kMaximumMods));
    RAWFRAME_TRY_ASSIGN(made.packages, referencesOf(record.find("packages"), kMaximumPackages));
    made.profile = *profile->text();
    made.createdAt = *created->integer();
    return made;
}

result::Result<std::string> writeComposition(const CompositionRecord& record) {
    Value mods = Value::array();
    for (const BuildReference& each : record.mods) {
        mods.push(valueOf(each));
    }
    Value packages = Value::array();
    for (const BuildReference& each : record.packages) {
        packages.push(valueOf(each));
    }
    Value value = Value::object();
    value.add("schema", Value::integer(1));
    value.add("game", valueOf(record.game));
    value.add("mods", std::move(mods));
    value.add("packages", std::move(packages));
    value.add("profile", Value::string(record.profile));
    value.add("created_at", Value::integer(record.createdAt));
    RAWFRAME_TRY_ASSIGN(std::string text, document::writeCanonicalRecord(value));
    // What is written must read back: the grammar is one.
    RAWFRAME_TRY(readComposition(text));
    return text;
}

base::Sha256Digest compositionIdOf(std::string_view text) noexcept {
    return base::sha256(text);
}

} // namespace rawframe::content
