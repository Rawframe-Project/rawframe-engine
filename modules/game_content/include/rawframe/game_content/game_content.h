#pragma once

// A game's cooked content as a process holds it: one content store, shared
// through composition by every asset family that reads from it, and one
// catalog that holds the resources of the families this process admits.

#include "rawframe/composition/participant.h"
#include "rawframe/content/catalog.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/content/store.h"
#include "rawframe/result/result.h"

#include <optional>
#include <span>
#include <vector>

namespace rawframe::game_content {

/// A Build of the Composition a process's content is: the reference its
/// record gives, and every resource the Build holds, admitted or not.
struct ComposedBuild {
    content::BuildReference reference;
    std::vector<content::ManifestEntry> entries;
};

class GameContent {
public:
    GameContent() = default;
    GameContent(const GameContent&) = delete;
    GameContent& operator=(const GameContent&) = delete;
    virtual ~GameContent() = default;

    /// The store every family reads verified bytes from, by identity.
    [[nodiscard]] virtual content::ContentStore& store() noexcept = 0;
    /// Whether the process has cooked content at all (`content.root` or
    /// `content.composition`); a family that can do without it asks first.
    [[nodiscard]] virtual bool held() const noexcept = 0;
    /// Admits a family's representations: the catalog now holds the
    /// manifest's resources of those, and is published as the next
    /// generation. A family admits before it asks for anything. Refused
    /// when the process has no cooked content.
    [[nodiscard]] virtual result::Status admit(std::span<const content::AdmittedRepresentation> representations) = 0;
    /// The CompositionId of the Composition this content is, if it is one
    /// (SPEC-0021): what peers must agree on before play (ADR-0023).
    [[nodiscard]] virtual const std::optional<base::Sha256Digest>& compositionId() const noexcept = 0;
    /// The Composition's Game Build, if this content is one.
    [[nodiscard]] virtual const ComposedBuild* composedGame() const noexcept = 0;
    /// Its Mods, in the record's subject order; none if it is not one.
    /// Whether the game takes them is the game's to decide (SPEC-0042, D179).
    [[nodiscard]] virtual std::span<const ComposedBuild> composedMods() const noexcept = 0;
};

inline constexpr composition::Capability<GameContent> kGameContent{"rawframe.content.game"};

} // namespace rawframe::game_content
