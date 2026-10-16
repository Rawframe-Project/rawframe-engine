#include "running.h"

#include "replies.h"

#include <cstdint>

namespace rawframe::world_tooling {

namespace {

using document::Value;

/// Most ticks one step asks for: ten seconds at 60 a second.
constexpr std::int64_t kMostStepped = 600;

Value ran(const world_runtime::Simulation& simulation) {
    Value made = Value::object();
    made.add("kind", Value::string("tooling.ran"));
    made.add("paused", Value::boolean(simulation.paused()));
    made.add("tick", Value::integer(static_cast<std::int64_t>(simulation.tick().value)));
    made.add("stepsLeft", Value::integer(static_cast<std::int64_t>(simulation.stepsLeft())));
    return made;
}

} // namespace

std::optional<std::string>
answerRun(std::string_view kind, const Value& record, const Value& id, const ToolingSettings& settings) {
    if (kind != "tooling.run" && kind != "tooling.step") {
        return std::nullopt;
    }
    if (!settings.grants.run || settings.simulation == nullptr) {
        return errorLine(id, ToolingError::NotGranted, "pausing and stepping need the run grant and a World");
    }
    world_runtime::Simulation& simulation = *settings.simulation;
    if (kind == "tooling.run") {
        // `paused` true or false, or absent to ask only.
        const Value* kPaused = record.find("paused");
        const std::optional<bool> kTruth = kPaused != nullptr ? kPaused->truth() : std::nullopt;
        if (kPaused != nullptr && !kTruth.has_value()) {
            return errorLine(id, ToolingError::Malformed, "paused is true or false");
        }
        if (kTruth.has_value()) {
            simulation.pause(*kTruth);
        }
        return replyLine(id, "answer", ran(simulation));
    }
    const Value* kTicks = record.find("ticks");
    const std::optional<std::int64_t> kCount = kTicks != nullptr ? kTicks->integer() : std::nullopt;
    if (!kCount.has_value() || *kCount < 1 || *kCount > kMostStepped) {
        return errorLine(id, ToolingError::Malformed, "ticks is from 1 to 600");
    }
    if (!simulation.paused()) {
        return errorLine(id, ToolingError::Stopped, "a World is stepped only while paused");
    }
    simulation.step(static_cast<std::uint32_t>(*kCount));
    return replyLine(id, "answer", ran(simulation));
}

} // namespace rawframe::world_tooling
