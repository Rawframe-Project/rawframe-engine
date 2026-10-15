#include "bookmarks.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/base/files.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

constexpr std::string_view kKind = "rawframe.editor_state";
constexpr std::int64_t kFormatVersion = 1;

result::Error refusal(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument,
                        authoring::kAuthoringDomain,
                        authoring::code(authoring::AuthoringError::ValidationFailed),
                        why)
        .error();
}

std::optional<std::array<double, 3>> pointOf(const Value* point) {
    if (point == nullptr || point->kind() != Value::Kind::Array || point->items().size() != 3) {
        return std::nullopt;
    }
    std::array<double, 3> made{};
    for (std::size_t at = 0; at < 3; ++at) {
        const std::optional<double> kNumber = point->items()[at].real();
        if (!kNumber.has_value()) {
            return std::nullopt;
        }
        made[at] = *kNumber;
    }
    return made;
}

Value pointValue(const std::array<double, 3>& at) {
    Value made = Value::array();
    for (const double kEach : at) {
        made.push(Value::real(kEach));
    }
    return made;
}

/// The editor state at `path`, an empty one where there is no file.
result::Result<Value> stateOf(const std::filesystem::path& path) {
    std::error_code missing;
    if (!std::filesystem::exists(path, missing)) {
        Value made = Value::object();
        made.add("kind", Value::string(std::string{kKind}));
        made.add("formatVersion", Value::integer(kFormatVersion));
        return made;
    }
    const std::optional<std::string> kText = readFile(path);
    if (!kText.has_value()) {
        return std::unexpected{refusal("a scene's editor state is a file that reads")};
    }
    auto parsed = document::parse(*kText);
    const Value* kind = parsed.has_value() ? parsed->find("kind") : nullptr;
    const Value* version = parsed.has_value() ? parsed->find("formatVersion") : nullptr;
    if (!parsed.has_value() || parsed->kind() != Value::Kind::Object || kind == nullptr || kind->text() == nullptr ||
        *kind->text() != kKind || version == nullptr || version->integer() != kFormatVersion) {
        return std::unexpected{refusal("a scene's editor state is rawframe.editor_state, format 1")};
    }
    return std::move(*parsed);
}

} // namespace

std::filesystem::path editorStateOf(const std::filesystem::path& scene) {
    return std::filesystem::path{scene.string() + ".editor"};
}

result::Result<std::vector<Bookmark>> readBookmarks(const std::filesystem::path& path) {
    RAWFRAME_TRY_ASSIGN(const Value kState, stateOf(path));
    std::vector<Bookmark> made;
    const Value* listed = kState.find("bookmarks");
    if (listed == nullptr) {
        return made;
    }
    if (listed->kind() != Value::Kind::Array || listed->items().size() > 9) {
        return std::unexpected{refusal("an editor state's bookmarks are at most nine")};
    }
    for (const Value& kEach : listed->items()) {
        const Value* slot = kEach.find("slot");
        const std::optional<std::int64_t> kSlot = slot != nullptr ? slot->integer() : std::nullopt;
        const Value* view = kEach.find("view");
        const Value* angle = view != nullptr ? view->find("fieldOfView") : nullptr;
        const auto kEye = pointOf(view != nullptr ? view->find("eye") : nullptr);
        const auto kTarget = pointOf(view != nullptr ? view->find("target") : nullptr);
        const std::optional<double> kDegrees = angle != nullptr ? angle->real() : std::nullopt;
        if (!kSlot.has_value() || *kSlot < 1 || *kSlot > 9 || !kEye.has_value() || !kTarget.has_value() ||
            !kDegrees.has_value() || std::ranges::contains(made, static_cast<std::uint8_t>(*kSlot), &Bookmark::slot)) {
            return std::unexpected{refusal("a bookmark is a slot from 1 to 9, once, and a view")};
        }
        made.push_back(
            Bookmark{.slot = static_cast<std::uint8_t>(*kSlot),
                     .view = authoring::SceneView{.eye = *kEye, .target = *kTarget, .fieldOfView = *kDegrees}});
    }
    std::ranges::sort(made, {}, &Bookmark::slot);
    return made;
}

result::Status writeBookmarks(const std::filesystem::path& path, const std::vector<Bookmark>& bookmarks) {
    RAWFRAME_TRY_ASSIGN(const Value kState, stateOf(path));
    Value made = Value::object();
    for (std::size_t at = 0; at < kState.names().size(); ++at) {
        if (kState.names()[at] != "bookmarks") {
            made.add(kState.names()[at], kState.items()[at]);
        }
    }
    made.add("bookmarks", bookmarksValue(bookmarks));
    const std::string kText = document::write(made);
    if (!base::replaceFile(path, std::as_bytes(std::span{kText}))) {
        return std::unexpected{result::fail(result::ErrorClass::Unavailable,
                                            authoring::kAuthoringDomain,
                                            authoring::code(authoring::AuthoringError::Internal),
                                            "the scene's editor state could not be written")
                                   .error()};
    }
    return {};
}

Value bookmarksValue(const std::vector<Bookmark>& bookmarks) {
    Value made = Value::array();
    for (const Bookmark& kEach : bookmarks) {
        Value view = Value::object();
        view.add("eye", pointValue(kEach.view.eye));
        view.add("target", pointValue(kEach.view.target));
        view.add("fieldOfView", Value::real(kEach.view.fieldOfView));
        Value entry = Value::object();
        entry.add("slot", Value::integer(kEach.slot));
        entry.add("view", std::move(view));
        made.push(std::move(entry));
    }
    return made;
}

} // namespace rawframe::authoring_session
