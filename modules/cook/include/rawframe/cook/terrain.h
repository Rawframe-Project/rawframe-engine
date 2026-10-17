#pragma once

// The terrain importer (`rawframe.terrain`, D854): a terrain document
// (D853) cooked into the runtime's mesh, a part a layer painted drawn with
// its layer's material, so a game's mesh line places it as it does a glTF
// and a body of shape Mesh collides with it. It takes no settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer terrainImporter() noexcept;

} // namespace rawframe::cook
