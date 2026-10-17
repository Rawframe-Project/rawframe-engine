#pragma once

// The tooling endpoint's profile verb (D838): what the World's last ticks
// took, each tick and each system, the inspect grant's, for a profiler.

#include "rawframe/document/json.h"
#include "rawframe/world_tooling/server.h"

#include <optional>
#include <string>
#include <string_view>

namespace rawframe::world_tooling {

/// The reply line to `tooling.profile`; none for another kind.
[[nodiscard]] std::optional<std::string>
answerProfile(std::string_view kind, const document::Value& id, const ToolingSettings& settings);

} // namespace rawframe::world_tooling
