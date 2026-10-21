#pragma once

// A game's own window icon (D951), as its export writes it: a texture
// (rawframe.texture) whose square levels become the icon's images.

#include "rawframe/composition/held_files.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace rawframe::window_host {

/// One image of an icon: its side, and its texels, RGBA with straight
/// alpha, a byte a channel, rows from the top.
struct IconSide {
    std::uint32_t side = 0;
    std::vector<std::uint8_t> pixels;
};

/// The images of the icon at `path`, from `held` where the host holds its
/// files, else the file system: of the texture's 8-bit RGBA levels that are
/// square and 16 to 256 texels a side, the largest and the three smallest,
/// largest first. Refuses (BadConfiguration) a file that cannot be read,
/// one past 4 MiB, one that is not a texture, or one with no such level.
[[nodiscard]] result::Result<std::vector<IconSide>> readIcon(std::string_view path, const composition::HeldFiles* held);

} // namespace rawframe::window_host
