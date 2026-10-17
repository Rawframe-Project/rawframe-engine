#include "rawframe/authoring_session/session.h"

#include "asset_references.h"
#include "bookmarks.h"
#include "cooking.h"
#include "data_kinds.h"
#include "documents.h"
#include "handles.h"
#include "importing.h"
#include "previewing.h"
#include "rawframe/authoring/authored_data.h"
#include "rawframe/authoring/authored_lines.h"
#include "rawframe/authoring/authored_material.h"
#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/authored_styles.h"
#include "rawframe/authoring/authored_ui.h"
#include "rawframe/authoring/delta.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/authoring/queries.h"
#include "rawframe/authoring/request.h"
#include "rawframe/authoring/session.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/link.h"
#include "rawframe/base/bits128.h"
#include "rawframe/base/files.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/cook/importers.h"
#include "rawframe/document/json.h"
#include "rawframe/scene/scene.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world_kest/game_files.h"
#include "references.h"
#include "replies.h"
#include "settings.h"
#include "thumbnails.h"
#include "viewing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
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
    Held(std::filesystem::path game, std::filesystem::path root, std::filesystem::path cook)
        : game_(std::move(game)), root_(std::filesystem::weakly_canonical(root)), cook_(cook),
          cooking_(std::move(cook)), sheets_(root_, ".styles", "style sheet"), uis_(root_, ".ui", "UI document"),
          materials_(root_, ".rfmaterial", "material instance"),
          games_(root_, ".game", "game description", &readsGameDescription),
          blockouts_(root_, ".blockout", "blockout", &readsBlockout) {
        for (const DataKind& kKind : dataKinds()) {
            data_.emplace_back(root_, kKind.extension, kKind.called, kKind.reads);
        }
    }
    Held(const Held&) = delete;
    Held& operator=(const Held&) = delete;
    ~Held() = default;

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
        // An operation stopped by the end answers before the end does.
        std::string last;
        if (ended) {
            for (const std::string& kSaid : cooking_.end()) {
                last += kSaid;
            }
        }
        auto answered = handle(*record);
        clean_ = clean_ && answered.has_value() && !failed_;
        // A cook started answers when it ends (D502).
        if (answered.has_value() && answered->isNull()) {
            return last;
        }
        return last + (answered.has_value() ? authoring::writeReply(id, std::move(*answered))
                                            : authoring::writeRefusal(id, answered.error()));
    }

    bool clean() const noexcept {
        return clean_;
    }

    std::vector<std::string> poll() {
        bool failed = false;
        std::vector<std::string> said = cooking_.poll(failed);
        clean_ = clean_ && !failed;
        return said;
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
        case authoring::SessionVerb::ApplyTogether:
            return appliedTogether(record);
        case authoring::SessionVerb::Cook: {
            // The game's directory, as the cook tool reads its sources.
            RAWFRAME_TRY(cooking_.start(record.id,
                                        game_.parent_path(),
                                        record.output,
                                        record.cache.has_value() ? std::optional<std::filesystem::path>{*record.cache}
                                                                 : std::nullopt));
            return Value{};
        }
        case authoring::SessionVerb::Import: {
            // The game read again, so what it declares now names the new
            // asset (D503).
            RAWFRAME_TRY_ASSIGN(const Imported kMade, importAsset(game_, record.source, record.as, cook_));
            catalog_.reset();
            RAWFRAME_TRY(load());
            return importedOf(kMade);
        }
        case authoring::SessionVerb::ImportSettings:
            return importSettingsOf(game_, record.asset, importers_);
        case authoring::SessionVerb::SetImportSettings:
            return setImportSettings(game_, record.asset, record.settings, importers_);
        case authoring::SessionVerb::Thumbnail:
            return thumbnailOf(game_, record.asset, cooking_.lastCooked(), record.size);
        case authoring::SessionVerb::AssetReferences:
            return assetReferencesOf(game_, record.asset, cooking_.lastCooked());
        case authoring::SessionVerb::Cancel: {
            Value made = Value::object();
            made.add("kind", Value::string("authoring.cancelling"));
            made.add("found", Value::boolean(cooking_.cancel(record.operation)));
            return made;
        }
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
        case authoring::SessionVerb::Pick:
            return preview_.picked(record, previewedScene());
        case authoring::SessionVerb::Mark:
            return preview_.marked(record);
        case authoring::SessionVerb::PreviewInput:
            return preview_.inputted(record);
        case authoring::SessionVerb::HideLayers:
            return preview_.hid(record);
        case authoring::SessionVerb::Frame:
            return framed(record);
        case authoring::SessionVerb::ViewMode:
            return preview_.moded(record);
        case authoring::SessionVerb::Run:
        case authoring::SessionVerb::Step:
            return preview_.ran(record);
        case authoring::SessionVerb::Traffic:
            return preview_.trafficOf(record);
        case authoring::SessionVerb::Profile: {
            RAWFRAME_TRY_ASSIGN(Value made, preview_.profileOf(record));
            return captured(std::move(made), record.capture, ".rfprofile");
        }
        case authoring::SessionVerb::DrawnFrame: {
            RAWFRAME_TRY_ASSIGN(Value made, preview_.drawnOf(record));
            return captured(std::move(made), record.capture, ".rfframe");
        }
        case authoring::SessionVerb::LiveEntities:
        case authoring::SessionVerb::LiveEntity:
            return preview_.live(record);
        case authoring::SessionVerb::Overlays:
            return preview_.overlaid(record);
        case authoring::SessionVerb::Bookmark:
            return bookmarked(record);
        case authoring::SessionVerb::Bookmarks:
            return bookmarksOf(record);
        case authoring::SessionVerb::Split: {
            bool reopened = false;
            RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
            return preview_.split(record, *open->document);
        }
        case authoring::SessionVerb::Assets: {
            Value made = Value::object();
            made.add("kind", Value::string("authoring.assets"));
            made.add("assets", assets_);
            made.add("sources", sourcesValue());
            return made;
        }
        }
        return std::unexpected{failure(
            authoring::AuthoringError::Internal, result::ErrorClass::Internal, "a session record went unhandled")};
    }

    /// Every source the game's directory holds as it stands (D799), each
    /// with its folder, importer, identity, and how this session's cooks
    /// left it: `cooked`, `failed` with the cook's words, or `unknown`.
    Value sourcesValue() const {
        Value made = Value::array();
        for (const SourceListed& kSource : sourcesBeside(game_)) {
            Value each = Value::object();
            const std::size_t kSlash = kSource.path.rfind('/');
            each.add("path", Value::string(kSource.path));
            each.add("folder", Value::string(kSlash == std::string::npos ? "" : kSource.path.substr(0, kSlash)));
            each.add("importer", Value::string(kSource.importer));
            each.add("id", Value::string(kSource.id));
            const auto kCooked = cooking_.sources().find(kSource.path);
            if (kCooked == cooking_.sources().end()) {
                each.add("cook", Value::string("unknown"));
            } else if (kCooked->second.state == SourceCooked::State::Failed) {
                each.add("cook", Value::string("failed"));
                each.add("problem", Value::string(kCooked->second.problem));
            } else {
                each.add("cook",
                         Value::string(kCooked->second.state == SourceCooked::State::Cooked ? "cooked" : "cooking"));
            }
            made.push(std::move(each));
        }
        return made;
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
        for (const authoring::ComponentSchema& kComponent : catalog_->components()) {
            if (kComponent.uiNode) {
                gameDocuments_.nodes.insert(kComponent.name);
            }
        }
        beside_ = scenesBeside(game_);
        assets_ = assetsOf(files->description());
        // The documents whose references the session keeps (D642).
        for (const auto& [kNamed, kAt] : {std::pair{&files->description().ui, &gameDocuments_.ui},
                                          std::pair{&files->description().styles, &gameDocuments_.styles}}) {
            if (!kNamed->empty()) {
                *kAt = std::filesystem::weakly_canonical(game_.parent_path() / *kNamed);
            }
        }
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

    /// An answer kept in the capture file `capture` names, a file of
    /// `extension` under the root, when it names one: the answer itself,
    /// so a capture opens as it was read (a frame's D842, a profile's
    /// D844).
    result::Result<Value> captured(Value made, const std::string& capture, std::string_view extension) {
        if (capture.empty()) {
            return made;
        }
        const std::filesystem::path kPath = std::filesystem::weakly_canonical(root_ / capture);
        const auto [kRootEnd, kPathAt] = std::ranges::mismatch(root_, kPath);
        if (kRootEnd != root_.end() || kPath.extension() != extension) {
            return std::unexpected{
                failure(authoring::AuthoringError::ValidationFailed,
                        result::ErrorClass::InvalidArgument,
                        "a capture is a " + std::string{extension} + " file under the session's root")
                    .withContext("capture", capture)};
        }
        std::error_code madeDirectory;
        std::filesystem::create_directories(kPath.parent_path(), madeDirectory);
        const std::string kText = rawframe::document::write(made);
        if (madeDirectory || !base::replaceFile(kPath, std::as_bytes(std::span{kText.data(), kText.size()}))) {
            return std::unexpected{failure(authoring::AuthoringError::Internal,
                                           result::ErrorClass::Unavailable,
                                           "the capture could not be written")
                                       .withContext("capture", capture)};
        }
        made.add("capture", Value::string(capture));
        return made;
    }

    /// Writes an open scene whose document changed, by rename.
    result::Result<bool> save(const std::string& name, OpenScene& open) {
        const std::string kText = open.document->text();
        if (digestOf(kText) == open.onDisk) {
            return false;
        }
        const std::filesystem::path kPath = std::filesystem::weakly_canonical(root_ / name);
        if (!base::replaceFile(kPath, std::as_bytes(std::span{kText.data(), kText.size()}))) {
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
        if (auto* lines = linesOf(record.scene)) {
            return lines->history(record);
        }
        if (auto* data = dataOf(record.scene)) {
            return data->history(record);
        }
        if (sheets_.names(record.scene)) {
            return sheets_.history(record);
        }
        if (uis_.names(record.scene)) {
            return uis_.history(record);
        }
        if (materials_.names(record.scene)) {
            return materials_.history(record);
        }
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

    result::Result<Value> applied(const authoring::SessionRecord& record) {
        if (auto* lines = linesOf(record.scene)) {
            return lines->applied(record, failed_);
        }
        if (auto* data = dataOf(record.scene)) {
            return data->applied(record, failed_);
        }
        if (materials_.names(record.scene)) {
            return materials_.applied(record, failed_);
        }
        if (sheets_.names(record.scene) || uis_.names(record.scene)) {
            // A node names a class the sheet has (D642).
            RAWFRAME_TRY(referencesKept(
                gameDocuments_, std::filesystem::weakly_canonical(root_ / record.scene), record.request));
            return sheets_.names(record.scene) ? sheets_.applied(record, failed_) : uis_.applied(record, failed_);
        }
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
        // A dry run runs on a copy, so it answers as the request would be
        // kept, by the same code (D650).
        std::unique_ptr<authoring::AuthoredScene> copy;
        if (record.dryRun) {
            RAWFRAME_TRY_ASSIGN(copy, authoring::AuthoredScene::open(document.identity(), document.text()));
        }
        authoring::AuthoredScene& target = copy != nullptr ? *copy : document;
        const rawframe::scene::SceneSource kSources = sources();
        Value results = Value::array();
        std::size_t skipped = 0;
        if (request.batch == authoring::Batch::Atomic) {
            const auto kOutcome =
                authoring::executeAtomic(target, target.generation(), request.operations, *catalog_, &kSources);
            failed_ = !kOutcome.has_value();
            results.push(slotValue(kOutcome));
        } else {
            const authoring::IndependentOutcome kOutcomes = authoring::executeIndependent(
                target,
                target.generation(),
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
        if (copy != nullptr) {
            return ranDry(outcomeOf(document, false, reopened, std::move(results), skipped), copy->text());
        }
        RAWFRAME_TRY_ASSIGN(const bool kWritten, save(record.scene, *open));
        return outcomeOf(*open->document, kWritten, reopened, std::move(results), skipped);
    }

    /// SPEC-0040's multi-document transaction (D497): every part's scene
    /// open and as its request expects, its operations staged in its own
    /// transaction, all kept or none; then each scene changed is written,
    /// and each answers its outcome, in the parts' order.
    result::Result<Value> appliedTogether(const authoring::SessionRecord& record) {
        std::vector<OpenScene*> opened;
        std::vector<bool> reopened;
        std::vector<authoring::DocumentPart> parts;
        for (const authoring::SessionPart& kPart : record.parts) {
            bool again = false;
            RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(kPart.scene, again));
            const std::string kBefore = digestOf(open->document->text());
            if (kPart.request.expects.has_value() && *kPart.request.expects != kBefore) {
                return std::unexpected{failure(authoring::AuthoringError::TargetStale,
                                               result::ErrorClass::FailedPrecondition,
                                               "the request was computed against another generation of the scene")
                                           .withContext("scene", kPart.scene)
                                           .withContext("document", kBefore)};
            }
            opened.push_back(open);
            reopened.push_back(again);
            parts.push_back(authoring::DocumentPart{.scene = open->document.get(),
                                                    .generation = open->document->generation(),
                                                    .operations = kPart.request.operations});
        }
        const rawframe::scene::SceneSource kSources = sources();
        const auto kCommitted = authoring::executeTogether(parts, *catalog_, &kSources);
        failed_ = failed_ || !kCommitted.has_value();
        Value documents = Value::array();
        for (std::size_t at = 0; at < opened.size(); ++at) {
            RAWFRAME_TRY_ASSIGN(const bool kWritten, save(record.parts[at].scene, *opened[at]));
            Value results = Value::array();
            results.push(kCommitted.has_value()
                             ? slotValue((*kCommitted)[at])
                             : slotValue(std::unexpected<result::Error>{kCommitted.error().clone()}));
            Value each = outcomeOf(*opened[at]->document, kWritten, reopened[at], std::move(results), 0);
            each.add("scene", Value::string(record.parts[at].scene));
            documents.push(std::move(each));
        }
        Value made = Value::object();
        made.add("kind", Value::string("authoring.outcomes"));
        made.add("documents", std::move(documents));
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
        made.add("view", viewOf(*open->document));
        made.add("previewing", Value::boolean(forward(record.scene, *open)));
        return made;
    }

    /// `frame` (D792): the view turned on what is chosen, as `view` sets
    /// it; refused while nothing chosen stands anywhere.
    result::Result<Value> framed(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        std::vector<std::array<double, 3>> points;
        for (const base::Bits128 kChosen : open->document->selection()) {
            if (const auto kAt = standingOf(open->document->scene(), kChosen); kAt.has_value()) {
                points.push_back(*kAt);
            }
        }
        if (points.empty()) {
            return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                           result::ErrorClass::InvalidArgument,
                                           "nothing chosen in the scene has a pose to frame")};
        }
        RAWFRAME_TRY(
            open->document->setView(framedView(open->document->view().value_or(authoring::SceneView{}), points)));
        Value made = Value::object();
        made.add("kind", Value::string("authoring.view"));
        made.add("document", Value::string(digestOf(open->document->text())));
        made.add("reopened", Value::boolean(reopened));
        made.add("view", viewOf(*open->document));
        made.add("previewing", Value::boolean(forward(record.scene, *open)));
        made.add("framed", Value::integer(static_cast<std::int64_t>(points.size())));
        return made;
    }

    /// `bookmark` (D796): a view kept in a slot of the scene's editor
    /// state, or the slot cleared; the scene's bookmarks after.
    result::Result<Value> bookmarked(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY(sceneOf(record.scene, reopened));
        RAWFRAME_TRY_ASSIGN(const std::filesystem::path kPath, pathOf(record.scene));
        if (!record.clears) {
            RAWFRAME_TRY(authoring::checkView(record.view));
        }
        const std::filesystem::path kState = editorStateOf(kPath);
        RAWFRAME_TRY_ASSIGN(std::vector<Bookmark> marks, readBookmarks(kState));
        std::erase_if(marks, [&record](const Bookmark& each) {
            return each.slot == record.slot;
        });
        if (!record.clears) {
            marks.push_back(Bookmark{.slot = record.slot, .view = record.view});
            std::ranges::sort(marks, {}, &Bookmark::slot);
        }
        RAWFRAME_TRY(writeBookmarks(kState, marks));
        return bookmarksReply(marks);
    }

    /// `bookmarks` (D796): the scene's, in slot order.
    result::Result<Value> bookmarksOf(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY(sceneOf(record.scene, reopened));
        RAWFRAME_TRY_ASSIGN(const std::filesystem::path kPath, pathOf(record.scene));
        RAWFRAME_TRY_ASSIGN(const std::vector<Bookmark> kMarks, readBookmarks(editorStateOf(kPath)));
        return bookmarksReply(kMarks);
    }

    static Value bookmarksReply(const std::vector<Bookmark>& marks) {
        Value made = Value::object();
        made.add("kind", Value::string("authoring.bookmarks"));
        made.add("bookmarks", bookmarksValue(marks));
        return made;
    }

    /// `preview` (D433): a running Runtime's tooling endpoint, granted
    /// `view`, shows the scene from its view from now on, each view the
    /// scene is told or stepped back or forth to handed it by `tooling.look`;
    /// a null preview lets it go. One preview at a time.
    result::Result<Value> previewed(const authoring::SessionRecord& record) {
        bool reopened = false;
        RAWFRAME_TRY_ASSIGN(OpenScene * open, sceneOf(record.scene, reopened));
        RAWFRAME_TRY(preview_.open(record));
        Value made = Value::object();
        made.add("kind", Value::string("authoring.preview"));
        made.add("reopened", Value::boolean(reopened));
        made.add("previewing", Value::boolean(forward(record.scene, *open)));
        return made;
    }

    /// The previewed scene, open, if it is.
    authoring::AuthoredScene* previewedScene() {
        const auto kPath = pathOf(preview_.scene());
        const auto kOpen = kPath.has_value() ? scenes_.find(kPath->string()) : scenes_.end();
        return kOpen != scenes_.end() ? kOpen->second.document.get() : nullptr;
    }

    /// Hands the preview the scene's view, where it previews it.
    bool forward(const std::string& name, const OpenScene& open) {
        return preview_.forward(name, *open.document);
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
        made.add("selection", selectionOf(*open->document));
        return made;
    }

    result::Result<Value> answered(const authoring::SessionRecord& record) {
        if (auto* lines = linesOf(record.scene)) {
            return lines->answered(record, failed_);
        }
        if (auto* data = dataOf(record.scene)) {
            return data->answered(record, failed_);
        }
        if (sheets_.names(record.scene)) {
            return sheets_.answered(record, failed_);
        }
        if (uis_.names(record.scene)) {
            return uis_.answered(record, failed_);
        }
        if (materials_.names(record.scene)) {
            return materials_.answered(record, failed_);
        }
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
        if (auto* lines = linesOf(record.scene)) {
            return lines->stepped(record, undo, failed_);
        }
        if (auto* data = dataOf(record.scene)) {
            return data->stepped(record, undo, failed_);
        }
        if (materials_.names(record.scene)) {
            return materials_.stepped(record, undo, failed_);
        }
        if (sheets_.names(record.scene) || uis_.names(record.scene)) {
            // A step that would leave a node naming a class the sheet no
            // longer has is stepped back and refused (D642).
            const auto kStep = [this](const authoring::SessionRecord& asked, bool back) {
                return sheets_.names(asked.scene) ? sheets_.stepped(asked, back, failed_)
                                                  : uis_.stepped(asked, back, failed_);
            };
            const std::set<std::uint64_t> kBefore = unresolvedStyles(gameDocuments_);
            auto stepped = kStep(record, undo);
            if (!stepped.has_value() || failed_) {
                return stepped;
            }
            for (const std::uint64_t kStyle : unresolvedStyles(gameDocuments_)) {
                if (!kBefore.contains(kStyle)) {
                    authoring::SessionRecord back = record;
                    back.expects.reset();
                    static_cast<void>(kStep(back, !undo));
                    failed_ = false;
                    return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                                   result::ErrorClass::Conflict,
                                                   "the step would leave a node naming a class the sheet does not "
                                                   "have")};
                }
            }
            return stepped;
        }
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
        return outcomeOf(*open->document, kWritten, reopened, std::move(results), 0);
    }

    std::filesystem::path game_;
    std::filesystem::path root_;
    std::optional<authoring::ComponentCatalog> catalog_;
    std::vector<std::pair<rawframe::base::Bits128, std::filesystem::path>> beside_;
    Value assets_ = Value::array();
    std::map<std::string, OpenScene> scenes_;
    bool greeted_ = false;
    /// The preview, and what it shows and is pressed in (D433, D456).
    Preview preview_;
    /// Whether the last record's operations, queries, or step failed in a
    /// slot of its answer, and whether every record so far succeeded.
    bool failed_ = false;
    bool clean_ = true;
    /// The cook tool, and the session's long-running operation, if one
    /// runs (D502).
    std::filesystem::path cook_;
    Cooking cooking_;
    /// The engine's importers, which describe and check a source's settings
    /// (D800); materials need no toolchain for that.
    std::vector<cook::Importer> importers_ = cook::engineImporters();
    /// The style sheets, UI documents, and material instances records name
    /// (D636, D641, D721).
    Documents<authoring::AuthoredStyles> sheets_;
    Documents<authoring::AuthoredUi> uis_;
    Documents<authoring::AuthoredMaterial> materials_;
    /// Each data kind's documents (D811), in `dataKinds()`'s order.
    std::vector<Documents<authoring::AuthoredData>> data_;
    /// The game's descriptions, its settings, as line documents (D817).
    Documents<authoring::AuthoredLines> games_;
    /// Its blockouts, line documents too (D849).
    Documents<authoring::AuthoredLines> blockouts_;

    /// The line documents `name` is one of, if any.
    Documents<authoring::AuthoredLines>* linesOf(const std::string& name) {
        return games_.names(name) ? &games_ : blockouts_.names(name) ? &blockouts_ : nullptr;
    }

    /// The data documents `name` is one of, if any.
    Documents<authoring::AuthoredData>* dataOf(const std::string& name) {
        const auto kOf = std::ranges::find_if(data_, [&name](const Documents<authoring::AuthoredData>& each) {
            return each.names(name);
        });
        return kOf != data_.end() ? &*kOf : nullptr;
    }
    GameDocuments gameDocuments_;
};

} // namespace

struct Session::State {
    State(std::filesystem::path game, std::filesystem::path root, std::filesystem::path cook)
        : held(std::move(game), std::move(root), std::move(cook)) {
    }
    Held held;
};

Session::Session(std::filesystem::path game, std::filesystem::path root, std::filesystem::path cook)
    : state_(std::make_unique<State>(std::move(game), std::move(root), std::move(cook))) {
}

Session::~Session() = default;

std::string Session::answer(std::string_view line, bool& ended) {
    return state_->held.answer(line, ended);
}

bool Session::clean() const noexcept {
    return state_->held.clean();
}

std::vector<std::string> Session::poll() {
    return state_->held.poll();
}

} // namespace rawframe::authoring_session
