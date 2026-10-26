#pragma once

// The tooling endpoint's traffic verb: a replication server's totals
// and connections, the inspect grant's, for a network panel to turn into
// rates.

#include "rawframe/document/json.h"
#include "rawframe/world/time.h"
#include "rawframe/world_tooling/server.h"

#include <optional>
#include <string>
#include <string_view>

namespace rawframe::world_tooling {

/// The reply line to `tooling.traffic`, `tick` the next to run; none for
/// another kind.
[[nodiscard]] std::optional<std::string>
answerTraffic(std::string_view kind, const document::Value& id, const ToolingSettings& settings, world::TickIndex tick);

} // namespace rawframe::world_tooling
