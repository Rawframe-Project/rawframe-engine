#include "rawframe/authoring_session/session.h"

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/delta.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/authoring/queries.h"
#include "rawframe/authoring/request.h"
#include "rawframe/authoring/session.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/link.h"
#include "rawframe/base/bits128.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/document/json.h"
#include "rawframe/scene/scene.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world_kest/game_files.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

namespace {

using document::Value;

/// A scene a session holds open: the document with its history, and the
/// digest of the bytes on disk as the session last read or wrote them.
struct OpenScene {
    std::unique_ptr<authoring::AuthoredScene> document;
    std::string onDisk;
};

/// An authoring session (D407): the game read once, its scenes opened as
/// records name them and kept open with their histories, each change
/// written to its file as it commits.
class Held {
public:
    Held(std::filesystem::path game, std::filesystem::path root)
        : game_(std::move(game)), root_(std::filesystem::weakly_canonical(root)) {
    }
    Held(const Held&) = delete;
    Held& operator=(const Held&) = delete;
    ~Held() {
        letGo();
    }

    /// One record answered: its reply line, its line feed included.
    std::string answer(std::string_view line, bool& ended) {
        Value id;
        ended = false;
        if (line.size() > authoring::kMaximumSessionRecordBytes) {
            clean_ = false;
            return authoring::writeRefusal(id, limitError());
        }
        auto record = authoring::readSessionRecord(line, id);
        if (!record.has_value()) {
            clean_ = false;
            return authoring::writeRefusal(id, record.error());
        }
        ended = record->verb == authoring::SessionVerb::End;
        auto answered = handle(*record);
        clean_ = clean_ && answered.has_value() && !failed_;
        return answered.has_value() ? authoring::writeReply(id, std::move(*answered))
                                    : authoring::writeRefusal(id, answered.error());
    }

    bool clean() const noexcept {
        return clean_;
    }

private:
    static result::Error limitError() {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            authoring::kAuthoringDomain,
                            code(authoring::AuthoringError::LimitExceeded),
                            "a session record is at most 16 MiB")
            .error();
    }

    static result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
        return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
    }

    result::Result<Value> handle(const authoring::SessionRecord& record) {
        failed_ = false;
        if (record.verb == authoring::SessionVerb::Hello) {
            if (record.surfaceGeneration != authoring::kSurfaceGeneration) {
                return std::unexpected{
                    failure(authoring::AuthoringError::UnsupportedOperation,
                            result::ErrorClass::InvalidArgument,
                            "the session speaks only its own surface generation until a stability promise")
                        .withContext("surfaceGeneration", std::to_string(authoring::kSurfaceGeneration))};
            }
            RAWFRAME_TRY(load());
            greeted_ = true;
            Value made = Value::object();
            made.add("kind", Value::string("authoring.welcome"));
            made.add("surfaceGeneration", Value::integer(authoring::kSurfaceGeneration));
            return made;
        }
        if (!greeted_) {
            return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                           result::ErrorClass::FailedPrecondition,
                                           "a session begins with hello")};
        }
        switch (record.verb) {
        case authoring::SessionVerb::Hello:
            break;
        case authoring::SessionVerb::Describe:
            return rawframe::document::parse(authoring::writeDiscovery(&*catalog_));
        case authoring::SessionVerb::End: {
            Value made = Value::object();
            made.add("kind", Value::string("authoring.ended"));
            return made;
        }
        case authoring::SessionVerb::Apply:
            return applied(record);
        case authoring::SessionVerb::Read:
            return answered(record);
        case authoring::SessionVerb::Undo:
        case authoring::SessionVerb::Redo:
            return stepped(record, record.verb == authoring::SessionVerb::Undo);
        case authoring::SessionVerb::Select:
            return selected(record);
        case authoring::SessionVerb::View:
            return viewed(record);
        case authoring::SessionVerb::Preview:
            return previewed(record);
        case authoring::SessionVerb::CreateScene:
            return created(record);
        case authoring::SessionVerb::History:
            return historyOf(record);
        }
        return std::unexpected{failure(
            authoring::AuthoringError::Internal, result::ErrorClass::Internal, "a session record went unhandled")};
    }

    /// The game's catalog and the scenes an instance may name, read once.
    result::Status load() {
        if (catalog_.has_value()) {
            return {};
        }
        auto files = rawframe::world_kest::GameFiles::fromDirectory(
            game_.string(), nullptr, rawframe::world_kest::MeshReading::Named);
        if (!files.has_value()) {
            return std::unexpected<result::Error>{std::move(files).error()};
        }
        RAWFRAME_TRY_ASSIGN(catalog_, catalogOf(*files));
        beside_ = scenesBeside(game_);
        return {};
    }

    /// Where the scene a record names is: a .scene file under the root.
    result::Result<std::filesystem::path> pathOf(const std::string& name) const {
        const std::filesystem::path kPath = std::filesystem::weakly_canonical(root_ / name);
        const auto [kRootEnd, kPathAt] = std::ranges::mismatch(root_, kPath);
        if (kRootEnd != root_.end() || kPath.extension() != ".scene") {
            return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                           result::ErrorClass::NotFound,
                                           "a session's scenes are .scene files under its root")
                                       .withContext("scene", name)};
        }
        return kPath;
    }

    /// The scene a record names, opened if it is not; reopened, its
    /// history let go, when its file changed under the session.
    result::Result<OpenScene*> sceneOf(const std::string& name, bool& reopened) {
        reopened = false;
        RAWFRAME_TRY_ASSIGN(const std::filesystem::path kPath, pathOf(name));
        const auto kText = readFile(kPath);
        if (!kText.has_value()) {
            return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                           result::ErrorClass::NotFound,
                                           "the scene is a file that reads")
                                       .withContext("scene", name)};
        }
        const std::string kDigest = digestOf(*kText);
        OpenScene& open = scenes_[kPath.string()];
        if (open.document != nullptr && open.onDisk == kDigest) {
            return &open;
        }
        reopened = open.document != nullptr;
        RAWFRAME_TRY_ASSIGN(
            open.document,
            authoring::AuthoredScene::open(sidecarIdentity(kPath).value_or(rawframe::base::Bits128{}), *kText));
        open.onDisk = kDigest;
        return &open;
    }

    /// Writes an open scene whose document changed, by rename.
    result::Result<bool> save(const std::string& name, OpenScene& open) {
        const std::string kText = open.document->text();
        if (digestOf(kText) == open.onDisk) {
            return false;
        }
        const std::filesystem::path kPath = std::filesystem::weakly_canonical(root_ / name);
        const std::filesystem::path kStaged = kPath.string() + ".authoring";
        std::ofstream{kStaged, std::ios::binary | std::ios::trunc} << kText;
        std::error_code renamed;
        std::filesystem::rename(kStaged, kPath, renamed);
        if (renamed) {
            return std::unexpected{failure(authoring::AuthoringError::Internal,
                                           result::ErrorClass::Unavailable,
                                           "the scene could not be replaced")};
        }
        open.onDisk = digestOf(kText);
        open.document->markSaved();
        return true;
    }

    rawframe::scene::SceneSource sources() const {
        return [this](rawframe::base::Bits128 id) -> result::Result<rawframe::scene::Scene> {
            const auto kFound =
                std::ranges::find(beside_, id, &std::pair<rawframe::base::Bits128, std::filesystem::path>::first);
            const auto kText = kFound != beside_.end() ? readFile(kFound->second) : std::nullopt;
            if (!kText.has_value()) {
                return result::fail(result::ErrorClass::NotFound,
                                    authoring::kAuthoringDomain,
                                    code(authoring::AuthoringError::TargetNotFound),
                                    "no scene beside the game has that identity");
            }
            return rawframe::scene::readScene(*kText);
        };
    }

    /// A scene's history (D454): each entry, oldest first, summed up, with
    /// how many deltas it holds and whether it is applied (undoable) or
    /// undone (redoable). A read: nothing staged, nothing written.
    result::Result<Value> historyOf(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        const authoring::AuthoredScene& scene = *open->document;
        Value entries = Value::array();
        for (std::size_t at = 0; at < scene.undoable() + scene.redoable(); ++at) {
            const authoring::Journal& journal = scene.journalAt(at);
            Value entry = Value::object();
            entry.add("summary", Value::string(authoring::summaryOf(journal)));
            entry.add("deltas", Value::integer(static_cast<std::int64_t>(journal.size())));
            entry.add("applied", Value::boolean(at < scene.undoable()));
            entries.push(std::move(entry));
        }
        Value made = Value::object();
        made.add("kind", Value::string("authoring.history"));
        made.add("reopened", Value::boolean(reopened));
        made.add("entries", std::move(entries));
        return made;
    }

    /// A new scene (D449): empty, beside a sidecar giving it a fresh
    /// resource identity, at a path under the root in a directory that is
    /// there, where neither is. A new document, so no history holds it; the
    /// scenes an instance may name gain it.
    result::Result<Value> created(const authoring::SessionRecord& record) {
        RAWFRAME_TRY_ASSIGN(const std::filesystem::path kPath, pathOf(record.scene));
        const std::filesystem::path kSidecar = kPath.string() + std::string{content::kSidecarSuffix};
        std::error_code error;
        if (std::filesystem::exists(std::filesystem::symlink_status(kPath, error)) ||
            std::filesystem::exists(std::filesystem::symlink_status(kSidecar, error))) {
            return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                           result::ErrorClass::AlreadyExists,
                                           "a new scene's path and its sidecar's are free")
                                       .withContext("scene", record.scene)};
        }
        if (!std::filesystem::is_directory(kPath.parent_path(), error)) {
            return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                           result::ErrorClass::NotFound,
                                           "a new scene goes in a directory that is there")
                                       .withContext("scene", record.scene)};
        }
        std::random_device device;
        const auto kWord = [&device] {
            return (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
        };
        const content::ResourceId kIdentity{.value = base::Bits128{.high = kWord(), .low = kWord()}};
        RAWFRAME_TRY_ASSIGN(const std::string kText, rawframe::scene::writeScene(rawframe::scene::Scene{}));
        // Each file made where nothing is, so nothing put there meanwhile
        // is followed or replaced; the sidecar last, so a scene without one
        // is never named.
        const auto kWritten = [](const std::filesystem::path& path, std::string_view text) {
            std::FILE* file = std::fopen(path.string().c_str(), "wbx");
            if (file == nullptr) {
                return false;
            }
            const bool kWhole = std::fwrite(text.data(), 1, text.size(), file) == text.size();
            return std::fclose(file) == 0 && kWhole;
        };
        if (!kWritten(kPath, kText)) {
            return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                           result::ErrorClass::Unavailable,
                                           "the new scene could not be written")
                                       .withContext("scene", record.scene)};
        }
        if (!kWritten(kSidecar,
                      content::writeSidecar(content::Sidecar{.id = kIdentity, .importer = "rawframe.scene"}))) {
            std::filesystem::remove(kPath, error);
            return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                           result::ErrorClass::Unavailable,
                                           "the new scene's sidecar could not be written")
                                       .withContext("scene", record.scene)};
        }
        beside_.emplace_back(kIdentity.value, kPath);
        std::array<char, base::kBits128HexDigits> digits{};
        base::formatBits128Hex(kIdentity.value, digits);
        Value made = Value::object();
        made.add("kind", Value::string("authoring.created"));
        made.add("scene", Value::string(record.scene));
        made.add("resource", Value::string(std::string{digits.data(), digits.size()}));
        made.add("document", Value::string(digestOf(kText)));
        return made;
    }

    /// The outcome document `apply` writes, from a session's scene.
    static Value outcome(const OpenScene& open, bool written, bool reopened, Value results, std::size_t skipped) {
        Value made = Value::object();
        made.add("kind", Value::string("authoring.outcome"));
        made.add("document", Value::string(digestOf(open.document->text())));
        made.add("written", Value::boolean(written));
        made.add("reopened", Value::boolean(reopened));
        made.add("undoable", Value::integer(static_cast<std::int64_t>(open.document->undoable())));
        made.add("redoable", Value::integer(static_cast<std::int64_t>(open.document->redoable())));
        made.add("selection", selectionOf(open));
        made.add("view", viewOf(open));
        made.add("results", std::move(results));
        made.add("skipped", Value::integer(static_cast<std::int64_t>(skipped)));
        return made;
    }

    result::Result<Value> applied(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        authoring::AuthoredScene& document = *open->document;
        const authoring::Request& request = record.request;
        const std::string kBefore = digestOf(document.text());
        if (request.expects.has_value() && *request.expects != kBefore) {
            return std::unexpected{failure(authoring::AuthoringError::TargetStale,
                                           result::ErrorClass::FailedPrecondition,
                                           "the request was computed against another generation of the scene")
                                       .withContext("document", kBefore)};
        }
        const rawframe::scene::SceneSource kSources = sources();
        Value results = Value::array();
        std::size_t skipped = 0;
        if (request.batch == authoring::Batch::Atomic) {
            const auto kOutcome =
                authoring::executeAtomic(document, document.generation(), request.operations, *catalog_, &kSources);
            failed_ = !kOutcome.has_value();
            results.push(slotValue(kOutcome));
        } else {
            const authoring::IndependentOutcome kOutcomes = authoring::executeIndependent(
                document,
                document.generation(),
                request.operations,
                *catalog_,
                request.batch == authoring::Batch::HaltRemaining ? authoring::OnFailure::HaltRemaining
                                                                 : authoring::OnFailure::ContinuePerItem,
                &kSources);
            for (const auto& each : kOutcomes.outcomes) {
                failed_ = failed_ || !each.has_value();
                results.push(slotValue(each));
            }
            skipped = kOutcomes.skipped;
        }
        RAWFRAME_TRY_ASSIGN(const bool kWritten, save(record.scene, *open));
        return outcome(*open, kWritten, reopened, std::move(results), skipped);
    }

    /// The entities an open scene has selected, by their ids.
    static Value selectionOf(const OpenScene& open) {
        Value made = Value::array();
        for (const rawframe::base::Bits128& each : open.document->selection()) {
            const auto kText = rawframe::schema::formatStableIdText(each);
            made.push(Value::string(std::string{kText.data(), kText.size()}));
        }
        return made;
    }

    /// Where an open scene is looked at from, null until told (D432).
    static Value viewOf(const OpenScene& open) {
        const std::optional<authoring::SceneView>& kView = open.document->view();
        if (!kView.has_value()) {
            return Value{};
        }
        const auto kPoint = [](const std::array<double, 3>& at) {
            Value made = Value::array();
            for (const double kEach : at) {
                made.push(Value::real(kEach));
            }
            return made;
        };
        Value made = Value::object();
        made.add("eye", kPoint(kView->eye));
        made.add("target", kPoint(kView->target));
        made.add("fieldOfView", Value::real(kView->fieldOfView));
        return made;
    }

    /// `view`: where the scene is looked at from, in place of what was.
    result::Result<Value> viewed(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        RAWFRAME_TRY(open->document->setView(record.view));
        Value made = Value::object();
        made.add("kind", Value::string("authoring.view"));
        made.add("document", Value::string(digestOf(open->document->text())));
        made.add("reopened", Value::boolean(reopened));
        made.add("view", viewOf(*open));
        made.add("previewing", Value::boolean(forward(record.scene, *open)));
        return made;
    }

    /// `preview` (D433): a running Runtime's tooling endpoint, granted
    /// `view`, shows the scene from its view from now on, each view the
    /// scene is told or stepped back or forth to handed it by `tooling.look`;
    /// a null preview lets it go. One preview at a time.
    result::Result<Value> previewed(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        letGo();
        if (record.preview.has_value()) {
            // A preview is a client on this machine: its token never leaves
            // it for an endpoint a record names.
            const std::optional<std::string> kEndpoint = loopbackEndpoint(record.preview->endpoint);
            if (!kEndpoint.has_value()) {
                return std::unexpected{failure(authoring::AuthoringError::CapabilityDenied,
                                               result::ErrorClass::PermissionDenied,
                                               "a preview's endpoint is a loopback address literal")
                                           .withContext("endpoint", record.preview->endpoint)};
            }
            std::string said;
            preview_ =
                ToolingLink::open(*kEndpoint, record.preview->pinFile.c_str(), record.preview->tokenFile.c_str(), said);
            if (preview_ == nullptr) {
                return std::unexpected{failure(authoring::AuthoringError::CapabilityDenied,
                                               result::ErrorClass::Unavailable,
                                               "the preview's endpoint could not be reached or did not welcome")
                                           .withContext("said", said)};
            }
            const auto kWelcome = rawframe::document::parse(said);
            const Value* kAnswer = kWelcome.has_value() ? kWelcome->find("answer") : nullptr;
            const Value* kGrants = kAnswer != nullptr ? kAnswer->find("grants") : nullptr;
            const bool kViewGranted = kGrants != nullptr && kGrants->kind() == Value::Kind::Array &&
                                      std::ranges::any_of(kGrants->items(), [](const Value& each) {
                                          return each.kind() == Value::Kind::String && *each.text() == "view";
                                      });
            if (!kViewGranted) {
                preview_.reset();
                return std::unexpected{failure(authoring::AuthoringError::CapabilityDenied,
                                               result::ErrorClass::PermissionDenied,
                                               "the preview's endpoint does not grant view")};
            }
            previewScene_ = record.scene;
            previewed_ = std::nullopt;
            told_ = false;
        }
        Value made = Value::object();
        made.add("kind", Value::string("authoring.preview"));
        made.add("reopened", Value::boolean(reopened));
        made.add("previewing", Value::boolean(forward(record.scene, *open)));
        return made;
    }

    /// Gives a preview's player its camera back, and lets the preview go.
    void letGo() {
        if (preview_ != nullptr) {
            (void)preview_->ask(R"({"kind":"tooling.look","id":0,"view":null})");
        }
        preview_.reset();
        previewScene_.clear();
    }

    /// Hands the preview the scene's view where it previews that scene and
    /// the view changed since it was last told; whether the preview is
    /// live. A preview that does not answer is let go.
    bool forward(const std::string& name, const OpenScene& open) {
        if (preview_ == nullptr || name != previewScene_) {
            return false;
        }
        const std::optional<authoring::SceneView>& kView = open.document->view();
        if (told_ && kView == previewed_) {
            return true;
        }
        Value look = Value::object();
        look.add("kind", Value::string("tooling.look"));
        look.add("id", Value::integer(static_cast<std::int64_t>(++looks_)));
        look.add("view", viewOf(open));
        const auto kReply = preview_->ask(rawframe::document::writeCompact(look));
        const bool kAnswered = kReply.has_value() && [&kReply] {
            const auto kParsed = rawframe::document::parse(*kReply);
            return kParsed.has_value() && kParsed->find("answer") != nullptr;
        }();
        if (!kAnswered) {
            preview_.reset();
            previewScene_.clear();
            return false;
        }
        previewed_ = kView;
        told_ = true;
        return true;
    }

    /// `select`: the scene's selection, in place of what was (D417).
    result::Result<Value> selected(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        RAWFRAME_TRY(open->document->select(record.entities));
        Value made = Value::object();
        made.add("kind", Value::string("authoring.selection"));
        made.add("document", Value::string(digestOf(open->document->text())));
        made.add("reopened", Value::boolean(reopened));
        made.add("selection", selectionOf(*open));
        return made;
    }

    result::Result<Value> answered(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        Value answers = Value::array();
        for (const authoring::Query& query : record.queries) {
            const auto kAnswer = authoring::answer(open->document->scene(), query, *catalog_);
            Value made = Value::object();
            if (kAnswer.has_value()) {
                made.add("answer", authoring::answerValue(*kAnswer));
            } else {
                failed_ = true;
                made.add("error", authoring::errorRecord(kAnswer.error()));
            }
            answers.push(std::move(made));
        }
        Value made = Value::object();
        made.add("kind", Value::string("authoring.answers"));
        made.add("document", Value::string(digestOf(open->document->text())));
        made.add("reopened", Value::boolean(reopened));
        made.add("answers", std::move(answers));
        return made;
    }

    result::Result<Value> stepped(const authoring::SessionRecord& record, bool undo) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        authoring::AuthoredScene& document = *open->document;
        const std::string kBefore = digestOf(document.text());
        if (record.expects.has_value() && *record.expects != kBefore) {
            return std::unexpected{failure(authoring::AuthoringError::TargetStale,
                                           result::ErrorClass::FailedPrecondition,
                                           "the step was asked against another generation of the scene")
                                       .withContext("document", kBefore)};
        }
        const auto kStep = undo ? document.undo(document.generation()) : document.redo(document.generation());
        Value results = Value::array();
        failed_ = !kStep.has_value();
        results.push(slotValue(kStep));
        RAWFRAME_TRY_ASSIGN(const bool kWritten, save(record.scene, *open));
        (void)forward(record.scene, *open);
        return outcome(*open, kWritten, reopened, std::move(results), 0);
    }

    std::filesystem::path game_;
    std::filesystem::path root_;
    std::optional<authoring::ComponentCatalog> catalog_;
    std::vector<std::pair<rawframe::base::Bits128, std::filesystem::path>> beside_;
    std::map<std::string, OpenScene> scenes_;
    bool greeted_ = false;
    /// The preview and the scene it shows (D433), the view it was last
    /// told, and the looks asked of it.
    std::unique_ptr<ToolingLink> preview_;
    std::string previewScene_;
    std::optional<authoring::SceneView> previewed_;
    bool told_ = false;
    std::uint64_t looks_ = 0;
    /// Whether the last record's operations, queries, or step failed in a
    /// slot of its answer, and whether every record so far succeeded.
    bool failed_ = false;
    bool clean_ = true;
};

} // namespace

struct Session::State {
    State(std::filesystem::path game, std::filesystem::path root) : held(std::move(game), std::move(root)) {
    }
    Held held;
};

Session::Session(std::filesystem::path game, std::filesystem::path root)
    : state_(std::make_unique<State>(std::move(game), std::move(root))) {
}

Session::~Session() = default;

std::string Session::answer(std::string_view line, bool& ended) {
    return state_->held.answer(line, ended);
}

bool Session::clean() const noexcept {
    return state_->held.clean();
}

} // namespace rawframe::authoring_session
