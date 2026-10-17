#include "replies.h"

#include <cmath>
#include <utility>

namespace rawframe::world_tooling {

using document::Value;

namespace {

std::string_view codeName(ToolingError error) noexcept {
    switch (error) {
    case ToolingError::Malformed:
        return "malformed";
    case ToolingError::Unauthenticated:
        return "unauthenticated";
    case ToolingError::NotGranted:
        return "not_granted";
    case ToolingError::Unsupported:
        return "unsupported";
    case ToolingError::LimitExceeded:
        return "limit_exceeded";
    case ToolingError::Configuration:
        return "configuration";
    case ToolingError::NotFound:
        return "not_found";
    case ToolingError::Stopped:
        return "stopped";
    }
    return "malformed";
}

} // namespace

std::string replyLine(const Value& id, std::string_view member, Value value) {
    Value made = Value::object();
    made.add("kind", Value::string("tooling.reply"));
    made.add("id", id);
    made.add(std::string{member}, std::move(value));
    return document::writeCompact(made) + "\n";
}

std::string entityName(world::EntityHandle entity) {
    return std::to_string(entity.slot) + ":" + std::to_string(entity.generation);
}

std::string errorLine(const Value& id, ToolingError error, std::string_view why) {
    Value record = Value::object();
    record.add("code", Value::string(std::string{codeName(error)}));
    record.add("message", Value::string(std::string{why}));
    return replyLine(id, "error", std::move(record));
}

std::optional<std::array<double, 3>> pointOf(const Value* point) {
    if (point == nullptr || point->kind() != Value::Kind::Array || point->items().size() != 3) {
        return std::nullopt;
    }
    std::array<double, 3> at{};
    for (std::size_t each = 0; each < 3; ++each) {
        const std::optional<double> kNumber = point->items()[each].real();
        if (!kNumber.has_value() || !std::isfinite(*kNumber) || std::abs(*kNumber) > 1e6) {
            return std::nullopt;
        }
        at[each] = *kNumber;
    }
    return at;
}

std::optional<std::optional<Look>> lookOf(const Value& record, std::string& refusal) {
    const Value* kView = record.find("view");
    if (kView != nullptr && kView->isNull()) {
        return std::optional<Look>{};
    }
    const bool kObject = kView != nullptr && kView->kind() == Value::Kind::Object && kView->names().size() == 3;
    const auto kEye = kObject ? pointOf(kView->find("eye")) : std::nullopt;
    const auto kTarget = kObject ? pointOf(kView->find("target")) : std::nullopt;
    const Value* kAngle = kObject ? kView->find("fieldOfView") : nullptr;
    const std::optional<double> kDegrees = kAngle != nullptr ? kAngle->real() : std::nullopt;
    if (!kEye.has_value() || !kTarget.has_value() || !kDegrees.has_value() || !(*kDegrees >= 1 && *kDegrees <= 179)) {
        refusal = "view is null, or an eye and a target of three numbers within a million metres and a fieldOfView "
                  "of 1 to 179 degrees";
        return std::nullopt;
    }
    return std::optional<Look>{Look{.eye = *kEye, .target = *kTarget, .fieldOfView = *kDegrees}};
}

} // namespace rawframe::world_tooling
