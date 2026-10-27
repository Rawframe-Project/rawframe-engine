#pragma once

// The tooling endpoint's run verbs: the World paused, let run again,
// and stepped tick by tick, each the run grant's, on the Runtime's
// simulation.

#include "rawframe/document/json.h"
#include "rawframe/world_tooling/server.h"

#include <optional>
#include <string>
#include <string_view>

namespace rawframe::world_tooling {

/// The reply line to `record` of `kind`, if it is `tooling.run` or
/// `tooling.step`; none for another kind.
[[nodiscard]] std::optional<std::string> answerRun(std::string_view kind,
                                                   const document::Value& record,
                                                   const document::Value& id,
                                                   const ToolingSettings& settings);

} // namespace rawframe::world_tooling
