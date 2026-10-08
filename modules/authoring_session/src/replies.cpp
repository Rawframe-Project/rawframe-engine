#include "replies.h"

#include "rawframe/authoring_session/game.h"
#include "rawframe/base/bits128.h"
#include "rawframe/schema/stable_id.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace rawframe::authoring_session {

using document::Value;

Value outcomeOf(
    const authoring::AuthoredScene& scene, bool written, bool reopened, Value results, std::size_t skipped) {
    Value made = Value::object();
    made.add("kind", Value::string("authoring.outcome"));
    made.add("document", Value::string(digestOf(scene.text())));
    made.add("written", Value::boolean(written));
    made.add("reopened", Value::boolean(reopened));
    made.add("undoable", Value::integer(static_cast<std::int64_t>(scene.undoable())));
    made.add("redoable", Value::integer(static_cast<std::int64_t>(scene.redoable())));
    made.add("selection", selectionOf(scene));
    made.add("view", viewOf(scene));
    made.add("results", std::move(results));
    made.add("skipped", Value::integer(static_cast<std::int64_t>(skipped)));
    return made;
}

Value selectionOf(const authoring::AuthoredScene& scene) {
    Value made = Value::array();
    for (const base::Bits128& each : scene.selection()) {
        const auto kText = schema::formatStableIdText(each);
        made.push(Value::string(std::string{kText.data(), kText.size()}));
    }
    return made;
}

Value viewOf(const authoring::AuthoredScene& scene) {
    const std::optional<authoring::SceneView>& kView = scene.view();
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

} // namespace rawframe::authoring_session
