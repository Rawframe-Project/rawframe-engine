#pragma once

// The component animation reads and writes (SPEC-0035): an entity with an
// Animator plays the graph of the game's animator it names, from the
// parameters in the game's component that animator binds, is told after
// each step how many events it fired, and may ask the step for a transition. Its layout is fixed and plain,
// so a script declares the same struct (rawframe.animation's) and the
// field table below says what a matching declaration holds.

#include "rawframe/schema/layout.h"
#include "rawframe/schema/stable_id.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::world_animation {

struct Animator {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("ab29d523-9461-4549-a049-2bb048965ba0");
    static constexpr std::string_view kComponentName = "rawframe.animation.animator";

    /// The game's animator, by the identity its `animator` line gives it.
    /// Changing it plays that animator's graph from its start.
    std::uint64_t graph = 0;
    /// animation::Relevance: `Presentation` plays where poses are drawn,
    /// `Simulation` on a dedicated server too (SPEC-0035's declared
    /// simulation relevance).
    std::uint8_t relevance = 0;
    /// Written by every step: the events its graph fired in it, which the
    /// game's queries read one by one.
    std::uint32_t events = 0;
    /// A transition request for the next step (SPEC-0035): an event
    /// identity that event conditions hold on during that step only. The
    /// step clears it, played here or not; nought is none. Made where it is
    /// written and never replicated as a request, so a transition every peer
    /// must take belongs on a replicated parameter instead.
    std::uint64_t request = 0;
};

/// Root motion (SPEC-0035, D134): on an entity with an Animator, asks that
/// the skeleton's root motion source move the character rather than its
/// pose. Every step writes how the character moved and turned in it, in
/// its own frame as it was before, and the running whole since the
/// component was added; a step that plays nothing moved it nowhere. The
/// game's movement reads it after `rawframe.animation.step`. A dedicated
/// server plays only animators of `Simulation` relevance, so a character
/// its animation moves is one.
struct RootMotion {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("70bcba44-b0d4-4384-8219-149da8023c44");
    static constexpr std::string_view kComponentName = "rawframe.animation.root_motion";

    /// This step's move, and its turn as a unit quaternion.
    double moveX = 0;
    double moveY = 0;
    double moveZ = 0;
    double turnX = 0;
    double turnY = 0;
    double turnZ = 0;
    double turnW = 1;
    /// Every step's move and turn composed: where the character is, and
    /// how it faces, from where it was when the component was added. A
    /// facing of all noughts is the identity, as a spawn line leaves it.
    double travelX = 0;
    double travelY = 0;
    double travelZ = 0;
    double facingX = 0;
    double facingY = 0;
    double facingZ = 0;
    double facingW = 1;
};

/// The engine's animation components as a script must declare them.
[[nodiscard]] std::span<const schema::ComponentLayout> componentLayouts() noexcept;

} // namespace rawframe::world_animation
