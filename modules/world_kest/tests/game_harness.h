#pragma once

// What the game tests share: a composition of the game and the World
// runtime, a participant that finds the simulation the game was loaded
// into, the test games held as a host holds files, and small file helpers.

#include "rawframe/composition/composition.h"
#include "rawframe/test/files.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_runtime/simulation.h"

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::game_test {

inline const std::array<composition::RegistrarEntry, 2> kRegistrars = {
    composition::RegistrarEntry{"world_kest", &world_kest::registerParticipants, world_kest::kScopes},
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
};

/// The simulation the game was loaded into, found through a participant of
/// the test's own.
inline world_runtime::Simulation* simulation = nullptr;

inline result::Result<composition::ParticipantOwner> makeWatcher(composition::ParticipantContext& context) noexcept {
    RAWFRAME_TRY_ASSIGN(simulation, context.capability(world_runtime::kSimulation));
    struct Watcher final : composition::Participant {};
    return composition::ParticipantOwner{new Watcher{}};
}

inline constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};

inline void registerWatcher(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "test.watcher",
        .factory = &makeWatcher,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
    });
}

inline const std::array<composition::RegistrarEntry, 3> kWatched = {
    kRegistrars[0], kRegistrars[1], composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

inline std::vector<std::pair<float, float>> positions() {
    world::World& world = *simulation->world();
    const auto kId = world.registry().find(schema::ComponentTypeId::fromText("0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<std::pair<float, float>> found;
    query->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
        const auto* values = reinterpret_cast<const float*>(chunk.columns[0]);
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            found.emplace_back(values[row * 2], values[(row * 2) + 1]);
        }
    });
    return found;
}

/// The test games' files, held under `game/` as a host holds what it
/// fetched (D167), so that a game is played where there are no files.
inline const composition::HeldFiles& heldGames() {
    static const composition::HeldFiles kHeld = [] {
        std::vector<composition::HeldFiles::File> files;
        for (const std::string& path : test::filesUnder(RAWFRAME_WORLD_KEST_GAMES, "")) {
            const std::string kText = test::readFile(RAWFRAME_WORLD_KEST_GAMES + path);
            const auto kBytes = std::as_bytes(std::span{kText.data(), kText.size()});
            files.emplace_back("game/" + path, std::vector<std::byte>{kBytes.begin(), kBytes.end()});
        }
        return *composition::HeldFiles::of(std::move(files));
    }();
    return kHeld;
}

inline void writeText(const std::string& path, std::string_view text) {
    if (std::FILE* file = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    }
}

inline std::string readText(const std::string& path) {
    std::string text;
    if (std::FILE* file = std::fopen(path.c_str(), "rb")) {
        char chunk[4096];
        std::size_t got = 0;
        while ((got = std::fread(chunk, 1, sizeof chunk, file)) != 0) {
            text.append(chunk, got);
        }
        std::fclose(file);
    }
    return text;
}

} // namespace rawframe::game_test
