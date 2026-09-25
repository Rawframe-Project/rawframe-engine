// The authoring tool (ADR-0032, SPEC-0040, D152): the operation surface from
// a command line, run by an author, a pipeline, or an agent; never part of a
// client or a server.
//
//   rawframe-author describe
//   rawframe-author apply <game description> <scene> <request> [--dry-run]
//
// `describe` writes the discovery document. `apply` reads the scene,
// builds its component catalog from the game (each component's layout from
// the game's program, an `entity` field a reference), runs the request, and
// writes one outcome: the document's generation after it, whether the
// scene was written, and a result per operation slot, each a count of
// deltas or the one error record. Between processes a document's
// generation is its content digest, which a request's `expects` names. The
// scene is replaced whole, by rename, only when something changed and not
// with `--dry-run`. Exit status 0 when every slot succeeded, 1 otherwise,
// and 2 for a usage error.

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/authoring/request.h"
#include "rawframe/base/sha256.h"
#include "rawframe/document/json.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace authoring = rawframe::authoring;
namespace result = rawframe::result;
using rawframe::document::Value;

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
    case K::Other:
        return std::nullopt;
    }
    return std::nullopt;
}

/// The game's components as authoring knows them. A type the program never
/// lays out is not one a scene can hold, and is left out.
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
                schema.fields.push_back(authoring::FieldSchema{.name = field.name, .kind = *kKind});
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

/// The outcome of a request that could not be run at all.
int refused(const result::Error& error) {
    Value made = Value::object();
    made.add("kind", Value::string("authoring.outcome"));
    made.add("error", authoring::errorRecord(error));
    std::fputs(rawframe::document::write(made).c_str(), stdout);
    return 1;
}

int apply(const char* game, const std::filesystem::path& scenePath, const char* requestPath, bool dryRun) {
    const auto kRequestText = readFile(requestPath);
    const auto kSceneText = readFile(scenePath);
    if (!kRequestText.has_value() || !kSceneText.has_value()) {
        return refused(result::fail(result::ErrorClass::NotFound,
                                    authoring::kAuthoringDomain,
                                    code(authoring::AuthoringError::TargetNotFound),
                                    "the request and the scene are files that read")
                           .error());
    }
    auto request = authoring::readRequest(*kRequestText);
    if (!request.has_value()) {
        return refused(request.error());
    }
    // Between processes, a document's generation is its digest.
    const std::string kBefore = digestOf(*kSceneText);
    if (request->expects.has_value() && *request->expects != kBefore) {
        return refused(result::fail(result::ErrorClass::FailedPrecondition,
                                    authoring::kAuthoringDomain,
                                    code(authoring::AuthoringError::TargetStale),
                                    "the request was computed against another generation of the scene")
                           .error()
                           .withContext("document", kBefore));
    }
    auto files = rawframe::world_kest::GameFiles::fromDirectory(game);
    if (!files.has_value()) {
        return refused(files.error());
    }
    auto catalog = catalogOf(*files);
    if (!catalog.has_value()) {
        return refused(catalog.error());
    }
    auto scene = authoring::AuthoredScene::open(rawframe::base::Bits128{}, *kSceneText);
    if (!scene.has_value()) {
        return refused(scene.error());
    }
    authoring::AuthoredScene& document = **scene;
    Value results = Value::array();
    bool failed = false;
    std::size_t skipped = 0;
    if (request->batch == authoring::Batch::Atomic) {
        const auto kOutcome = authoring::executeAtomic(document, 0, request->operations, *catalog);
        failed = !kOutcome.has_value();
        results.push(slotValue(kOutcome));
    } else {
        const authoring::IndependentOutcome kOutcomes = authoring::executeIndependent(
            document,
            0,
            request->operations,
            *catalog,
            request->batch == authoring::Batch::HaltRemaining ? authoring::OnFailure::HaltRemaining
                                                              : authoring::OnFailure::ContinuePerItem);
        for (const auto& each : kOutcomes.outcomes) {
            failed = failed || !each.has_value();
            results.push(slotValue(each));
        }
        skipped = kOutcomes.skipped;
    }
    const std::string kAfter = document.text();
    const bool kWrite = !dryRun && kAfter != *kSceneText;
    if (kWrite) {
        const std::filesystem::path kStaged = scenePath.string() + ".authoring";
        std::ofstream{kStaged, std::ios::binary | std::ios::trunc} << kAfter;
        std::error_code renamed;
        std::filesystem::rename(kStaged, scenePath, renamed);
        if (renamed) {
            return refused(result::fail(result::ErrorClass::Unavailable,
                                        authoring::kAuthoringDomain,
                                        code(authoring::AuthoringError::Internal),
                                        "the scene could not be replaced")
                               .error());
        }
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.outcome"));
    made.add("document", Value::string(dryRun ? kBefore : digestOf(kAfter)));
    made.add("written", Value::boolean(kWrite));
    made.add("results", std::move(results));
    made.add("skipped", Value::integer(static_cast<std::int64_t>(skipped)));
    std::fputs(rawframe::document::write(made).c_str(), stdout);
    return failed ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view kVerb = argc >= 2 ? argv[1] : "";
    if (kVerb == "describe" && argc == 2) {
        std::fputs(authoring::writeDiscovery().c_str(), stdout);
        return 0;
    }
    const bool kDryRun = argc == 6 && std::string_view{argv[5]} == "--dry-run";
    if (kVerb == "apply" && (argc == 5 || kDryRun)) {
        return apply(argv[2], argv[3], argv[4], kDryRun);
    }
    std::fputs("usage: rawframe-author describe\n"
               "       rawframe-author apply <game description> <scene> <request> [--dry-run]\n",
               stderr);
    return 2;
}
