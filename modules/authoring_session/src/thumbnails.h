#pragma once

// A source's thumbnail from the session's last cook (D801): the cooked
// object that holds it and the first of its levels no larger than asked,
// which a browser shows as it is and an agent reads as a file.

#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace rawframe::authoring_session {

/// `authoring.thumbnail`'s answer for the source at `asset` under `game`'s
/// directory, from the cook into `cooked`: the cooked object's path, the
/// level, and its size. Refused for a path that is no source
/// (`target_not_found`), a source that is no picture (`unsupported`: a
/// mesh, an environment's cube, a volume), and
/// a game this session has not cooked or a cook that holds no such texture
/// (`target_not_found`).
[[nodiscard]] result::Result<document::Value> thumbnailOf(const std::filesystem::path& game,
                                                          std::string_view asset,
                                                          const std::optional<std::filesystem::path>& cooked,
                                                          std::uint32_t size);

} // namespace rawframe::authoring_session
