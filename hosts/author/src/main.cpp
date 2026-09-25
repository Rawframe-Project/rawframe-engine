// The authoring tool (ADR-0032, SPEC-0040, D152): the operation surface from
// a command line, run by an author, a pipeline, or an agent; never part of a
// client or a server.
//
//   rawframe-author describe
//   rawframe-author apply <game description> <scene> <request> [--dry-run]
//   rawframe-author migrate <game description> <scene>... [--dry-run]
//
// `describe` writes the discovery document. `apply` reads the scene,
// builds its component catalog from the game (each component's layout from
// the game's program, an `entity` field a reference), runs the request, and
// writes one outcome: the document's generation after it, whether the
// scene was written, and a result per operation slot, each a count of
// deltas or the one error record. Between processes a document's
// generation is its content digest, which a request's `expects` names. The
// scene is replaced whole, by rename, only when something changed and not
// with `--dry-run`.
//
// `migrate` brings scenes authored against older layouts of the game's
// components to the current ones (ADR-0067, D153): per scene, every
// component whose recorded mark differs is remarked in one atomic
// transaction, and the report gives each scene's verdict, `unchanged`,
// `migrated`, `refused` (with the fields that do not carry over, or the
// component the game no longer has), or `failed`. A refused scene is left
// as it was; nothing is dropped.
//
// Exit status 0 when everything succeeded, 1 otherwise, and 2 for a usage
// error.

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
#include <memory>
#include <span>
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

int migrate(const char* game, std::span<char* const> scenes, bool dryRun) {
    auto files = rawframe::world_kest::GameFiles::fromDirectory(game);
    if (!files.has_value()) {
        return refused(files.error());
    }
    auto catalog = catalogOf(*files);
    if (!catalog.has_value()) {
        return refused(catalog.error());
    }
    Value documents = Value::array();
    bool clean = true;
    for (const char* each : scenes) {
        const std::filesystem::path kPath = each;
        Value report = Value::object();
        report.add("path", Value::string(kPath.string()));
        const auto kText = readFile(kPath);
        auto scene = kText.has_value() ? authoring::AuthoredScene::open(rawframe::base::Bits128{}, *kText)
                                       : result::Result<std::unique_ptr<authoring::AuthoredScene>>{
                                             result::fail(result::ErrorClass::NotFound,
                                                          authoring::kAuthoringDomain,
                                                          code(authoring::AuthoringError::TargetNotFound),
                                                          "the scene is a file that reads")};
        if (!scene.has_value()) {
            report.add("verdict", Value::string("failed"));
            report.add("error", authoring::errorRecord(scene.error()));
            documents.push(std::move(report));
            clean = false;
            continue;
        }
        std::vector<authoring::Operation> remarks;
        std::string unknown;
        for (const rawframe::scene::SchemaMark& mark : (*scene)->scene().schema) {
            const authoring::ComponentSchema* kNow = catalog->findNamed(mark.component);
            if (kNow == nullptr) {
                unknown += (unknown.empty() ? "" : " ") + mark.component;
            } else if (kNow->mark != mark.mark) {
                remarks.emplace_back(authoring::RemarkComponent{.component = kNow->id});
            }
        }
        if (!unknown.empty()) {
            report.add("verdict", Value::string("refused"));
            report.add("error",
                       authoring::errorRecord(result::fail(result::ErrorClass::InvalidArgument,
                                                           authoring::kAuthoringDomain,
                                                           code(authoring::AuthoringError::ValidationFailed),
                                                           "every component a scene records is one the game has")
                                                  .error()
                                                  .withContext("unknown", unknown)));
            documents.push(std::move(report));
            clean = false;
            continue;
        }
        if (remarks.empty()) {
            report.add("verdict", Value::string("unchanged"));
            documents.push(std::move(report));
            continue;
        }
        const auto kOutcome = authoring::executeAtomic(**scene, 0, remarks, *catalog);
        if (!kOutcome.has_value()) {
            report.add("verdict", Value::string("refused"));
            report.add("error", authoring::errorRecord(kOutcome.error()));
            documents.push(std::move(report));
            clean = false;
            continue;
        }
        if (!dryRun) {
            const std::filesystem::path kStaged = kPath.string() + ".authoring";
            std::ofstream{kStaged, std::ios::binary | std::ios::trunc} << (*scene)->text();
            std::error_code renamed;
            std::filesystem::rename(kStaged, kPath, renamed);
            if (renamed) {
                report.add("verdict", Value::string("failed"));
                documents.push(std::move(report));
                clean = false;
                continue;
            }
        }
        report.add("verdict", Value::string("migrated"));
        report.add("components", Value::integer(static_cast<std::int64_t>(remarks.size())));
        documents.push(std::move(report));
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.migration"));
    made.add("dryRun", Value::boolean(dryRun));
    made.add("documents", std::move(documents));
    std::fputs(rawframe::document::write(made).c_str(), stdout);
    return clean ? 0 : 1;
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
    if (kVerb == "migrate" && argc >= 4) {
        const bool kDry = std::string_view{argv[argc - 1]} == "--dry-run";
        const std::span<char* const> kScenes{argv + 3, static_cast<std::size_t>(argc - 3 - (kDry ? 1 : 0))};
        if (!kScenes.empty()) {
            return migrate(argv[2], kScenes, kDry);
        }
    }
    std::fputs("usage: rawframe-author describe\n"
               "       rawframe-author apply <game description> <scene> <request> [--dry-run]\n"
               "       rawframe-author migrate <game description> <scene>... [--dry-run]\n",
               stderr);
    return 2;
}
