#pragma once

// The tooling endpoint's preview verbs: looking, marking,
// hiding layers, overlays, and view modes, each the view grant's, answered
// by the preview the Runtime lends.

#include "rawframe/document/json.h"
#include "rawframe/world_tooling/server.h"

#include <optional>
#include <string>
#include <string_view>

namespace rawframe::world_tooling {

/// The reply line to `record` of `kind`, if it is one of the preview's
/// verbs; none for another kind.
[[nodiscard]] std::optional<std::string> answerPreview(std::string_view kind,
                                                       const document::Value& record,
                                                       const document::Value& id,
                                                       const ToolingSettings& settings);

} // namespace rawframe::world_tooling
