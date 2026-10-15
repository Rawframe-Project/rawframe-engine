#pragma once

// The tooling endpoint's reply lines and the values its records carry,
// shared by the server and the preview's verbs.

#include "rawframe/document/json.h"
#include "rawframe/world_tooling/errors.h"
#include "rawframe/world_tooling/preview.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::world_tooling {

/// One reply line, its line feed included.
[[nodiscard]] std::string replyLine(const document::Value& id, std::string_view member, document::Value value);

/// An error reply line: its code's name and why.
[[nodiscard]] std::string errorLine(const document::Value& id, ToolingError error, std::string_view why);

/// A point of three finite numbers within a million metres, as `tooling.look`
/// and `tooling.pick` give one; none otherwise.
[[nodiscard]] std::optional<std::array<double, 3>> pointOf(const document::Value* point);

/// What `tooling.look` asks: an eye looking at a target with a field of
/// view in degrees (1 to 179), or none to give the player's camera back;
/// nothing, with `refusal` said, for a record out of form (D432).
[[nodiscard]] std::optional<std::optional<Look>> lookOf(const document::Value& record, std::string& refusal);

} // namespace rawframe::world_tooling
