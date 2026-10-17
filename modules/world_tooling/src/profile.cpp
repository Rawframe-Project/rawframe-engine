#include "profile.h"

#include "rawframe/world_runtime/simulation.h"
#include "replies.h"

namespace rawframe::world_tooling {

std::optional<std::string>
answerProfile(std::string_view kind, const document::Value& id, const ToolingSettings& settings) {
    using document::Value;
    if (kind != "tooling.profile") {
        return std::nullopt;
    }
    if (!settings.grants.inspect) {
        return errorLine(id, ToolingError::NotGranted, "a profile needs the inspect grant");
    }
    if (settings.simulation == nullptr) {
        return errorLine(id, ToolingError::NotFound, "no World runs here");
    }
    // Microseconds, as the World's own tick summary says them.
    const world_runtime::TickProfile kProfile = settings.simulation->profile();
    const auto kMicroseconds = [](std::int64_t nanoseconds) {
        return Value::integer(nanoseconds / 1000);
    };
    Value ticks = Value::array();
    for (const std::int64_t kTick : kProfile.tickNanoseconds) {
        ticks.push(kMicroseconds(kTick));
    }
    Value systems = Value::array();
    for (const world_runtime::TickProfile::SystemTime& kSystem : kProfile.systems) {
        Value each = Value::object();
        each.add("system", Value::string(kSystem.system));
        each.add("totalUs", kMicroseconds(kSystem.totalNanoseconds));
        each.add("mostUs", kMicroseconds(kSystem.mostNanoseconds));
        systems.push(std::move(each));
    }
    Value made = Value::object();
    made.add("kind", Value::string("tooling.profile"));
    made.add("lastTick", Value::integer(static_cast<std::int64_t>(kProfile.lastTick)));
    // Each tick's share of a second: what a tick past it is late by.
    const world::TickRate kRate = settings.simulation->rate();
    made.add("tickUs",
             Value::integer(kRate.ticks != 0 ? static_cast<std::int64_t>(kRate.seconds) * 1'000'000 /
                                                   static_cast<std::int64_t>(kRate.ticks)
                                             : 0));
    made.add("ticksUs", std::move(ticks));
    made.add("systems", std::move(systems));
    return replyLine(id, "answer", std::move(made));
}

} // namespace rawframe::world_tooling
