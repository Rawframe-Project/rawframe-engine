#pragma once

// A client's presentation of the World it mirrors (D260): what only a client
// that draws decides. Once a game tick, before the canvas extracts: each
// `presentation` component attached, zeroed, to every mirrored entity that
// has its `on` component; the game's `present` systems run over the mirror
// in line order, a schedule of their own on the game's program; then every
// animator played (D258). A dedicated server never holds any of it.

#include "rawframe/composition/participant.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/diagnostics/emitter.h"
#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/schema/component.h"
#include "rawframe/world/time.h"
#include "rawframe/world/world.h"
#include "rawframe/world_animation/animation.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_replication/messages.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace rawframe::world_kest {

struct PresentationSettings {
    std::shared_ptr<const kest::Program> program;
    const GameDescription* game = nullptr;
    /// Every component the game declares, in declaration order.
    std::span<const schema::ComponentDescriptor> descriptors;
    kest::MachineLimits limits;
    /// The game's animators, if it has any.
    std::optional<world_animation::AnimationSettings> animation;
};

struct PresentationStatistics {
    std::uint64_t ticks = 0;
    /// Presentation components attached to mirrored entities.
    std::uint64_t attached = 0;
    /// Present systems refused or run out, each changing nothing that tick.
    std::uint64_t systemsFailed = 0;
    /// Mirrors bound, one per World the client made.
    std::uint64_t bound = 0;
};

class ClientPresentation {
public:
    /// Nothing for a game with no presentation components, present systems,
    /// or animators.
    [[nodiscard]] static result::Result<std::unique_ptr<ClientPresentation>> create(PresentationSettings settings);

    ClientPresentation(const ClientPresentation&) = delete;
    ClientPresentation& operator=(const ClientPresentation&) = delete;
    ~ClientPresentation();

    /// One game tick of `mirror`, whose own player is `player` (or none
    /// yet), its present systems reading the messages `arrived` for it
    /// (D266), bound afresh when it is another World than the last (a
    /// client that made a new one). A present system that is refused or
    /// runs out changes nothing that tick, as a system does, and is counted
    /// and logged (`present_failed`) through `emitter`.
    [[nodiscard]] result::Status present(world::World& mirror,
                                         world::EntityHandle player,
                                         std::span<const world_replication::ReceivedMessage> arrived,
                                         world::TickRate rate,
                                         diagnostics::Emitter emitter = {});

    [[nodiscard]] PresentationStatistics statistics() const noexcept;
    [[nodiscard]] world_animation::AnimationStatistics animationStatistics() const noexcept;

    struct State;

private:
    explicit ClientPresentation(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// What the loaded game gives a client that draws; provided by the
/// participant that loads the game.
class PresentationPlan {
public:
    PresentationPlan() = default;
    PresentationPlan(const PresentationPlan&) = delete;
    PresentationPlan& operator=(const PresentationPlan&) = delete;
    virtual ~PresentationPlan() = default;

    /// The game's presentation, or nothing when it presents nothing.
    [[nodiscard]] virtual result::Result<std::unique_ptr<ClientPresentation>> presentation() const = 0;
};

inline constexpr composition::Capability<PresentationPlan> kPresentationPlan{"rawframe.world_kest.presentation_plan"};

/// Contributes `rawframe.world_kest.presented`, never on a dedicated server:
/// once a frame, in `run_worlds`, the World of the client that plays (or
/// else the first) presented for each game tick (`world.tick_rate`, 60) the
/// frame's time holds, at most four, the rest dropped and counted. It logs
/// `presented_summary` when it stops.
void registerPresented(composition::ParticipantRegistrar& registrar) noexcept;

} // namespace rawframe::world_kest
