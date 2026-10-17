#include "rawframe/cook/importers.h"

#include "rawframe/cook/animation.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/blockout.h"
#include "rawframe/cook/font.h"
#include "rawframe/cook/game.h"
#include "rawframe/cook/kest.h"
#include "rawframe/cook/mesh.h"
#include "rawframe/cook/mod.h"
#include "rawframe/cook/scene.h"
#include "rawframe/cook/terrain.h"
#include "rawframe/cook/text.h"
#include "rawframe/cook/texture.h"
#include "rawframe/cook/tilemap.h"

#include <algorithm>
#include <utility>

namespace rawframe::cook {

std::vector<Importer> engineImporters(std::optional<ShaderTools> tools) {
    return {animationImporter(),
            audioImporter(),
            blockoutImporter(),
            canvasImporter(),
            fontImporter(),
            gameImporter(),
            kestImporter(),
            materialImporter(std::move(tools)),
            meshImporter(),
            modImporter(),
            postProcessImporter(),
            sceneImporter(),
            terrainImporter(),
            textImporter(),
            textureImporter(),
            tilemapImporter()};
}

const Importer* importerNamed(std::span<const Importer> importers, std::string_view identity) noexcept {
    const auto kFound = std::ranges::find(importers, identity, &Importer::identity);
    return kFound != importers.end() ? &*kFound : nullptr;
}

} // namespace rawframe::cook
