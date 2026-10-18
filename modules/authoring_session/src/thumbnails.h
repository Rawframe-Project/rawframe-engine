#pragma once

// A source's thumbnail from the session's last cook (D801): the cooked
// object that holds it and the first of its levels no larger than asked,
// which a browser shows as it is and an agent reads as a file. A mesh's is
// drawn by the thumbnail tool (D899) beside the cook, every mesh of it
// at the first asking, and kept there as a texture.

#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace rawframe::authoring_session {

/// The meshes' pictures a session has had drawn: the thumbnail tool
/// (rawframe-thumbnail), and the cook it last drew, which is not drawn
/// again whether or not it could be.
struct MeshPictures {
    std::filesystem::path tool;
    std::optional<std::filesystem::path> drawn;
};

/// `authoring.thumbnail`'s answer for the source at `asset` under `game`'s
/// directory, from the cook into `cooked`: the path of the texture that
/// holds it (the cooked object for a texture, the drawn picture for a
/// mesh), the level, and its size. Refused for a path that is no source
/// (`target_not_found`), a source that is no picture (`unsupported`: a
/// scene, an environment's cube, a volume, a mesh with no tool to draw it,
/// or one the tool could not draw, as on a machine with no device), and a
/// game this session has not cooked or a cook that holds no such source
/// (`target_not_found`).
[[nodiscard]] result::Result<document::Value> thumbnailOf(const std::filesystem::path& game,
                                                          std::string_view asset,
                                                          const std::optional<std::filesystem::path>& cooked,
                                                          std::uint32_t size,
                                                          MeshPictures& meshes);

} // namespace rawframe::authoring_session
