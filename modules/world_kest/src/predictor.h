#pragma once

// A game's predicted Kest systems over one entity, for a predicting client
// (SPEC-0041): its own World holding only the player, its own machine, and
// the same program the server runs. A game with physics, of either
// dimension, also steps the player's body there, among the level's static
// bodies (D36).

#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/schema/component.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_replication/prediction.h"

#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::world_kest {

struct PredictorSettings {
    std::shared_ptr<const kest::Program> program;
    const GameDescription* game = nullptr;
    /// Every component the game declares, in declaration order.
    std::span<const schema::ComponentDescriptor> descriptors;
    kest::MachineLimits limits;
    /// The game's physics, for a game with it, in two dimensions or three:
    /// the player's body is stepped among `level`, each entity's components
    /// and their values.
    std::optional<physics2d::Physics2DSettings> physics;
    std::optional<physics3d::Physics3DSettings> physics3d;
    std::span<const std::vector<std::pair<schema::ComponentTypeId, std::vector<std::byte>>>> level;
};

[[nodiscard]] result::Result<std::unique_ptr<world_replication::Predictor>>
makePredictor(const PredictorSettings& settings);

} // namespace rawframe::world_kest
