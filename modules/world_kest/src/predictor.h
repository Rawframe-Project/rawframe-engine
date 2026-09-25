#pragma once

// A game's predicted Kest systems over one entity, for a predicting client
// (SPEC-0041): its own World holding only the player, its own machine, and
// the same program the server runs.

#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/schema/component.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_replication/prediction.h"

#include <memory>
#include <span>

namespace rawframe::world_kest {

struct PredictorSettings {
    std::shared_ptr<const kest::Program> program;
    const GameDescription* game = nullptr;
    /// Every component the game declares, in declaration order.
    std::span<const schema::ComponentDescriptor> descriptors;
    kest::MachineLimits limits;
};

[[nodiscard]] result::Result<std::unique_ptr<world_replication::Predictor>>
makePredictor(const PredictorSettings& settings);

} // namespace rawframe::world_kest
