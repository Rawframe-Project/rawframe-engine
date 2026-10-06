#include "rawframe/authoring_session/game.h"

#include "rawframe/authoring/request.h"
#include "rawframe/base/sha256.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/kest/program.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace rawframe::authoring_session {

namespace {

using document::Value;

/// The importer a scene's sidecar names.
constexpr std::string_view kSceneImporter = "rawframe.scene";

std::optional<authoring::FieldKind> kindOf(rawframe::kest::FieldKind kind) {
    using K = rawframe::kest::FieldKind;
    switch (kind) {
    case K::I8:
    case K::I16:
    case K::I32:
    case K::I64:
        return authoring::FieldKind::Signed;
    case K::U8:
    case K::U16:
    case K::U32:
    case K::U64:
        return authoring::FieldKind::Unsigned;
    case K::F32:
    case K::F64:
        return authoring::FieldKind::Real;
    case K::Bool:
        return authoring::FieldKind::Truth;
    case K::Tag:
        return authoring::FieldKind::Case;
    // A case's data is the game's to set, not a scene's (D271).
    case K::Payload:
    case K::Other:
        return std::nullopt;
    }
    return std::nullopt;
}

/// The game's components as authoring knows them. A type the program never
/// lays out is not one a scene can hold, and is left out.

} // namespace

std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

std::string digestOf(std::string_view text) {
    const rawframe::base::Sha256Digest kDigest = rawframe::base::sha256(text);
    std::string made = "sha256:";
    for (const std::byte each : kDigest) {
        const auto kByte = std::to_integer<unsigned>(each);
        made += "0123456789abcdef"[kByte >> 4U];
        made += "0123456789abcdef"[kByte & 0xFU];
    }
    return made;
}

result::Result<authoring::ComponentCatalog> catalogOf(const rawframe::world_kest::GameFiles& files) {
    const rawframe::world_kest::GameDescription& game = files.description();
    RAWFRAME_TRY_ASSIGN(const auto kProgram, files.compile(game.program));
    authoring::ComponentCatalog catalog;
    for (const rawframe::world_kest::GameComponent& component : game.components) {
        const auto kLayout = rawframe::world_kest::componentLayout(game, *kProgram, component);
        if (!kLayout.has_value()) {
            continue;
        }
        authoring::ComponentSchema schema{
            .id = component.id, .name = component.name, .mark = kLayout->mark, .fields = {}};
        std::vector<std::string> references;
        for (const rawframe::world_kest::GameEntityField& field : game.entityFields) {
            if (field.component == component.name) {
                references.push_back(field.field);
                schema.fields.push_back(
                    authoring::FieldSchema{.name = field.field, .kind = authoring::FieldKind::Reference});
            }
        }
        for (const rawframe::kest::Field& field : kLayout->fields) {
            const bool kPartOfReference = std::ranges::any_of(references, [&field](const std::string& reference) {
                return field.name.starts_with(reference + ".");
            });
            const std::optional<authoring::FieldKind> kKind = kindOf(field.kind);
            if (!kPartOfReference && kKind.has_value()) {
                schema.fields.push_back(authoring::FieldSchema{
                    .name = field.name,
                    .kind = *kKind,
                    .cases = *kKind == authoring::FieldKind::Case ? field.cases : std::vector<std::string>{}});
            }
        }
        RAWFRAME_TRY(catalog.add(std::move(schema)));
    }
    return catalog;
}

Value slotValue(const result::Result<authoring::Committed>& outcome) {
    Value made = Value::object();
    if (outcome.has_value()) {
        made.add("deltas", Value::integer(static_cast<std::int64_t>(outcome->deltas)));
    } else {
        made.add("error", authoring::errorRecord(outcome.error()));
    }
    return made;
}

/// The identity a scene's sidecar gives it, if it has one that reads.
std::optional<rawframe::base::Bits128> sidecarIdentity(const std::filesystem::path& source) {
    const auto kText = readFile(source.string() + std::string{rawframe::content::kSidecarSuffix});
    if (!kText.has_value()) {
        return std::nullopt;
    }
    const auto kSidecar = rawframe::content::readSidecar(*kText);
    if (!kSidecar.has_value() || kSidecar->importer != kSceneImporter) {
        return std::nullopt;
    }
    return kSidecar->id.value;
}

/// Every scene under the game description's directory, by the identity its
/// sidecar gives it.
std::vector<std::pair<rawframe::base::Bits128, std::filesystem::path>> scenesBeside(const std::filesystem::path& game) {
    std::vector<std::pair<rawframe::base::Bits128, std::filesystem::path>> made;
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator{game.parent_path(), error};
         !error && entry != std::filesystem::recursive_directory_iterator{};
         entry.increment(error)) {
        if (entry->is_regular_file() && entry->path().extension() == ".scene") {
            const auto kIdentity = sidecarIdentity(entry->path());
            if (kIdentity.has_value()) {
                made.emplace_back(*kIdentity, entry->path());
            }
        }
    }
    return made;
}

} // namespace rawframe::authoring_session
