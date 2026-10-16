#include "traffic.h"

#include "rawframe/world_replication/traffic.h"
#include "replies.h"

#include <cstdint>

namespace rawframe::world_tooling {

std::optional<std::string> answerTraffic(std::string_view kind,
                                         const document::Value& id,
                                         const ToolingSettings& settings,
                                         world::TickIndex tick) {
    using document::Value;
    if (kind != "tooling.traffic") {
        return std::nullopt;
    }
    if (!settings.grants.inspect) {
        return errorLine(id, ToolingError::NotGranted, "traffic needs the inspect grant");
    }
    if (settings.traffic == nullptr) {
        return errorLine(id, ToolingError::NotFound, "no replication server runs here");
    }
    const world_replication::ServerReplicationStatistics kTotals = settings.traffic->totals();
    const auto kCount = [](std::uint64_t value) {
        return Value::integer(static_cast<std::int64_t>(value));
    };
    Value made = Value::object();
    made.add("kind", Value::string("tooling.traffic"));
    made.add("tick", kCount(tick.value));
    made.add("connections", kCount(settings.traffic->connections()));
    made.add("stateDatagrams", kCount(kTotals.stateDatagrams));
    made.add("stateBytes", kCount(kTotals.stateBytes));
    made.add("recordsSent", kCount(kTotals.recordsSent));
    made.add("inputWindows", kCount(kTotals.inputWindows));
    made.add("inputBytes", kCount(kTotals.inputBytes));
    made.add("inputsStale", kCount(kTotals.inputsStale));
    made.add("inputsRefused", kCount(kTotals.inputsRefused));
    made.add("messagesSent", kCount(kTotals.messagesSent));
    return replyLine(id, "answer", std::move(made));
}

} // namespace rawframe::world_tooling
