#pragma once

// SPEC-0041's effect emission surface (D219): a door `Effects.<name>` for
// each effect a game declares, taking the entity the effect is about. A
// server's doors keep nothing; a predictor's keep what one step emitted,
// each identified by its kind, its one emitting system, and its place among
// that system's emissions of the kind in the step.

#include "rawframe/kest/doors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_replication/prediction.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_kest {

class EffectDoors {
public:
    /// `keep` for a predictor, which reports what each step emitted.
    EffectDoors(const GameDescription& game, bool keep);

    [[nodiscard]] result::Status addDoors(kest::DoorTable& doors);

    /// Forgets the last step's effects, before the next.
    void clear() noexcept;
    [[nodiscard]] std::span<const world_replication::StepEffect> emitted() const noexcept {
        return emitted_;
    }

    struct Kind {
        EffectDoors* owner = nullptr;
        std::uint32_t kind = 0;
        std::uint32_t system = 0;
        std::string door;
        /// Emitted so far this step.
        std::uint32_t count = 0;
    };

private:
    std::vector<std::unique_ptr<Kind>> kinds_;
    bool keep_ = false;
    std::vector<world_replication::StepEffect> emitted_;
    std::array<kest::Parameter, 1> takes_{kest::Parameter{kest::Slot::Value, kEntityType}};

    static void emit(kest::DoorCall& call, void* context) noexcept;
};

} // namespace rawframe::world_kest
