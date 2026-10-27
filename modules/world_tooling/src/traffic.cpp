#include "traffic.h"

#include "rawframe/world_replication/traffic.h"
#include "replies.h"

#include <cstdint>

namespace rawframe::world_tooling {

namespace {

constexpr std::size_t kMostConnectionsAnswered = 256;

} // namespace

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
    made.add("recordsHeld", kCount(kTotals.recordsHeld));
    made.add("recordsDeferred", kCount(kTotals.recordsDeferred));
    made.add("valuesLookedAt", kCount(kTotals.valuesLookedAt));
    made.add("interestLeft", kCount(kTotals.interestLeft));
    made.add("inputWindows", kCount(kTotals.inputWindows));
    made.add("inputBytes", kCount(kTotals.inputBytes));
    made.add("inputsStale", kCount(kTotals.inputsStale));
    made.add("inputsRefused", kCount(kTotals.inputsRefused));
    made.add("messagesSent", kCount(kTotals.messagesSent));
    // Each connection counted alone, the first few hundred: what a
    // tool shows of a crowded server.
    Value each = Value::array();
    for (const world_replication::ConnectionTraffic& kConnection : settings.traffic->perConnection()) {
        if (each.items().size() == kMostConnectionsAnswered) {
            break;
        }
        Value one = Value::object();
        one.add("connection", kCount(kConnection.connection.value));
        one.add("player", Value::string(entityName(kConnection.player)));
        one.add("mapped", kCount(kConnection.mapped));
        one.add("stateDatagrams", kCount(kConnection.stateDatagrams));
        one.add("stateBytes", kCount(kConnection.stateBytes));
        one.add("recordsSent", kCount(kConnection.recordsSent));
        one.add("recordsDeferred", kCount(kConnection.recordsDeferred));
        one.add("inputWindows", kCount(kConnection.inputWindows));
        one.add("inputBytes", kCount(kConnection.inputBytes));
        one.add("unacknowledged", kCount(kConnection.unacknowledged));
        if (kConnection.lagTicks.has_value()) {
            one.add("lagTicks", Value::real(*kConnection.lagTicks));
        }
        one.add("divergences", kCount(kConnection.divergences));
        each.push(std::move(one));
    }
    made.add("perConnection", std::move(each));
    return replyLine(id, "answer", std::move(made));
}

} // namespace rawframe::world_tooling
