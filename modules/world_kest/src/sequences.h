#pragma once

// A game's sequences: entities of the
// game's components of `rawframe.sequence.Player`'s type, each playing one
// of the game's sequences, moved on by a system before the simulation
// phase, so `rawframe.animation.step` plays what it started that tick. A
// play key the system reaches starts
// its animator on every entity of the game's components of
// `rawframe.sequence.Cast`'s type cast in the key's track, as its
// Animator's `graph`; an event key is kept for the `Sequences.event` door
// until the next step, so a system in the simulation phase sees this
// tick's. A machine that plays none (a client) answers that
// no event fired.

#include "rawframe/diagnostics/emitter.h"
#include "rawframe/kest/doors.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/sequence/sequence.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/entity.h"
#include "rawframe/world_runtime/simulation.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// What a game's sequences did by its stop: the keys their players fired,
/// and the steps of a player naming no sequence of the game.
inline constexpr diagnostics::EventIdentity kSequenceSummary{"world_kest", "sequence_summary"};

/// The system that plays every sequence.
inline constexpr std::string_view kSequencePlaySystem = "rawframe.sequence.play";

/// `rawframe.sequence.Player` as C++ reads it.
struct SequencePlayer {
    std::uint64_t sequence = 0;
    double time = 0;
    bool playing = false;
    std::uint32_t events = 0;
};

/// `rawframe.sequence.Cast` as C++ reads it.
struct SequenceCast {
    world::EntityHandle player;
    std::uint32_t track = 0;
};

/// A game's components of the sequence types, and its sequences by the
/// identities its lines give them.
struct GameSequenceSettings {
    std::vector<schema::ComponentTypeId> players;
    std::vector<schema::ComponentTypeId> casts;
    std::map<std::uint64_t, std::shared_ptr<const sequence::Sequence>> sequences;
};

class GameSequences final : public world_runtime::SystemContributor {
public:
    explicit GameSequences(GameSequenceSettings settings) noexcept;
    GameSequences(const GameSequences&) = delete;
    GameSequences& operator=(const GameSequences&) = delete;
    ~GameSequences() override;

    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    /// Plays every player on by `seconds`.
    void play(world::World& world, double seconds);

    /// The events `player` fired in the last step, in order.
    [[nodiscard]] std::span<const std::uint64_t> eventsOf(world::EntityHandle player) const;

    /// Keys fired since it began, and steps of a player naming no sequence.
    [[nodiscard]] std::uint64_t fired() const noexcept {
        return fired_;
    }
    [[nodiscard]] std::uint64_t unknown() const noexcept {
        return unknown_;
    }

private:
    GameSequenceSettings settings_;
    std::vector<world::ColumnQuery> playerQueries_;
    std::vector<world::ColumnQuery> castQueries_;
    std::optional<schema::ComponentRuntimeId> animator_;
    std::vector<schema::ComponentRuntimeId> reads_;
    std::vector<schema::ComponentRuntimeId> writes_;
    std::unique_ptr<world::System> system_;
    std::map<world::EntityHandle, std::vector<std::uint64_t>> events_;
    std::uint64_t fired_ = 0;
    std::uint64_t unknown_ = 0;
};

/// Adds `Sequences.event(player, index)`, answered from `sequences`' last
/// step (none for a null `sequences`). Safe for untrusted code: it reads.
[[nodiscard]] result::Status addSequenceDoors(kest::DoorTable& doors, const GameSequences* sequences);

} // namespace rawframe::world_kest
