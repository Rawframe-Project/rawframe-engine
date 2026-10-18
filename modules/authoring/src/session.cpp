#include "rawframe/authoring/session.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/schema/stable_id.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace rawframe::authoring {

namespace {

using document::Value;

std::unexpected<result::Error> malformed(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kAuthoringDomain, code(AuthoringError::ValidationFailed), why);
}

const std::string* textOf(const Value* value) {
    return value != nullptr && value->kind() == Value::Kind::String ? value->text() : nullptr;
}

struct VerbName {
    std::string_view kind;
    SessionVerb verb;
};

constexpr std::array<VerbName, 40> kVerbs = {
    VerbName{.kind = "authoring.hello", .verb = SessionVerb::Hello},
    VerbName{.kind = "authoring.describe", .verb = SessionVerb::Describe},
    VerbName{.kind = "authoring.apply", .verb = SessionVerb::Apply},
    VerbName{.kind = "authoring.read", .verb = SessionVerb::Read},
    VerbName{.kind = "authoring.undo", .verb = SessionVerb::Undo},
    VerbName{.kind = "authoring.redo", .verb = SessionVerb::Redo},
    VerbName{.kind = "authoring.select", .verb = SessionVerb::Select},
    VerbName{.kind = "authoring.view", .verb = SessionVerb::View},
    VerbName{.kind = "authoring.preview", .verb = SessionVerb::Preview},
    VerbName{.kind = "authoring.create_scene", .verb = SessionVerb::CreateScene},
    VerbName{.kind = "authoring.history", .verb = SessionVerb::History},
    VerbName{.kind = "authoring.assets", .verb = SessionVerb::Assets},
    VerbName{.kind = "authoring.pick", .verb = SessionVerb::Pick},
    VerbName{.kind = "authoring.mark", .verb = SessionVerb::Mark},
    VerbName{.kind = "authoring.preview_input", .verb = SessionVerb::PreviewInput},
    VerbName{.kind = "authoring.hide_layers", .verb = SessionVerb::HideLayers},
    VerbName{.kind = "authoring.frame", .verb = SessionVerb::Frame},
    VerbName{.kind = "authoring.view_mode", .verb = SessionVerb::ViewMode},
    VerbName{.kind = "authoring.bookmark", .verb = SessionVerb::Bookmark},
    VerbName{.kind = "authoring.bookmarks", .verb = SessionVerb::Bookmarks},
    VerbName{.kind = "authoring.split", .verb = SessionVerb::Split},
    VerbName{.kind = "authoring.overlays", .verb = SessionVerb::Overlays},
    VerbName{.kind = "authoring.import_settings", .verb = SessionVerb::ImportSettings},
    VerbName{.kind = "authoring.set_import_settings", .verb = SessionVerb::SetImportSettings},
    VerbName{.kind = "authoring.thumbnail", .verb = SessionVerb::Thumbnail},
    VerbName{.kind = "authoring.asset_references", .verb = SessionVerb::AssetReferences},
    VerbName{.kind = "authoring.run", .verb = SessionVerb::Run},
    VerbName{.kind = "authoring.step", .verb = SessionVerb::Step},
    VerbName{.kind = "authoring.traffic", .verb = SessionVerb::Traffic},
    VerbName{.kind = "authoring.profile", .verb = SessionVerb::Profile},
    VerbName{.kind = "authoring.drawn_frame", .verb = SessionVerb::DrawnFrame},
    VerbName{.kind = "authoring.live_entities", .verb = SessionVerb::LiveEntities},
    VerbName{.kind = "authoring.live_entity", .verb = SessionVerb::LiveEntity},
    VerbName{.kind = "authoring.apply_together", .verb = SessionVerb::ApplyTogether},
    VerbName{.kind = "authoring.cook", .verb = SessionVerb::Cook},
    VerbName{.kind = "authoring.cancel", .verb = SessionVerb::Cancel},
    VerbName{.kind = "authoring.import", .verb = SessionVerb::Import},
    VerbName{.kind = "authoring.extensions", .verb = SessionVerb::Extensions},
    VerbName{.kind = "authoring.extension", .verb = SessionVerb::Extension},
    VerbName{.kind = "authoring.end", .verb = SessionVerb::End}};

/// The members a verb's record may hold beside `kind` and `id`, and those
/// it must.
struct Members {
    std::size_t required = 0;
    std::size_t optional = 0;
};

Members membersOf(SessionVerb verb) {
    switch (verb) {
    case SessionVerb::Hello:
        return Members{.required = 1, .optional = 0};
    case SessionVerb::Describe:
    case SessionVerb::Assets:
    case SessionVerb::Extensions:
    case SessionVerb::End:
        return Members{.required = 0, .optional = 0};
    case SessionVerb::Extension:
        return Members{.required = 4, .optional = 0};
    case SessionVerb::Apply:
    case SessionVerb::Read:
    case SessionVerb::Select:
    case SessionVerb::View:
    case SessionVerb::Preview:
    case SessionVerb::Mark:
    case SessionVerb::PreviewInput:
    case SessionVerb::HideLayers:
    case SessionVerb::ViewMode:
    case SessionVerb::Split:
    case SessionVerb::Overlays:
    case SessionVerb::Cook:
    case SessionVerb::Import:
    case SessionVerb::Step:
        return Members{.required = 2, .optional = 0};
    case SessionVerb::Run:
        return Members{.required = 1, .optional = 0};
    case SessionVerb::Traffic:
    case SessionVerb::Profile:
    case SessionVerb::DrawnFrame:
        return Members{.required = 1, .optional = 0};
    case SessionVerb::LiveEntities:
        return Members{.required = 1, .optional = 0};
    case SessionVerb::LiveEntity:
        return Members{.required = 2, .optional = 0};
    case SessionVerb::Undo:
    case SessionVerb::Redo:
        return Members{.required = 1, .optional = 1};
    case SessionVerb::Bookmark:
        return Members{.required = 3, .optional = 0};
    case SessionVerb::CreateScene:
    case SessionVerb::History:
    case SessionVerb::Pick:
    case SessionVerb::Frame:
    case SessionVerb::Bookmarks:
    case SessionVerb::ApplyTogether:
    case SessionVerb::Cancel:
    case SessionVerb::ImportSettings:
    case SessionVerb::AssetReferences:
        return Members{.required = 1, .optional = 0};
    case SessionVerb::SetImportSettings:
    case SessionVerb::Thumbnail:
        return Members{.required = 2, .optional = 0};
    }
    return {};
}

/// A view's form, as `view` and `bookmark` give it: an eye and a target of
/// three numbers each and a field of view; the scene checks its ranges.
bool readView(const Value* view, SceneView& into) {
    const auto kPoint = [](const Value* point, std::array<double, 3>& at) {
        if (point == nullptr || point->kind() != Value::Kind::Array || point->items().size() != 3) {
            return false;
        }
        for (std::size_t each = 0; each < 3; ++each) {
            const std::optional<double> kNumber = point->items()[each].real();
            if (!kNumber.has_value()) {
                return false;
            }
            at[each] = *kNumber;
        }
        return true;
    };
    if (view == nullptr || view->kind() != Value::Kind::Object || view->names().size() != 3) {
        return false;
    }
    const Value* kAngle = view->find("fieldOfView");
    const std::optional<double> kDegrees = kAngle != nullptr ? kAngle->real() : std::nullopt;
    if (!kPoint(view->find("eye"), into.eye) || !kPoint(view->find("target"), into.target) || !kDegrees.has_value()) {
        return false;
    }
    into.fieldOfView = *kDegrees;
    return true;
}

/// Whether `path` is absolute in POSIX's form (`/a`) or Windows' (`C:/a`,
/// `C:\a`). Never a network share (`\\server\share`, `//server/share`):
/// Windows answers one by authenticating to the server, which would hand
/// the user's credentials to whoever a record names (D503).
bool absolute(std::string_view path) {
    const bool kDrive = path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':' &&
                        (path[2] == '/' || path[2] == '\\');
    const bool kShare = path.size() >= 2 && (path[0] == '/' || path[0] == '\\') && (path[1] == '/' || path[1] == '\\');
    return !kShare && (path.starts_with('/') || kDrive);
}

/// Apply together's parts: two or more, each a scene's path and an atomic
/// request, no scene named twice.
result::Result<SessionRecord> together(const Value& parsed, SessionRecord record) {
    const Value* kDocuments = parsed.find("documents");
    if (kDocuments == nullptr || kDocuments->kind() != Value::Kind::Array || kDocuments->items().size() < 2) {
        return malformed("apply_together holds two or more documents");
    }
    for (std::size_t at = 0; at < kDocuments->items().size(); ++at) {
        const Value& kEach = kDocuments->items()[at];
        const std::string* scene = kEach.kind() == Value::Kind::Object ? textOf(kEach.find("scene")) : nullptr;
        const Value* kRequest = scene != nullptr ? kEach.find("request") : nullptr;
        if (kRequest == nullptr || scene->empty() || kEach.names().size() != 2) {
            return std::unexpected<result::Error>{malformed("each document names its scene and holds its request")
                                                      .error()
                                                      .withContext("part", std::to_string(at))};
        }
        for (const SessionPart& kBefore : record.parts) {
            if (kBefore.scene == *scene) {
                return std::unexpected<result::Error>{
                    malformed("apply_together names each scene once").error().withContext("part", std::to_string(at))};
            }
        }
        auto request = readRequest(document::writeCompact(*kRequest));
        if (!request.has_value()) {
            return std::unexpected<result::Error>{std::move(request).error().withContext("part", std::to_string(at))};
        }
        if (request->batch != Batch::Atomic) {
            return std::unexpected<result::Error>{
                malformed("apply_together's requests are atomic").error().withContext("part", std::to_string(at))};
        }
        record.parts.push_back(SessionPart{.scene = *scene, .request = std::move(*request)});
    }
    return record;
}

} // namespace

result::Result<SessionRecord> readSessionRecord(std::string_view line, document::Value& idRead) {
    idRead = Value{};
    auto parsed = document::parse(line, document::ReadLimits{.maximumBytes = kMaximumSessionRecordBytes});
    if (!parsed.has_value() || parsed->kind() != Value::Kind::Object) {
        return malformed("a session record is one strict JSON object a line");
    }
    if (const Value* kId = parsed->find("id"); kId != nullptr) {
        idRead = *kId;
    }
    const std::string* kind = textOf(parsed->find("kind"));
    const VerbName* named = nullptr;
    for (const VerbName& each : kVerbs) {
        if (kind != nullptr && *kind == each.kind) {
            named = &each;
        }
    }
    if (named == nullptr) {
        return malformed("a session record's kind is hello, describe, apply, read, undo, redo, select, view, preview, "
                         "create_scene, history, assets, pick, mark, apply_together, cook, cancel, import, or end");
    }
    SessionRecord record{.verb = named->verb, .id = idRead};
    const Members kMembers = membersOf(record.verb);
    const Value* scene = parsed->find("scene");
    const Value* expects = parsed->find("expects");
    const Value* dry = parsed->find("dryRun");
    // A verb's own members it may leave out: run's paused (D806), and the
    // live entities' page (D808).
    std::size_t leftOutable = 0;
    if (record.verb == SessionVerb::Run) {
        leftOutable = parsed->find("paused") != nullptr ? 1 : 0;
    } else if (record.verb == SessionVerb::LiveEntities) {
        for (const char* kName : {"after", "component", "limit"}) {
            leftOutable += parsed->find(kName) != nullptr ? 1 : 0;
        }
    } else if (record.verb == SessionVerb::DrawnFrame) {
        for (const char* kName : {"steps", "capture"}) {
            leftOutable += parsed->find(kName) != nullptr ? 1 : 0;
        }
    } else if (record.verb == SessionVerb::Profile) {
        leftOutable = parsed->find("capture") != nullptr ? 1 : 0;
    }
    const std::size_t kGiven = parsed->names().size() - 1 - (parsed->find("id") != nullptr ? 1 : 0) -
                               (expects != nullptr ? 1 : 0) - (dry != nullptr ? 1 : 0) - leftOutable;
    if (kGiven != kMembers.required || (expects != nullptr && (kMembers.optional == 0 || textOf(expects) == nullptr))) {
        return malformed("a session record holds its verb's members and no others");
    }
    if (dry != nullptr && (record.verb != SessionVerb::Apply || !dry->truth().has_value())) {
        return malformed("only apply runs dry, told so by dryRun, true or false");
    }
    if (expects != nullptr) {
        record.expects = *expects->text();
    }
    record.dryRun = dry != nullptr && dry->truth().value_or(false);
    switch (record.verb) {
    case SessionVerb::Hello: {
        const Value* kGeneration = parsed->find("surfaceGeneration");
        const std::optional<std::int64_t> kNumber = kGeneration != nullptr ? kGeneration->integer() : std::nullopt;
        if (!kNumber.has_value() || *kNumber < 0 || *kNumber > UINT32_MAX) {
            return malformed("hello names the surface generation the client speaks");
        }
        record.surfaceGeneration = static_cast<std::uint32_t>(*kNumber);
        return record;
    }
    case SessionVerb::Describe:
    case SessionVerb::Assets:
    case SessionVerb::Extensions:
    case SessionVerb::End:
        return record;
    case SessionVerb::ApplyTogether:
        return together(*parsed, std::move(record));
    case SessionVerb::Cook: {
        // Absolute paths in either system's form; whether they are fit to
        // cook to is the session's to say, on its own system.
        const std::string* output = textOf(parsed->find("output"));
        const Value* kCache = parsed->find("cache");
        if (output == nullptr || !absolute(*output) || kCache == nullptr ||
            (!kCache->isNull() && (textOf(kCache) == nullptr || !absolute(*kCache->text())))) {
            return malformed("cook names an absolute output and an absolute cache, or null for none");
        }
        record.output = *output;
        if (!kCache->isNull()) {
            record.cache = *kCache->text();
        }
        return record;
    }
    case SessionVerb::Thumbnail: {
        const std::string* kAsset = textOf(parsed->find("asset"));
        const Value* kSize = parsed->find("size");
        const std::optional<std::int64_t> kPixels = kSize != nullptr ? kSize->integer() : std::nullopt;
        if (kAsset == nullptr || kAsset->empty() || kAsset->size() > 1024 || !kPixels.has_value() || *kPixels < 16 ||
            *kPixels > 256) {
            return malformed("thumbnail names an asset by its path and a size of 16 to 256 pixels");
        }
        record.asset = *kAsset;
        record.size = static_cast<std::uint32_t>(*kPixels);
        return record;
    }
    case SessionVerb::ImportSettings:
    case SessionVerb::AssetReferences:
    case SessionVerb::SetImportSettings: {
        // A source by its path under the game's directory, which the
        // session finds among those it lists (D800).
        const std::string* kAsset = textOf(parsed->find("asset"));
        if (kAsset == nullptr || kAsset->empty() || kAsset->size() > 1024) {
            return malformed("import settings name an asset by its path under the game's directory");
        }
        record.asset = *kAsset;
        if (record.verb == SessionVerb::SetImportSettings) {
            const Value* kSettings = parsed->find("settings");
            if (kSettings == nullptr || kSettings->kind() != Value::Kind::Object) {
                return malformed("set_import_settings gives the settings to change, an object");
            }
            record.settings = *kSettings;
        }
        return record;
    }
    case SessionVerb::Import: {
        // The source absolute, where it is; where it goes, the session's to
        // check under the game's directory.
        const std::string* source = textOf(parsed->find("source"));
        const std::string* as = textOf(parsed->find("as"));
        if (source == nullptr || !absolute(*source) || as == nullptr || as->empty()) {
            return malformed("import names an absolute source and the path it goes to");
        }
        record.source = *source;
        record.as = *as;
        return record;
    }
    case SessionVerb::Cancel: {
        const Value* kOperation = parsed->find("operation");
        if (kOperation == nullptr || kOperation->isNull()) {
            return malformed("cancel names the operation by the id its client gave it");
        }
        record.operation = *kOperation;
        return record;
    }
    case SessionVerb::Apply:
    case SessionVerb::Read:
    case SessionVerb::Undo:
    case SessionVerb::Redo:
    case SessionVerb::Select:
    case SessionVerb::View:
    case SessionVerb::Preview:
    case SessionVerb::CreateScene:
    case SessionVerb::History:
    case SessionVerb::Pick:
    case SessionVerb::Mark:
    case SessionVerb::PreviewInput:
    case SessionVerb::HideLayers:
    case SessionVerb::Frame:
    case SessionVerb::ViewMode:
    case SessionVerb::Bookmark:
    case SessionVerb::Bookmarks:
    case SessionVerb::Split:
    case SessionVerb::Overlays:
    case SessionVerb::Run:
    case SessionVerb::Step:
    case SessionVerb::Traffic:
    case SessionVerb::Profile:
    case SessionVerb::DrawnFrame:
    case SessionVerb::LiveEntities:
    case SessionVerb::LiveEntity:
    case SessionVerb::Extension:
        break;
    }
    if (textOf(scene) == nullptr || scene->text()->empty()) {
        return malformed("the record names its scene by its path under the game's directory");
    }
    record.scene = *scene->text();
    if (record.verb == SessionVerb::Apply) {
        const Value* kRequest = parsed->find("request");
        if (kRequest == nullptr) {
            return malformed("apply holds a request");
        }
        RAWFRAME_TRY_ASSIGN(record.request, readRequest(document::writeCompact(*kRequest)));
    } else if (record.verb == SessionVerb::Read) {
        const Value* kQueries = parsed->find("queries");
        if (kQueries == nullptr) {
            return malformed("read holds a query document");
        }
        RAWFRAME_TRY_ASSIGN(record.queries, readQueries(document::writeCompact(*kQueries)));
    } else if (record.verb == SessionVerb::Select || record.verb == SessionVerb::Extension) {
        // An extension's command is run on the entities chosen, as select
        // names them (D874), its extension and command by their names; the
        // session finds them among the game's.
        if (record.verb == SessionVerb::Extension) {
            const std::string* kExtension = textOf(parsed->find("extension"));
            const std::string* kCommand = textOf(parsed->find("command"));
            if (kExtension == nullptr || kExtension->empty() || kExtension->size() > 1024 || kCommand == nullptr ||
                kCommand->empty() || kCommand->size() > 256) {
                return malformed("extension names the extension by its path and the command by its name");
            }
            record.extension = *kExtension;
            record.command = *kCommand;
        }
        const Value* kEntities = parsed->find("entities");
        if (kEntities == nullptr || kEntities->kind() != Value::Kind::Array) {
            return malformed("select holds the entities it chooses");
        }
        for (const Value& each : kEntities->items()) {
            const std::string* text = textOf(&each);
            const base::Bits128Parse kId = text != nullptr ? schema::parseStableIdText(*text) : base::Bits128Parse{};
            if (!kId.parsed) {
                return malformed("select names each entity by its SourceEntityId");
            }
            record.entities.push_back(kId.value);
        }
    } else if (record.verb == SessionVerb::View) {
        // The scene's checks its ranges; the record only its form.
        if (!readView(parsed->find("view"), record.view)) {
            return malformed("view holds an eye and a target of three numbers each and a fieldOfView");
        }
    } else if (record.verb == SessionVerb::Profile) {
        // A profile kept as a capture too (D844), its place the session's
        // to judge.
        const Value* kCapture = parsed->find("capture");
        if (kCapture != nullptr && (textOf(kCapture) == nullptr || !kCapture->text()->ends_with(".rfprofile"))) {
            return malformed("a profile's capture is a path ending .rfprofile");
        }
        record.capture = kCapture != nullptr ? *kCapture->text() : std::string{};
    } else if (record.verb == SessionVerb::DrawnFrame) {
        // The capture's place is the session's to judge; the record only
        // its form.
        const Value* kSteps = parsed->find("steps");
        const Value* kCapture = parsed->find("capture");
        if ((kSteps != nullptr && !kSteps->truth().has_value()) ||
            (kCapture != nullptr && (textOf(kCapture) == nullptr || !kCapture->text()->ends_with(".rfframe")))) {
            return malformed("a drawn frame's steps is true or false, and its capture a path ending .rfframe");
        }
        record.steps = kSteps != nullptr && *kSteps->truth();
        record.capture = kCapture != nullptr ? *kCapture->text() : std::string{};
    } else if (record.verb == SessionVerb::Overlays) {
        // Only bounded: the names are the renderer's.
        const Value* kOverlays = parsed->find("overlays");
        bool sound = kOverlays != nullptr && kOverlays->kind() == Value::Kind::Array && kOverlays->items().size() <= 8;
        for (std::size_t at = 0; sound && at < kOverlays->items().size(); ++at) {
            const std::string* kName = textOf(&kOverlays->items()[at]);
            sound = kName != nullptr && !kName->empty() && kName->size() <= 32;
            if (sound) {
                record.overlays.push_back(*kName);
            }
        }
        if (!sound) {
            return malformed("overlays are at most 8 names");
        }
    } else if (record.verb == SessionVerb::Split) {
        const Value* kViews = parsed->find("views");
        const std::optional<std::int64_t> kCount = kViews != nullptr ? kViews->integer() : std::nullopt;
        if (!kCount.has_value() || *kCount < 1 || *kCount > 3) {
            return malformed("split's views are 1, 2, or 3");
        }
        record.views = static_cast<std::uint8_t>(*kCount);
    } else if (record.verb == SessionVerb::Bookmark) {
        // A slot of one to nine, and the view kept there or null to
        // clear it (D796).
        const Value* kSlot = parsed->find("slot");
        const std::optional<std::int64_t> kNumber = kSlot != nullptr ? kSlot->integer() : std::nullopt;
        const Value* kView = parsed->find("view");
        if (!kNumber.has_value() || *kNumber < 1 || *kNumber > 9 || kView == nullptr ||
            (!kView->isNull() && !readView(kView, record.view))) {
            return malformed("bookmark names a slot from 1 to 9 and a view, or null to clear it");
        }
        record.slot = static_cast<std::uint8_t>(*kNumber);
        record.clears = kView->isNull();
    } else if (record.verb == SessionVerb::HideLayers) {
        const Value* kLayers = parsed->find("layers");
        bool sound = kLayers != nullptr && kLayers->kind() == Value::Kind::Array && kLayers->items().size() <= 64;
        for (std::size_t at = 0; sound && at < kLayers->items().size(); ++at) {
            const std::optional<std::int64_t> kLayer = kLayers->items()[at].integer();
            sound = kLayer.has_value() && *kLayer > 0 && *kLayer <= std::numeric_limits<std::uint32_t>::max();
            if (sound) {
                record.layers.push_back(static_cast<std::uint32_t>(*kLayer));
            }
        }
        if (!sound) {
            return malformed("layers are at most 64 layer numbers, each above nought");
        }
    } else if (record.verb == SessionVerb::Run) {
        // Paused true or false, or absent to ask only.
        const Value* kPaused = parsed->find("paused");
        if (kPaused != nullptr && !kPaused->truth().has_value()) {
            return malformed("paused is true or false");
        }
        record.paused = kPaused != nullptr ? kPaused->truth() : std::nullopt;
    } else if (record.verb == SessionVerb::LiveEntities || record.verb == SessionVerb::LiveEntity) {
        // Handed on as given, the server's endpoint reading and bounding
        // them; only their shapes are held here.
        record.query = Value::object();
        for (const char* kName : {"after", "component", "limit", "entity"}) {
            if (const Value* kHanded = parsed->find(kName); kHanded != nullptr) {
                const bool kShaped = std::string_view{kName} == "limit"
                                         ? kHanded->integer().has_value()
                                         : kHanded->kind() == Value::Kind::String && kHanded->text()->size() <= 128;
                if (!kShaped) {
                    return malformed("after, component, and entity are short text, and limit a number");
                }
                record.query.add(kName, *kHanded);
            }
        }
        if (record.verb == SessionVerb::LiveEntity && record.query.find("entity") == nullptr) {
            return malformed("live_entity names its entity as the server lists it");
        }
    } else if (record.verb == SessionVerb::Step) {
        const Value* kTicks = parsed->find("ticks");
        const std::optional<std::int64_t> kCount = kTicks != nullptr ? kTicks->integer() : std::nullopt;
        if (!kCount.has_value() || *kCount < 1 || *kCount > 600) {
            return malformed("ticks is from 1 to 600");
        }
        record.ticks = static_cast<std::uint32_t>(*kCount);
    } else if (record.verb == SessionVerb::ViewMode) {
        // Only bounded: the names are the renderer's.
        const Value* kMode = parsed->find("mode");
        if (textOf(kMode) == nullptr || kMode->text()->empty() || kMode->text()->size() > 32) {
            return malformed("mode names a view mode");
        }
        record.mode = *kMode->text();
    } else if (record.verb == SessionVerb::PreviewInput) {
        // An array, read event by event where it is handed on.
        const Value* kEvents = parsed->find("events");
        if (kEvents == nullptr || kEvents->kind() != Value::Kind::Array) {
            return malformed("events is an array of input events");
        }
        record.events = *kEvents;
    } else if (record.verb == SessionVerb::Mark) {
        // A point of three finite numbers, or null for none.
        const Value* kAt = parsed->find("at");
        if (kAt != nullptr && kAt->isNull()) {
            return record;
        }
        std::array<double, 3> at{};
        bool sound = kAt != nullptr && kAt->kind() == Value::Kind::Array && kAt->items().size() == 3;
        for (std::size_t each = 0; sound && each < 3; ++each) {
            const std::optional<double> kNumber = kAt->items()[each].real();
            sound = kNumber.has_value() && std::isfinite(*kNumber);
            at[each] = sound ? *kNumber : 0;
        }
        if (!sound) {
            return malformed("mark's at is a point of three finite numbers, or null");
        }
        record.mark = at;
    } else if (record.verb == SessionVerb::Preview) {
        const Value* kPreview = parsed->find("preview");
        if (kPreview != nullptr && kPreview->isNull()) {
            return record;
        }
        // The server a pick asks (D456) is optional: its endpoint and pin file
        // together, the token the preview's.
        const Value* kServer =
            kPreview != nullptr && kPreview->kind() == Value::Kind::Object ? kPreview->find("server") : nullptr;
        const bool kObject = kPreview != nullptr && kPreview->kind() == Value::Kind::Object &&
                             kPreview->names().size() == (kServer != nullptr ? 4U : 3U);
        const std::string* kEndpoint = kObject ? textOf(kPreview->find("endpoint")) : nullptr;
        const std::string* kPin = kObject ? textOf(kPreview->find("pinFile")) : nullptr;
        const std::string* kToken = kObject ? textOf(kPreview->find("tokenFile")) : nullptr;
        const bool kServerObject =
            kServer != nullptr && kServer->kind() == Value::Kind::Object && kServer->names().size() == 2;
        const std::string* kServerEndpoint = kServerObject ? textOf(kServer->find("endpoint")) : nullptr;
        const std::string* kServerPin = kServerObject ? textOf(kServer->find("pinFile")) : nullptr;
        if (kEndpoint == nullptr || kPin == nullptr || kToken == nullptr || kEndpoint->empty() || kPin->empty() ||
            kToken->empty() ||
            (kServer != nullptr && (kServerEndpoint == nullptr || kServerPin == nullptr || kServerEndpoint->empty() ||
                                    kServerPin->empty()))) {
            return malformed("preview is null, or holds an endpoint, a pinFile, and a tokenFile, and perhaps a server "
                             "of an endpoint and a pinFile");
        }
        record.preview = PreviewTarget{.endpoint = *kEndpoint, .pinFile = *kPin, .tokenFile = *kToken};
        if (kServer != nullptr) {
            record.preview->serverEndpoint = *kServerEndpoint;
            record.preview->serverPinFile = *kServerPin;
        }
    }
    return record;
}

std::string writeReply(const document::Value& id, document::Value answer) {
    Value made = Value::object();
    made.add("kind", Value::string("authoring.reply"));
    made.add("id", id);
    made.add("answer", std::move(answer));
    return document::writeCompact(made) + "\n";
}

std::string writeRefusal(const document::Value& id, const result::Error& error) {
    Value made = Value::object();
    made.add("kind", Value::string("authoring.reply"));
    made.add("id", id);
    made.add("error", errorRecord(error));
    return document::writeCompact(made) + "\n";
}

std::string writeCancelled(const document::Value& id, std::string_view reason) {
    Value why = Value::object();
    why.add("reason", Value::string(std::string{reason}));
    Value made = Value::object();
    made.add("kind", Value::string("authoring.reply"));
    made.add("id", id);
    made.add("cancelled", std::move(why));
    return document::writeCompact(made) + "\n";
}

} // namespace rawframe::authoring
