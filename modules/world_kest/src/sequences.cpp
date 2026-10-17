#include "sequences.h"

#include "rawframe/world_animation/components.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace rawframe::world_kest {

namespace {

/// `rawframe.sequence.SequenceEvent`, as the program lays it out.
struct SequenceEvent {
    bool found = false;
    std::uint64_t event = 0;
};

constexpr std::array<kest::Parameter, 2> kEventTakes = {kest::Parameter{kest::Slot::Value, "rawframe.world.Entity"},
                                                        kest::Parameter{kest::Slot::U32}};
constexpr std::array<kest::Parameter, 1> kEventGives = {
    kest::Parameter{kest::Slot::Value, "rawframe.sequence.SequenceEvent"}};

/// `Sequences.event(player, index)`.
void eventDoor(kest::DoorCall& call, void* context) noexcept {
    const auto* const kSequences = static_cast<const GameSequences*>(context);
    world::EntityHandle player;
    if (!call.value(0, std::as_writable_bytes(std::span{&player, 1}))) {
        call.fail("the program's Entity is not the engine's");
        return;
    }
    SequenceEvent answer;
    const std::span<const std::uint64_t> kEvents =
        kSequences != nullptr ? kSequences->eventsOf(player) : std::span<const std::uint64_t>{};
    const auto kIndex = static_cast<std::uint64_t>(call.integer(1));
    if (kIndex < kEvents.size()) {
        answer = SequenceEvent{.found = true, .event = kEvents[kIndex]};
    }
    if (!call.answerValue(std::as_bytes(std::span{&answer, 1}))) {
        call.fail("the program's SequenceEvent is not the engine's");
    }
}

/// The playing as a system of the World.
class PlaySystem final : public world::System {
public:
    explicit PlaySystem(GameSequences& owner) noexcept : owner_(&owner) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        owner_->play(context.world,
                     static_cast<double>(context.rate.seconds) / static_cast<double>(context.rate.ticks));
        return {};
    }

private:
    GameSequences* owner_;
};

} // namespace

GameSequences::GameSequences(GameSequenceSettings settings) noexcept : settings_(std::move(settings)) {
}

GameSequences::~GameSequences() = default;

result::Status GameSequences::declareSystems(const schema::SchemaRegistry& registry,
                                             std::vector<world::SystemDeclaration>& systems) noexcept {
    playerQueries_.clear();
    castQueries_.clear();
    reads_.clear();
    writes_.clear();
    for (const schema::ComponentTypeId kId : settings_.players) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kPlayer, registry.find(kId));
        const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{kPlayer, world::Access::Write}};
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kTerms, registry));
        playerQueries_.push_back(std::move(query));
        writes_.push_back(kPlayer);
    }
    for (const schema::ComponentTypeId kId : settings_.casts) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kCast, registry.find(kId));
        const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{kCast, world::Access::Read}};
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kTerms, registry));
        castQueries_.push_back(std::move(query));
        reads_.push_back(kCast);
    }
    // A game that animates nothing holds no Animator, and its play keys
    // start nothing.
    animator_.reset();
    if (const auto kAnimator = registry.find(world_animation::Animator::kComponentTypeId); kAnimator.has_value()) {
        animator_ = *kAnimator;
        writes_.push_back(*kAnimator);
    }
    system_ = std::make_unique<PlaySystem>(*this);
    systems.push_back(world::SystemDeclaration{.identity = kSequencePlaySystem,
                                               .phase = world::Phase::PreSimulation,
                                               .reads = reads_,
                                               .writes = writes_,
                                               .system = system_.get()});
    return {};
}

void GameSequences::play(world::World& world, double seconds) {
    // Each player's cast, by track and then by entity, so the same cast
    // starts in the same order.
    std::map<world::EntityHandle, std::vector<std::pair<std::uint32_t, world::EntityHandle>>> casts;
    for (world::ColumnQuery& query : castQueries_) {
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                SequenceCast cast;
                std::memcpy(&cast, chunk.columns[0] + (row * sizeof(SequenceCast)), sizeof(SequenceCast));
                casts[cast.player].emplace_back(cast.track, chunk.entities[row]);
            }
        });
    }
    for (auto& [kPlayer, cast] : casts) {
        std::ranges::sort(cast);
    }
    events_.clear();
    for (world::ColumnQuery& query : playerQueries_) {
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                std::byte* const kData = chunk.columns[0] + (row * sizeof(SequencePlayer));
                SequencePlayer player;
                std::memcpy(&player, kData, sizeof(SequencePlayer));
                player.events = 0;
                const auto kSequence = settings_.sequences.find(player.sequence);
                if (player.playing && kSequence == settings_.sequences.end()) {
                    ++unknown_;
                } else if (player.playing) {
                    const sequence::Sequence& timeline = *kSequence->second;
                    const sequence::Advance kAdvance = sequence::advance(timeline, player.time, seconds);
                    player.time = kAdvance.time;
                    player.playing = !kAdvance.ended;
                    const auto kCast = casts.find(chunk.entities[row]);
                    for (const std::size_t kIndex : kAdvance.keys) {
                        const sequence::Key& key = timeline.keys[kIndex];
                        ++fired_;
                        if (key.kind == sequence::KeyKind::Event) {
                            events_[chunk.entities[row]].push_back(key.event);
                            ++player.events;
                            continue;
                        }
                        if (kCast == casts.end() || !animator_.has_value()) {
                            continue;
                        }
                        for (const auto& [kTrack, kEntity] : kCast->second) {
                            auto* const animator =
                                static_cast<world_animation::Animator*>(world.getErased(kEntity, *animator_));
                            if (kTrack == key.track && animator != nullptr) {
                                animator->graph = key.animator;
                            }
                        }
                    }
                }
                std::memcpy(kData, &player, sizeof(SequencePlayer));
            }
        });
    }
}

std::span<const std::uint64_t> GameSequences::eventsOf(world::EntityHandle player) const {
    const auto kFound = events_.find(player);
    return kFound == events_.end() ? std::span<const std::uint64_t>{} : std::span{kFound->second};
}

result::Status addSequenceDoors(kest::DoorTable& doors, const GameSequences* sequences) {
    return doors.add(kest::Door{.name = "Sequences.event",
                                .function = &eventDoor,
                                .context = const_cast<GameSequences*>(sequences),
                                .takes = kEventTakes,
                                .gives = kEventGives,
                                .safeForUntrusted = true});
}

} // namespace rawframe::world_kest
