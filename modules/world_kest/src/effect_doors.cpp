#include "effect_doors.h"

#include <algorithm>

namespace rawframe::world_kest {

void EffectDoors::emit(kest::DoorCall& call, void* context) noexcept {
    EffectDoors::Kind& kind = *static_cast<EffectDoors::Kind*>(context);
    EffectDoors& owner = *kind.owner;
    if (!owner.keep_) {
        return;
    }
    // A step emits at most so many of one kind; past it the rest are not
    // kept, and a program that spins on a door runs out of fuel first.
    constexpr std::uint32_t kMaximumPerStep = 256;
    if (kind.count >= kMaximumPerStep) {
        return;
    }
    owner.emitted_.push_back(
        world_replication::StepEffect{.kind = kind.kind, .system = kind.system, .ordinal = kind.count});
    ++kind.count;
    call.spendFuel(1);
}

EffectDoors::EffectDoors(const GameDescription& game, bool keep) : keep_(keep) {
    for (std::size_t index = 0; index < game.effects.size(); ++index) {
        auto& kind = *kinds_.emplace_back(std::make_unique<Kind>());
        kind.owner = this;
        kind.kind = static_cast<std::uint32_t>(index);
        kind.door = "Effects." + game.effects[index].name;
        // Its one emitter's place among the game's systems: the system's
        // stable identity within the game.
        const auto kEmitter = std::ranges::find_if(game.systems, [&](const GameSystem& system) {
            return std::ranges::contains(system.emits, game.effects[index].name);
        });
        kind.system = static_cast<std::uint32_t>(kEmitter - game.systems.begin());
    }
}

result::Status EffectDoors::addDoors(kest::DoorTable& doors) {
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        RAWFRAME_TRY(doors.add(
            kest::Door{.name = kind->door, .function = &emit, .context = kind.get(), .takes = takes_, .gives = {}}));
    }
    return {};
}

void EffectDoors::clear() noexcept {
    emitted_.clear();
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        kind->count = 0;
    }
}

} // namespace rawframe::world_kest
