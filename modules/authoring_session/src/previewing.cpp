#include "previewing.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world_tooling/window_input.h"
#include "replies.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

/// A point of three numbers, as the tooling endpoint writes one.
std::optional<std::array<double, 3>> pointOf(const Value* point) {
    if (point == nullptr || point->kind() != Value::Kind::Array || point->items().size() != 3) {
        return std::nullopt;
    }
    std::array<double, 3> at{};
    for (std::size_t each = 0; each < 3; ++each) {
        const std::optional<double> kNumber = point->items()[each].real();
        if (!kNumber.has_value() || !std::isfinite(*kNumber)) {
            return std::nullopt;
        }
        at[each] = *kNumber;
    }
    return at;
}

} // namespace

result::Status Preview::open(const authoring::SessionRecord& record) {
    letGo();
    if (!record.preview.has_value()) {
        return {};
    }
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
    preview_ = ToolingLink::open(*kEndpoint, record.preview->pinFile.c_str(), record.preview->tokenFile.c_str(), said);
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
    // The server the game plays on, for picking (D456): a loopback
    // client too, granting inspect.
    if (record.preview->serverEndpoint.has_value()) {
        const std::optional<std::string> kServer = loopbackEndpoint(*record.preview->serverEndpoint);
        std::string welcomed;
        server_ =
            kServer.has_value()
                ? ToolingLink::open(
                      *kServer, record.preview->serverPinFile->c_str(), record.preview->tokenFile.c_str(), welcomed)
                : nullptr;
        if (server_ == nullptr || welcomed.find("\"inspect\"") == std::string::npos) {
            letGo();
            return std::unexpected{failure(authoring::AuthoringError::CapabilityDenied,
                                           result::ErrorClass::Unavailable,
                                           "the previewed game's server could not be reached, or does not "
                                           "grant inspect")
                                       .withContext("said", welcomed)};
        }
    }
    scene_ = record.scene;
    previewed_ = std::nullopt;
    told_ = false;
    clicks_ = 0;
    pressed_.reset();
    return {};
}

void Preview::letGo() {
    if (preview_ != nullptr) {
        (void)preview_->ask(R"({"kind":"tooling.look","id":0,"view":null})");
    }
    preview_.reset();
    server_.reset();
    scene_.clear();
    // A preview to come counts its wheel and drags afresh (D469).
    motionRead_ = {};
}

/// Hands the preview the scene's view where it previews that scene and
/// the view changed since it was last told; whether the preview is
/// live. A preview that does not answer is let go.
bool Preview::forward(const std::string& name, const authoring::AuthoredScene& scene) {
    if (preview_ == nullptr || name != scene_) {
        return false;
    }
    const std::optional<authoring::SceneView>& kView = scene.view();
    if (told_ && kView == previewed_) {
        return true;
    }
    Value look = Value::object();
    look.add("kind", Value::string("tooling.look"));
    look.add("id", Value::integer(static_cast<std::int64_t>(++looks_)));
    look.add("view", viewOf(scene));
    // The views beside it, from the top, the front, and the side of its
    // target, as far as its eye is (D797).
    if (views_ > 1 && kView.has_value()) {
        Value others = Value::array();
        for (const authoring::SceneView& kOther : othersOf(*kView, views_)) {
            others.push(viewValue(kOther));
        }
        look.add("others", std::move(others));
    }
    const auto kReply = preview_->ask(rawframe::document::writeCompact(look));
    std::int64_t split = 0;
    const bool kAnswered = kReply.has_value() && [&kReply, &split] {
        const auto kParsed = rawframe::document::parse(*kReply);
        const Value* answer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        const Value* kSplit = answer != nullptr ? answer->find("split") : nullptr;
        split = kSplit != nullptr ? kSplit->integer().value_or(0) : 0;
        return answer != nullptr;
    }();
    splitShown_ = split;
    if (!kAnswered) {
        preview_.reset();
        scene_.clear();
        return false;
    }
    previewed_ = kView;
    told_ = true;
    return true;
}

result::Result<Value> Preview::picked(const authoring::SessionRecord& record, authoring::AuthoredScene* previewed) {
    if (preview_ == nullptr || server_ == nullptr || record.scene != scene_) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::FailedPrecondition,
                                       "picking needs the scene previewed, with its game's server")
                                   .withContext("scene", record.scene)};
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.picked"));
    const auto kClicked = preview_->ask(R"({"kind":"tooling.clicked","id":0})");
    const auto kParsed = rawframe::document::parse(kClicked.value_or(std::string{}));
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kCount = kAnswer != nullptr ? kAnswer->find("count") : nullptr;
    const std::int64_t kClicks = kCount != nullptr ? kCount->integer().value_or(0) : 0;
    Value scene;
    Value source;
    if (kClicks > clicks_ && kAnswer->find("origin") != nullptr && kAnswer->find("toward") != nullptr) {
        pressed_ = grabbed(kClicks, *kAnswer, previewed);
    }
    if (pressed_.has_value() && pressed_->count == kClicks && pressed_->handle.has_value()) {
        // A handle of the mark, its chosen entity's (D466): the server
        // is not asked what the ray meets.
        scene = Value::string(pressed_->scene);
        source = Value::string(pressed_->source);
    } else if (kClicks > clicks_ && kAnswer->find("origin") != nullptr && kAnswer->find("toward") != nullptr) {
        pressed_.reset();
        Value pick = Value::object();
        pick.add("kind", Value::string("tooling.pick"));
        pick.add("id", Value::integer(0));
        pick.add("origin", *kAnswer->find("origin"));
        pick.add("toward", *kAnswer->find("toward"));
        const auto kPicked = server_->ask(rawframe::document::writeCompact(pick));
        const auto kRead = rawframe::document::parse(kPicked.value_or(std::string{}));
        const Value* kHit = kRead.has_value() ? kRead->find("answer") : nullptr;
        if (kHit != nullptr && kHit->find("scene") != nullptr && kHit->find("source") != nullptr) {
            scene = *kHit->find("scene");
            source = *kHit->find("source");
            const auto kPoint = pointOf(kHit->find("point"));
            const auto kToward = pointOf(kAnswer->find("toward"));
            const Value* kModifiers = kAnswer->find("modifiers");
            if (kPoint.has_value() && kToward.has_value() && scene.text() != nullptr && source.text() != nullptr) {
                pressed_ = Pressed{.count = kClicks,
                                   .scene = *scene.text(),
                                   .source = *source.text(),
                                   .point = *kPoint,
                                   .toward = *kToward,
                                   .modifiers = kModifiers != nullptr ? kModifiers->integer().value_or(0) : 0};
            }
        }
    }
    clicks_ = std::max(clicks_, kClicks);
    made.add("clicks", Value::integer(kClicks));
    made.add("scene", std::move(scene));
    made.add("source", std::move(source));
    // The mark's part the pointer has lit, if any (D468, D504).
    made.add("lit", partName(lit_).empty() ? Value{} : Value::string(std::string{partName(lit_)}));
    // The press that met an authored entity, let go elsewhere: how far
    // its point moved across the level plane through it (D457); with
    // Shift, up or down the upright plane through it facing the eye,
    // and with Control, the turn about the entity, from where it was
    // pressed to where it was let go on the level plane (D463).
    const Value* kReleased = kAnswer != nullptr ? kAnswer->find("released") : nullptr;
    const std::int64_t kRelease = kReleased != nullptr ? kReleased->integer().value_or(0) : 0;
    Value moved;
    if (pressed_.has_value() && kRelease == pressed_->count && pressed_->handle.has_value()) {
        moved = movedAlong(*kAnswer);
        pressed_.reset();
    } else if (pressed_.has_value() && kRelease == pressed_->count) {
        const auto kOrigin = pointOf(kAnswer->find("releaseOrigin"));
        const auto kToward = pointOf(kAnswer->find("releaseToward"));
        constexpr std::int64_t kShift = 1;
        constexpr std::int64_t kControl = 2;
        const bool kHeight = (pressed_->modifiers & kShift) != 0;
        const bool kTurn = !kHeight && (pressed_->modifiers & kControl) != 0;
        const std::array<double, 3>& kAt = pressed_->point;
        // The plane: level, or upright facing the eye along the press.
        std::array<double, 3> normal{0, 1, 0};
        if (kHeight) {
            const double kAcross = std::hypot(pressed_->toward[0], pressed_->toward[2]);
            normal = kAcross > 1e-9
                         ? std::array<double, 3>{pressed_->toward[0] / kAcross, 0, pressed_->toward[2] / kAcross}
                         : std::array<double, 3>{0, 0, 0};
        }
        const auto kDot = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        const double kFacing = kToward.has_value() ? kDot(*kToward, normal) : 0;
        if (kOrigin.has_value() && kToward.has_value() && std::abs(kFacing) > 1e-9) {
            const double kAlong = (kDot(kAt, normal) - kDot(*kOrigin, normal)) / kFacing;
            if (kAlong > 0 && kAlong <= 1) {
                const std::array<double, 3> kMet{(*kOrigin)[0] + kAlong * (*kToward)[0],
                                                 (*kOrigin)[1] + kAlong * (*kToward)[1],
                                                 (*kOrigin)[2] + kAlong * (*kToward)[2]};
                const auto kPoint = [](const std::array<double, 3>& point) {
                    Value numbers = Value::array();
                    for (const double kEach : point) {
                        numbers.push(Value::real(kEach));
                    }
                    return numbers;
                };
                moved = Value::object();
                moved.add("scene", Value::string(pressed_->scene));
                moved.add("source", Value::string(pressed_->source));
                moved.add("how", Value::string(kHeight ? "height" : (kTurn ? "turn" : "move")));
                moved.add("by",
                          kPoint(kHeight ? std::array<double, 3>{0, kMet[1] - kAt[1], 0}
                                         : std::array<double, 3>{kMet[0] - kAt[0], 0, kMet[2] - kAt[2]}));
                if (kTurn) {
                    moved.add("from", kPoint(kAt));
                    moved.add("to", kPoint(kMet));
                }
            }
        }
        pressed_.reset();
    }
    made.add("moved", std::move(moved));
    if (kAnswer != nullptr) {
        lightUnder(*kAnswer);
    }
    made.add("viewed", kAnswer != nullptr ? viewMovedBy(*kAnswer, previewed) : Value{});
    return made;
}

/// The scene's view moved by the preview's wheel and right drags since
/// they were last read (D469), told the preview as any view is: the
/// view, or null where nothing moved.
Value Preview::viewMovedBy(const Value& clicked, authoring::AuthoredScene* previewed) {
    const ViewMotion kNow = motionOf(clicked);
    const ViewMotion kChange = motionSince(kNow, motionRead_);
    motionRead_ = kNow;
    if (kChange == ViewMotion{} || previewed == nullptr) {
        return Value{};
    }
    const authoring::SceneView kWas = previewed->view().value_or(authoring::SceneView{});
    if (!previewed->setView(movedBy(kWas, kChange)).has_value()) {
        return Value{};
    }
    forward(scene_, *previewed);
    return viewOf(*previewed);
}

/// Has the preview draw lit the handle of its mark the pointer is over,
/// as a press there would grab it, while no press is held (D468).
void Preview::lightUnder(const Value& clicked) {
    if (preview_ == nullptr || !mark_.has_value() || pressed_.has_value()) {
        return;
    }
    const auto kOrigin = pointOf(clicked.find("pointOrigin"));
    const auto kToward = pointOf(clicked.find("pointToward"));
    const std::uint8_t kPart =
        kOrigin.has_value() && kToward.has_value() ? partUnder(*mark_, *kOrigin, *kToward, fieldOfView()) : 0;
    if (kPart != lit_ && preview_->ask(rawframe::document::writeCompact(markRecord(mark_, kPart))).has_value()) {
        lit_ = kPart;
    }
}

double Preview::fieldOfView() const noexcept {
    return previewed_.has_value() ? previewed_->fieldOfView : 60;
}

/// The press numbered `count` grabbing a handle of the preview's mark,
/// if it does (D466): the scene's first chosen entity's, the mark being
/// where it stands.
std::optional<Preview::Pressed>
Preview::grabbed(std::int64_t count, const Value& clicked, const authoring::AuthoredScene* previewed) const {
    const auto kOrigin = pointOf(clicked.find("origin"));
    const auto kToward = pointOf(clicked.find("toward"));
    if (!mark_.has_value() || !kOrigin.has_value() || !kToward.has_value() || previewed == nullptr ||
        previewed->selection().empty()) {
        return std::nullopt;
    }
    const std::optional<Handle> kHandle = grabbedHandle(*mark_, *kOrigin, *kToward, fieldOfView());
    if (!kHandle.has_value()) {
        return std::nullopt;
    }
    const auto kSource = rawframe::schema::formatStableIdText(previewed->selection().front());
    return Pressed{.count = count,
                   .scene = scene_,
                   .source = std::string{kSource.data(), kSource.size()},
                   .point = kHandle->point,
                   .toward = *kToward,
                   .handle = kHandle,
                   .mark = *mark_};
}

/// A handle let go (D466, D467): the move, height, or turn it made, or
/// null.
Value Preview::movedAlong(const Value& clicked) const {
    const auto kOrigin = pointOf(clicked.find("releaseOrigin"));
    const auto kToward = pointOf(clicked.find("releaseToward"));
    if (!kOrigin.has_value() || !kToward.has_value()) {
        return Value{};
    }
    return movedRecord(pressed_->scene, pressed_->source, pressed_->mark, *pressed_->handle, *kOrigin, *kToward);
}

/// Has the scene's preview mark a point, or none (D464): where the
/// author chose something. Whether the preview shows it.
Value Preview::marked(const authoring::SessionRecord& record) {
    bool shown = false;
    if (preview_ != nullptr && record.scene == scene_) {
        const auto kReply = preview_->ask(rawframe::document::writeCompact(markRecord(record.mark, 0)));
        const auto kParsed = rawframe::document::parse(kReply.value_or(std::string{}));
        const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        const Value* kMarked = kAnswer != nullptr ? kAnswer->find("marked") : nullptr;
        shown = kMarked != nullptr && kMarked->truth().value_or(false);
    }
    // Kept for its handles (D466) while the scene is previewed, whether
    // or not a preview slow to answer said it shows it: the author's
    // choice stands.
    if (preview_ != nullptr && record.scene == scene_) {
        mark_ = record.mark;
        // Marked afresh, nothing lit (D468).
        lit_ = 0;
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.marked"));
    made.add("marked", Value::boolean(shown));
    return made;
}

result::Result<Value> Preview::inputted(const authoring::SessionRecord& record) {
    // Read whole before anything is handed on: hostile input, refused whole.
    const std::optional<std::vector<world_tooling::WindowInput>> kEvents = world_tooling::windowInputsOf(record.events);
    if (!kEvents.has_value()) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "events are at most 64 input events that read")};
    }
    bool taken = false;
    if (preview_ != nullptr && record.scene == scene_ && !kEvents->empty()) {
        Value asked = Value::object();
        asked.add("kind", Value::string("tooling.input"));
        asked.add("id", Value::integer(0));
        asked.add("events", world_tooling::windowInputsValue(*kEvents));
        const auto kReply = preview_->ask(rawframe::document::writeCompact(asked));
        const auto kParsed = rawframe::document::parse(kReply.value_or(std::string{}));
        const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        const Value* kTaken = kAnswer != nullptr ? kAnswer->find("taken") : nullptr;
        taken = kTaken != nullptr && kTaken->truth().value_or(false);
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.preview_inputted"));
    made.add("taken", Value::boolean(taken));
    return made;
}

Value Preview::hid(const authoring::SessionRecord& record) {
    bool hidden = false;
    if (preview_ != nullptr && record.scene == scene_) {
        Value asked = Value::object();
        asked.add("kind", Value::string("tooling.hide"));
        asked.add("id", Value::integer(0));
        Value layers = Value::array();
        for (const std::uint32_t kLayer : record.layers) {
            layers.push(Value::integer(kLayer));
        }
        asked.add("layers", std::move(layers));
        const auto kReply = preview_->ask(rawframe::document::writeCompact(asked));
        const auto kParsed = rawframe::document::parse(kReply.value_or(std::string{}));
        const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        const Value* kHidden = kAnswer != nullptr ? kAnswer->find("hidden") : nullptr;
        hidden = kHidden != nullptr && kHidden->truth().value_or(false);
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.layers_hidden"));
    made.add("hidden", Value::boolean(hidden));
    return made;
}

result::Result<Value> Preview::ran(const authoring::SessionRecord& record) {
    if (server_ == nullptr || record.scene != scene_) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::FailedPrecondition,
                                       "pausing and stepping need the scene previewed, with its game's server")
                                   .withContext("scene", record.scene)};
    }
    Value asked = Value::object();
    const bool kStep = record.verb == authoring::SessionVerb::Step;
    asked.add("kind", Value::string(kStep ? "tooling.step" : "tooling.run"));
    asked.add("id", Value::integer(0));
    if (kStep) {
        asked.add("ticks", Value::integer(record.ticks));
    } else if (record.paused.has_value()) {
        asked.add("paused", Value::boolean(*record.paused));
    }
    const auto kReply = server_->ask(rawframe::document::writeCompact(asked));
    const auto kParsed = rawframe::document::parse(kReply.value_or(std::string{}));
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    if (kAnswer == nullptr) {
        const Value* kError = kParsed.has_value() ? kParsed->find("error") : nullptr;
        const Value* kMessage = kError != nullptr ? kError->find("message") : nullptr;
        return std::unexpected{failure(authoring::AuthoringError::CapabilityDenied,
                                       result::ErrorClass::FailedPrecondition,
                                       kMessage != nullptr && kMessage->text() != nullptr
                                           ? *kMessage->text()
                                           : "the game's server did not answer")};
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.ran"));
    for (const char* kMember : {"paused", "tick", "stepsLeft"}) {
        const Value* kRead = kAnswer->find(kMember);
        made.add(kMember, kRead != nullptr ? *kRead : Value{});
    }
    return made;
}

result::Result<Value> Preview::trafficOf(const authoring::SessionRecord& record) {
    return serverSays(record, "tooling.traffic", "authoring.traffic", "traffic");
}

result::Result<Value> Preview::profileOf(const authoring::SessionRecord& record) {
    return serverSays(record, "tooling.profile", "authoring.profile", "profile");
}

result::Result<Value> Preview::serverSays(const authoring::SessionRecord& record,
                                          std::string_view asked,
                                          std::string_view answered,
                                          std::string_view what) {
    if (server_ == nullptr || record.scene != scene_) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::FailedPrecondition,
                                       std::string{what} + " needs the scene previewed, with its game's server")
                                   .withContext("scene", record.scene)};
    }
    const auto kReply = server_->ask(R"({"kind":")" + std::string{asked} + R"(","id":0})");
    const auto kParsed = rawframe::document::parse(kReply.value_or(std::string{}));
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    if (kAnswer == nullptr || kAnswer->kind() != Value::Kind::Object) {
        return std::unexpected{failure(authoring::AuthoringError::CapabilityDenied,
                                       result::ErrorClass::Unavailable,
                                       "the game's server did not say its " + std::string{what})};
    }
    // Its members as the server said them, under the session's kind.
    Value made = Value::object();
    made.add("kind", Value::string(std::string{answered}));
    for (std::size_t each = 0; each < kAnswer->names().size(); ++each) {
        if (kAnswer->names()[each] != "kind") {
            made.add(kAnswer->names()[each], kAnswer->items()[each]);
        }
    }
    return made;
}

result::Result<Value> Preview::live(const authoring::SessionRecord& record) {
    if (server_ == nullptr || record.scene != scene_) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::FailedPrecondition,
                                       "the live World needs the scene previewed, with its game's server")
                                   .withContext("scene", record.scene)};
    }
    const bool kOne = record.verb == authoring::SessionVerb::LiveEntity;
    Value asked = Value::object();
    asked.add("kind", Value::string(kOne ? "tooling.read_entity" : "tooling.entities"));
    asked.add("id", Value::integer(0));
    for (std::size_t each = 0; each < record.query.names().size(); ++each) {
        asked.add(record.query.names()[each], record.query.items()[each]);
    }
    const auto kReply = server_->ask(rawframe::document::writeCompact(asked));
    const auto kParsed = rawframe::document::parse(kReply.value_or(std::string{}));
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    if (kAnswer == nullptr || kAnswer->kind() != Value::Kind::Object) {
        const Value* kError = kParsed.has_value() ? kParsed->find("error") : nullptr;
        const Value* kMessage = kError != nullptr ? kError->find("message") : nullptr;
        return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       kMessage != nullptr && kMessage->text() != nullptr
                                           ? *kMessage->text()
                                           : "the game's server did not answer")};
    }
    Value made = Value::object();
    made.add("kind", Value::string(kOne ? "authoring.live_entity" : "authoring.live_entities"));
    for (std::size_t each = 0; each < kAnswer->names().size(); ++each) {
        if (kAnswer->names()[each] != "kind") {
            made.add(kAnswer->names()[each], kAnswer->items()[each]);
        }
    }
    return made;
}

Value Preview::moded(const authoring::SessionRecord& record) {
    bool shown = false;
    if (preview_ != nullptr && record.scene == scene_) {
        Value asked = Value::object();
        asked.add("kind", Value::string("tooling.view_mode"));
        asked.add("id", Value::integer(0));
        asked.add("mode", Value::string(record.mode));
        const auto kReply = preview_->ask(rawframe::document::writeCompact(asked));
        const auto kParsed = rawframe::document::parse(kReply.value_or(std::string{}));
        const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        const Value* kShown = kAnswer != nullptr ? kAnswer->find("shown") : nullptr;
        shown = kShown != nullptr && kShown->truth().value_or(false);
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.view_mode"));
    made.add("mode", Value::string(record.mode));
    made.add("shown", Value::boolean(shown));
    return made;
}

Value Preview::split(const authoring::SessionRecord& record, const authoring::AuthoredScene& scene) {
    views_ = record.views;
    told_ = false;
    splitShown_ = 0;
    const bool kLive = forward(record.scene, scene);
    Value made = Value::object();
    made.add("kind", Value::string("authoring.split"));
    made.add("views", Value::integer(static_cast<std::int64_t>(views_)));
    made.add("shown", Value::boolean(kLive && splitShown_ + 1 == static_cast<std::int64_t>(views_)));
    return made;
}

Value Preview::overlaid(const authoring::SessionRecord& record) {
    bool shown = false;
    Value names = Value::array();
    for (const std::string& kName : record.overlays) {
        names.push(Value::string(kName));
    }
    if (preview_ != nullptr && record.scene == scene_) {
        Value asked = Value::object();
        asked.add("kind", Value::string("tooling.overlays"));
        asked.add("id", Value::integer(0));
        asked.add("overlays", names);
        const auto kReply = preview_->ask(rawframe::document::writeCompact(asked));
        const auto kParsed = rawframe::document::parse(kReply.value_or(std::string{}));
        const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        const Value* kShown = kAnswer != nullptr ? kAnswer->find("shown") : nullptr;
        shown = kShown != nullptr && kShown->truth().value_or(false);
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.overlays"));
    made.add("overlays", std::move(names));
    made.add("shown", Value::boolean(shown));
    return made;
}

} // namespace rawframe::authoring_session
