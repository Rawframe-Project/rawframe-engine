#pragma once

// A game's service points (SPEC-0042, D199): for each, the door
// `Mods.<point>(value: T) -> T` the game's program calls, T the point's
// component type, and the one mod provider that answers it. The value is
// copied, lent to the provider as `values: [T]`, and what it left in
// `values[0]` comes back. Without a provider, or when the provider fails
// or runs out of fuel, the value comes back as it was given: a service may
// change what the game asked about, never stop the game asking.

#include "rawframe/kest/doors.h"
#include "rawframe/kest/machine.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/kest_systems.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_kest {

class ModServices {
public:
    /// One service per service point of `game`, none provided yet; `sizes`
    /// is each component's size, in the order of `game.components`.
    ModServices(const GameDescription& game, std::span<const std::size_t> sizes);

    ModServices(const ModServices&) = delete;
    ModServices& operator=(const ModServices&) = delete;

    /// Adds each point's door to `doors`. The services must outlive every
    /// machine started with them.
    [[nodiscard]] result::Status addDoors(kest::DoorTable& doors);

    /// Gives each provider of `programs` its point: `machines[i]` runs
    /// `programs[i]` (`modHandlers`, which checked each provider's shape).
    [[nodiscard]] result::Status bind(std::span<const GameModProgram> programs,
                                      std::span<const std::unique_ptr<KestSystems>> machines);

    /// Calls a provider refused or ran out of fuel on, whose value came back
    /// unchanged.
    [[nodiscard]] std::uint64_t failures() const noexcept;

    struct Service {
        std::string door;
        std::string point;
        std::string kestType;
        std::array<kest::Parameter, 1> takes{kest::Parameter{kest::Slot::Value}};
        std::array<kest::Parameter, 1> gives{kest::Parameter{kest::Slot::Value}};
        /// The provider, once bound: its machine and its entry.
        kest::Machine* machine = nullptr;
        kest::Entry entry;
        std::vector<kest::Value> frame;
        std::vector<std::byte> value;
        std::vector<std::byte> lent;
        std::uint64_t failures = 0;
    };

private:
    // Owned one by one: each door's context is its service's address.
    std::vector<std::unique_ptr<Service>> services_;
};

} // namespace rawframe::world_kest
