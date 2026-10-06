#include "rawframe/authoring/session.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/schema/stable_id.h"

#include <array>
#include <cstdint>
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

constexpr std::array<VerbName, 11> kVerbs = {
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
    case SessionVerb::End:
        return Members{.required = 0, .optional = 0};
    case SessionVerb::Apply:
    case SessionVerb::Read:
    case SessionVerb::Select:
    case SessionVerb::View:
    case SessionVerb::Preview:
        return Members{.required = 2, .optional = 0};
    case SessionVerb::Undo:
    case SessionVerb::Redo:
        return Members{.required = 1, .optional = 1};
    case SessionVerb::CreateScene:
        return Members{.required = 1, .optional = 0};
    }
    return {};
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
                         "create_scene, or end");
    }
    SessionRecord record{.verb = named->verb, .id = idRead};
    const Members kMembers = membersOf(record.verb);
    const Value* scene = parsed->find("scene");
    const Value* expects = parsed->find("expects");
    const std::size_t kGiven =
        parsed->names().size() - 1 - (parsed->find("id") != nullptr ? 1 : 0) - (expects != nullptr ? 1 : 0);
    if (kGiven != kMembers.required || (expects != nullptr && (kMembers.optional == 0 || textOf(expects) == nullptr))) {
        return malformed("a session record holds its verb's members and no others");
    }
    if (expects != nullptr) {
        record.expects = *expects->text();
    }
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
    case SessionVerb::End:
        return record;
    case SessionVerb::Apply:
    case SessionVerb::Read:
    case SessionVerb::Undo:
    case SessionVerb::Redo:
    case SessionVerb::Select:
    case SessionVerb::View:
    case SessionVerb::Preview:
    case SessionVerb::CreateScene:
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
    } else if (record.verb == SessionVerb::Select) {
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
        const Value* kView = parsed->find("view");
        const auto kPoint = [](const Value* point, std::array<double, 3>& into) {
            if (point == nullptr || point->kind() != Value::Kind::Array || point->items().size() != 3) {
                return false;
            }
            for (std::size_t at = 0; at < 3; ++at) {
                const std::optional<double> kNumber = point->items()[at].real();
                if (!kNumber.has_value()) {
                    return false;
                }
                into[at] = *kNumber;
            }
            return true;
        };
        const Value* kAngle =
            kView != nullptr && kView->kind() == Value::Kind::Object ? kView->find("fieldOfView") : nullptr;
        const std::optional<double> kDegrees = kAngle != nullptr ? kAngle->real() : std::nullopt;
        if (kView == nullptr || kView->kind() != Value::Kind::Object || kView->names().size() != 3 ||
            !kPoint(kView->find("eye"), record.view.eye) || !kPoint(kView->find("target"), record.view.target) ||
            !kDegrees.has_value()) {
            return malformed("view holds an eye and a target of three numbers each and a fieldOfView");
        }
        record.view.fieldOfView = *kDegrees;
    } else if (record.verb == SessionVerb::Preview) {
        const Value* kPreview = parsed->find("preview");
        if (kPreview != nullptr && kPreview->isNull()) {
            return record;
        }
        const bool kObject =
            kPreview != nullptr && kPreview->kind() == Value::Kind::Object && kPreview->names().size() == 3;
        const std::string* kEndpoint = kObject ? textOf(kPreview->find("endpoint")) : nullptr;
        const std::string* kPin = kObject ? textOf(kPreview->find("pinFile")) : nullptr;
        const std::string* kToken = kObject ? textOf(kPreview->find("tokenFile")) : nullptr;
        if (kEndpoint == nullptr || kPin == nullptr || kToken == nullptr || kEndpoint->empty() || kPin->empty() ||
            kToken->empty()) {
            return malformed("preview is null, or holds an endpoint, a pinFile, and a tokenFile");
        }
        record.preview = PreviewTarget{.endpoint = *kEndpoint, .pinFile = *kPin, .tokenFile = *kToken};
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

} // namespace rawframe::authoring
