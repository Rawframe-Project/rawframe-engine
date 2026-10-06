// The authoring tool (ADR-0032, SPEC-0040, D152): the operation surface from
// a command line, run by an author, a pipeline, or an agent; never part of a
// client or a server.
//
//   rawframe-author describe [<game description>]
//   rawframe-author apply <game description> <scene> <request> [--dry-run]
//   rawframe-author read <game description> <scene> <queries>
//   rawframe-author migrate <game description> <scene>... [--dry-run]
//
// `describe` writes the discovery document; given a game, with the game's
// components, so a request can name them by type id. `apply` reads the scene,
// builds its component catalog from the game (each component's layout from
// the game's program, an `entity` field a reference), runs the request, and
// writes one outcome: the document's generation after it, whether the
// scene was written, and a result per operation slot, each a count of
// deltas or the one error record. Between processes a document's
// generation is its content digest, which a request's `expects` names. The
// scene is replaced whole, by rename, only when something changed and not
// with `--dry-run`. The scenes an instance may name are those beside the
// game description, by the identity their sidecars give them, and the
// scene's own sidecar gives the document's.
//
// `read` answers a query document (D155) against the scene: the document's
// generation read, and per query its answer or the one error record, each
// on its own. It never writes.
//
// `migrate` brings scenes authored against older layouts of the game's
// components to the current ones (ADR-0067, D153): per scene, every
// component whose recorded mark differs is remarked in one atomic
// transaction, and the report gives each scene's verdict, `unchanged`,
// `migrated`, `refused` (with the fields that do not carry over, or the
// component the game no longer has), or `failed`. A refused scene is left
// as it was; nothing is dropped.
//
// `session` holds an authoring session on standard input and output
// (D407, authoring/session.h): records a line, the game read once, scenes
// under the scene root (the game's directory unless named) opened as
// records name them and kept open with their undo histories, each change
// written to its file as it commits, and each with its selection, which
// `select` sets and undo and redo put back (D417), and its view, which
// `view` sets and undo and redo put back too (D432). A scene whose file
// changed under the session is opened again, its history and selection
// let go, and the answer says so. `preview` connects the session to a
// running client's tooling endpoint granted `view` (D433), which then shows
// one scene's game from that scene's view, handed it each time it changes;
// the endpoint is a loopback address, so the token the record's file holds
// goes to no other machine.
//
// `connect` is a client of a running Runtime's tooling endpoint (D408,
// connect.h): records a line on standard input, the replies on standard
// output, the hello said for it with the token from its file.
//
// Exit status 0 when everything succeeded, 1 otherwise, and 2 for a usage
// error.

#include "connect.h"
#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/authoring/queries.h"
#include "rawframe/authoring/request.h"
#include "rawframe/authoring/session.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/base/sha256.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/document/json.h"
#include "rawframe/scene/scene.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace authoring = rawframe::authoring;
namespace result = rawframe::result;
using rawframe::authoring_session::catalogOf;
using rawframe::authoring_session::digestOf;
using rawframe::authoring_session::readFile;
using rawframe::authoring_session::scenesBeside;
using rawframe::authoring_session::sidecarIdentity;
using rawframe::authoring_session::slotValue;
using rawframe::document::Value;

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
    auto files =
        rawframe::world_kest::GameFiles::fromDirectory(game, nullptr, rawframe::world_kest::MeshReading::Named);
    if (!files.has_value()) {
        return refused(files.error());
    }
    auto catalog = catalogOf(*files);
    if (!catalog.has_value()) {
        return refused(catalog.error());
    }
    auto scene =
        authoring::AuthoredScene::open(sidecarIdentity(scenePath).value_or(rawframe::base::Bits128{}), *kSceneText);
    if (!scene.has_value()) {
        return refused(scene.error());
    }
    authoring::AuthoredScene& document = **scene;
    const auto kBeside = scenesBeside(game);
    const rawframe::scene::SceneSource kSources =
        [&kBeside](rawframe::base::Bits128 id) -> result::Result<rawframe::scene::Scene> {
        const auto kFound =
            std::ranges::find(kBeside, id, &std::pair<rawframe::base::Bits128, std::filesystem::path>::first);
        const auto kText = kFound != kBeside.end() ? readFile(kFound->second) : std::nullopt;
        if (!kText.has_value()) {
            return result::fail(result::ErrorClass::NotFound,
                                authoring::kAuthoringDomain,
                                code(authoring::AuthoringError::TargetNotFound),
                                "no scene beside the game has that identity");
        }
        return rawframe::scene::readScene(*kText);
    };
    Value results = Value::array();
    bool failed = false;
    std::size_t skipped = 0;
    if (request->batch == authoring::Batch::Atomic) {
        const auto kOutcome = authoring::executeAtomic(document, 0, request->operations, *catalog, &kSources);
        failed = !kOutcome.has_value();
        results.push(slotValue(kOutcome));
    } else {
        const authoring::IndependentOutcome kOutcomes = authoring::executeIndependent(
            document,
            0,
            request->operations,
            *catalog,
            request->batch == authoring::Batch::HaltRemaining ? authoring::OnFailure::HaltRemaining
                                                              : authoring::OnFailure::ContinuePerItem,
            &kSources);
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

int describe(const char* game) {
    auto files =
        rawframe::world_kest::GameFiles::fromDirectory(game, nullptr, rawframe::world_kest::MeshReading::Named);
    if (!files.has_value()) {
        return refused(files.error());
    }
    auto catalog = catalogOf(*files);
    if (!catalog.has_value()) {
        return refused(catalog.error());
    }
    std::fputs(authoring::writeDiscovery(&*catalog).c_str(), stdout);
    return 0;
}

int answerQueries(const char* game, const std::filesystem::path& scenePath, const char* queriesPath) {
    const auto kQueriesText = readFile(queriesPath);
    const auto kSceneText = readFile(scenePath);
    if (!kQueriesText.has_value() || !kSceneText.has_value()) {
        return refused(result::fail(result::ErrorClass::NotFound,
                                    authoring::kAuthoringDomain,
                                    code(authoring::AuthoringError::TargetNotFound),
                                    "the queries and the scene are files that read")
                           .error());
    }
    auto queries = authoring::readQueries(*kQueriesText);
    if (!queries.has_value()) {
        return refused(queries.error());
    }
    auto files =
        rawframe::world_kest::GameFiles::fromDirectory(game, nullptr, rawframe::world_kest::MeshReading::Named);
    if (!files.has_value()) {
        return refused(files.error());
    }
    auto catalog = catalogOf(*files);
    if (!catalog.has_value()) {
        return refused(catalog.error());
    }
    auto scene = rawframe::scene::readScene(*kSceneText);
    if (!scene.has_value()) {
        return refused(scene.error());
    }
    Value answers = Value::array();
    bool failed = false;
    for (const authoring::Query& query : *queries) {
        const auto kAnswer = authoring::answer(*scene, query, *catalog);
        Value made = Value::object();
        if (kAnswer.has_value()) {
            made.add("answer", authoring::answerValue(*kAnswer));
        } else {
            failed = true;
            made.add("error", authoring::errorRecord(kAnswer.error()));
        }
        answers.push(std::move(made));
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.answers"));
    made.add("document", Value::string(digestOf(*kSceneText)));
    made.add("answers", std::move(answers));
    std::fputs(rawframe::document::write(made).c_str(), stdout);
    return failed ? 1 : 0;
}

int migrate(const char* game, std::span<char* const> scenes, bool dryRun) {
    auto files =
        rawframe::world_kest::GameFiles::fromDirectory(game, nullptr, rawframe::world_kest::MeshReading::Named);
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

/// One line of `in` without its line feed; false at the input's end.
/// A line past the record limit is read through and left that long, so
/// it is refused whole.
bool readLine(std::FILE* in, std::string& line) {
    line.clear();
    int each = 0;
    bool any = false;
    while ((each = std::fgetc(in)) != EOF) {
        any = true;
        if (each == '\n') {
            return true;
        }
        if (line.size() <= authoring::kMaximumSessionRecordBytes) {
            line.push_back(static_cast<char>(each));
        }
    }
    return any;
}

/// `session`: records from standard input answered on standard output
/// until `end` or the input ends; 0 when every record succeeded.
int session(const std::filesystem::path& game, const std::filesystem::path& root) {
    rawframe::authoring_session::Session held{game, root};
    std::string line;
    while (readLine(stdin, line)) {
        bool ended = false;
        std::fputs(held.answer(line, ended).c_str(), stdout);
        std::fflush(stdout);
        if (ended) {
            break;
        }
    }
    return held.clean() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view kVerb = argc >= 2 ? argv[1] : "";
    if (kVerb == "describe" && argc == 2) {
        std::fputs(authoring::writeDiscovery().c_str(), stdout);
        return 0;
    }
    if (kVerb == "describe" && argc == 3) {
        return describe(argv[2]);
    }
    if (kVerb == "read" && argc == 5) {
        return answerQueries(argv[2], argv[3], argv[4]);
    }
    const bool kDryRun = argc == 6 && std::string_view{argv[5]} == "--dry-run";
    if (kVerb == "apply" && (argc == 5 || kDryRun)) {
        return apply(argv[2], argv[3], argv[4], kDryRun);
    }
    if (kVerb == "connect" && argc == 5) {
        return rawframe::author::connect(argv[2], argv[3], argv[4]);
    }
    if (kVerb == "session" && (argc == 3 || argc == 4)) {
        const std::filesystem::path kGame = argv[2];
        return session(kGame, argc == 4 ? std::filesystem::path{argv[3]} : kGame.parent_path());
    }
    if (kVerb == "migrate" && argc >= 4) {
        const bool kDry = std::string_view{argv[argc - 1]} == "--dry-run";
        const std::span<char* const> kScenes{argv + 3, static_cast<std::size_t>(argc - 3 - (kDry ? 1 : 0))};
        if (!kScenes.empty()) {
            return migrate(argv[2], kScenes, kDry);
        }
    }
    std::fputs("usage: rawframe-author describe [<game description>]\n"
               "       rawframe-author apply <game description> <scene> <request> [--dry-run]\n"
               "       rawframe-author read <game description> <scene> <queries>\n"
               "       rawframe-author migrate <game description> <scene>... [--dry-run]\n"
               "       rawframe-author session <game description> [<scene root>]\n"
               "       rawframe-author connect <endpoint> <pin file> <token file>\n",
               stderr);
    return 2;
}
