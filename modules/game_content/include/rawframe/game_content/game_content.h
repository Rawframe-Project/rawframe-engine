#pragma once

// A game's cooked content as a process holds it: one content store, shared
// through composition by every asset family that reads from it, and one
// catalog that holds the resources of the families this process admits.

#include "rawframe/composition/participant.h"
#include "rawframe/content/catalog.h"
#include "rawframe/content/store.h"
#include "rawframe/result/result.h"

#include <span>

namespace rawframe::game_content {

class GameContent {
public:
    GameContent() = default;
    GameContent(const GameContent&) = delete;
    GameContent& operator=(const GameContent&) = delete;
    virtual ~GameContent() = default;

    /// The store every family reads verified bytes from, by identity.
    [[nodiscard]] virtual content::ContentStore& store() noexcept = 0;
    /// Admits a family's representations: the catalog now holds the
    /// manifest's resources of those, and is published as the next
    /// generation. A family admits before it asks for anything. Refused
    /// when the process has no cooked content.
    [[nodiscard]] virtual result::Status admit(std::span<const content::AdmittedRepresentation> representations) = 0;
};

inline constexpr composition::Capability<GameContent> kGameContent{"rawframe.content.game"};

} // namespace rawframe::game_content
