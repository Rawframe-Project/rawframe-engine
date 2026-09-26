// Effects in a game (D219): `effect` lines and the `emits` pairs of
// predicted systems, and the `Effects.<name>` doors a predictor keeps what
// a step emitted through, each with its kind, emitter, and ordinal.

#include "../src/effect_doors.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/test/test.h"
#include "rawframe/world/entity.h"
#include "rawframe/world_kest/game.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_kest;

namespace {

constexpr std::string_view kHead = "program game.kest\n"
                                   "component 6b05cdb4-0683-437a-aaac-7ad990b2ddb4 test.stick Stick\n";

bool parses(std::string_view lines) {
    return parseGame(std::string{kHead} + std::string{lines}).has_value();
}

constexpr std::string_view kProgram = R"(module game

import rawframe.world

struct Stick {
    jump: i32
}

extern fn Effects.jump(entity: world.Entity)
extern fn Effects.land(entity: world.Entity)

fn run(count: i32, entities: [world.Entity]) {
    Effects.land(entities[0])
    Effects.jump(entities[0])
    Effects.jump(entities[0])
}
)";

void unused(kest::DoorCall& call, void*) noexcept {
    call.fail("not in this test");
}

constexpr std::array<kest::Parameter, 1> kEntity = {kest::Parameter{kest::Slot::Value, kEntityType}};
constexpr std::array<kest::Parameter, 2> kInsert = {kest::Parameter{kest::Slot::Value, kEntityType},
                                                    kest::Parameter{kest::Slot::Value, "rawframe.world.Persistent"}};
const std::array<kest::Door, 3> kWorldDoors = {
    kest::Door{.name = "World.create", .function = &unused, .takes = {}, .gives = kEntity},
    kest::Door{.name = "World.destroy", .function = &unused, .takes = kEntity},
    kest::Door{.name = "Persistent.insert", .function = &unused, .takes = kInsert}};

} // namespace

RAWFRAME_TEST(EffectLinesAndEmittersAreChecked) {
    RAWFRAME_EXPECT(
        parses("effect jump predicted\neffect land confirmed_only\n"
               "system test.run simulation run read test.stick entities predicted emits jump emits land\n"));
    // Undeclared, not predicted, two emitters, a bad name or class, twice.
    RAWFRAME_EXPECT(!parses("system test.run simulation run read test.stick predicted emits jump\n"));
    RAWFRAME_EXPECT(!parses("effect jump predicted\nsystem test.run simulation run read test.stick emits jump\n"));
    RAWFRAME_EXPECT(!parses("effect jump predicted\n"
                            "system test.a simulation run read test.stick predicted emits jump\n"
                            "system test.b simulation run read test.stick predicted emits jump\n"));
    RAWFRAME_EXPECT(!parses("effect Jump predicted\n"));
    RAWFRAME_EXPECT(!parses("effect jump sometimes\n"));
    RAWFRAME_EXPECT(!parses("effect jump predicted\neffect jump confirmed_only\n"));
    const auto kGame = parseGame(std::string{kHead} + "effect jump predicted\neffect land confirmed_only\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->effects.size() == 2 &&
                    kGame->effects[1].effectClass == world_replication::EffectClass::ConfirmedOnly);
}

RAWFRAME_TEST(APredictorsDoorsKeepWhatAStepEmitted) {
    const auto kGame = parseGame(std::string{kHead} + "effect jump predicted\neffect land predicted\n"
                                                      "system test.idle simulation run read test.stick predicted\n"
                                                      "system test.run simulation run entities predicted emits land "
                                                      "emits jump\n");
    RAWFRAME_EXPECT(kGame.has_value());
    if (!kGame.has_value()) {
        return;
    }
    const std::array<kest::SourceFile, 1> kFiles = {
        kest::SourceFile{.path = "game.kest", .text = std::string{kProgram}}};
    auto program = kest_library::compile("game.kest", kFiles, {});
    RAWFRAME_EXPECT(program.has_value());
    if (!program.has_value()) {
        return;
    }
    for (const bool kKeep : {true, false}) {
        EffectDoors effects{*kGame, kKeep};
        kest::DoorTable doors;
        RAWFRAME_EXPECT(effects.addDoors(doors).has_value());
        // What `rawframe.world` asks for, never called here.
        for (const kest::Door& door : kWorldDoors) {
            RAWFRAME_EXPECT(doors.add(door).has_value());
        }
        auto machine = kest::Machine::start(
            *program, doors, kest::Trust::Trusted, {.heapBytes = 1U << 16U, .fuelPerCall = 10'000});
        RAWFRAME_EXPECT(machine.has_value());
        if (!machine.has_value()) {
            return;
        }
        const auto kEntry = (*machine)->entry("run");
        RAWFRAME_EXPECT(kEntry.has_value());
        if (!kEntry.has_value()) {
            return;
        }
        for (int step = 0; step < 2; ++step) {
            effects.clear();
            // One entity, lent as a system's entities column is.
            std::array<world::EntityHandle, 1> entities{world::EntityHandle{.slot = 1, .generation = 1}};
            std::vector<kest::Value> frame(std::max<std::size_t>(kEntry->frameSlots, 2), kest::Value{.integer = 0});
            frame[0].integer = 1;
            auto lent = (*machine)->lend(entities.data(), 1, kEntityType, sizeof(world::EntityHandle));
            RAWFRAME_EXPECT(lent.has_value());
            if (!lent.has_value()) {
                return;
            }
            frame[1] = *lent;
            RAWFRAME_EXPECT((*machine)->call(*kEntry, frame).hasValue());
            (*machine)->endLend(*lent);
            if (!kKeep) {
                RAWFRAME_EXPECT(effects.emitted().empty());
                continue;
            }
            // Land is kind 1, jump kind 0, both emitted by the game's
            // second system; each kind counts its own ordinals, from nought
            // each step.
            const auto kEmitted = effects.emitted();
            RAWFRAME_EXPECT(kEmitted.size() == 3);
            if (kEmitted.size() == 3) {
                RAWFRAME_EXPECT(kEmitted[0].kind == 1 && kEmitted[0].system == 1 && kEmitted[0].ordinal == 0);
                RAWFRAME_EXPECT(kEmitted[1].kind == 0 && kEmitted[1].system == 1 && kEmitted[1].ordinal == 0);
                RAWFRAME_EXPECT(kEmitted[2].kind == 0 && kEmitted[2].system == 1 && kEmitted[2].ordinal == 1);
            }
        }
    }
}
