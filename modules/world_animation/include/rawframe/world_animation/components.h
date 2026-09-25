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

/// The engine's animation components as a script must declare them.
[[nodiscard]] std::span<const schema::ComponentLayout> componentLayouts() noexcept;

} // namespace rawframe::world_animation
