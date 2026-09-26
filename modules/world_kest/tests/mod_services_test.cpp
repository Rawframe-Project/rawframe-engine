// Service points (D199): a game's program asks `Mods.<point>` and gets what
// the mod's provider left in the value it was lent, or the value it asked
// about when no mod provides it or the provider fails.

#include "../src/mod_services.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/kest_systems.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_kest;

namespace {

constexpr std::string_view kGame = "program game.kest\n"
                                   "component 3d8b6f21-7c4e-4a95-b0d3-e6f1a2c95b78 test.penalty Penalty\n"
                                   "modapi test 1\nextension penalty service test.penalty exclusive\n";

constexpr std::string_view kGameProgram = R"(module game

struct Penalty {
    taken: i32
}

extern fn Mods.penalty(value: Penalty) -> Penalty

fn ask(taken: i32) -> i32 {
    return Mods.penalty(Penalty(taken)).taken
}
)";

constexpr std::string_view kModProgram = R"(module mod

struct Penalty {
    taken: i32
}

fn double(values: [Penalty]) {
    values[0].taken = values[0].taken * 2
}

fn spin(values: [Penalty]) {
    values[0].taken = 99
    while true {
    }
}

fn stray(values: [Penalty]) {
    values[0].taken = 99
    values[3].taken = 1
}
)";

std::shared_ptr<const kest::Program> compiled(std::string_view path, std::string_view text) {
    const std::array<kest::SourceFile, 1> kFiles = {
        kest::SourceFile{.path = std::string{path}, .text = std::string{text}}};
    auto program = kest::Program::compile(kFiles, {});
    RAWFRAME_EXPECT(program.has_value());
    return program.has_value() ? *program : nullptr;
}

/// What the game's program is answered for `taken` with `provider` of the
/// mod's program providing the penalty, or none for an empty name.
struct Asked {
    std::int64_t answer = 0;
    std::uint64_t failures = 0;
};

Asked ask(std::string_view provider, std::int64_t taken) {
    const auto kGameDescription = parseGame(kGame);
    RAWFRAME_EXPECT(kGameDescription.has_value());
    if (!kGameDescription.has_value()) {
        return {};
    }
    const std::array<std::size_t, 1> kSizes = {sizeof(std::int32_t)};
    ModServices services{*kGameDescription, kSizes};
    std::vector<std::unique_ptr<KestSystems>> machines;
    std::vector<GameModProgram> programs;
    if (!provider.empty()) {
        auto mod = KestSystems::create(KestSystemsSettings{.program = compiled("mod.kest", kModProgram),
                                                           .doors = {},
                                                           .components = {},
                                                           .prefabs = {},
                                                           .limits = {.heapBytes = 1U << 16U, .fuelPerCall = 10'000},
                                                           .trust = kest::Trust::Untrusted,
                                                           .systems = {}});
        RAWFRAME_EXPECT(mod.has_value());
        if (!mod.has_value()) {
            return {};
        }
        machines.push_back(std::move(*mod));
        programs.push_back(GameModProgram{.mod = "fan/penalty",
                                          .entry = "mod.kest",
                                          .files = {},
                                          .handlers = {},
                                          .providers = {{.point = "penalty", .function = std::string{provider}}}});
    }
    RAWFRAME_EXPECT(services.bind(programs, machines).has_value());
    kest::DoorTable doors;
    RAWFRAME_EXPECT(services.addDoors(doors).has_value());
    auto machine = kest::Machine::start(compiled("game.kest", kGameProgram),
                                        doors,
                                        kest::Trust::Trusted,
                                        {.heapBytes = 1U << 16U, .fuelPerCall = 100'000});
    RAWFRAME_EXPECT(machine.has_value());
    if (!machine.has_value()) {
        return {};
    }
    const auto kEntry = (*machine)->entry("ask");
    RAWFRAME_EXPECT(kEntry.has_value());
    if (!kEntry.has_value()) {
        return {};
    }
    std::vector<kest::Value> frame(std::max<std::size_t>(kEntry->frameSlots, 1), kest::Value{.integer = 0});
    frame[0].integer = taken;
    RAWFRAME_EXPECT((*machine)->call(*kEntry, frame).hasValue());
    return Asked{.answer = frame[0].integer, .failures = services.failures()};
}

} // namespace

RAWFRAME_TEST(AProviderAnswersWhatItLeftInTheValue) {
    const Asked kAsked = ask("double", 3);
    RAWFRAME_EXPECT(kAsked.answer == 6 && kAsked.failures == 0);
}

RAWFRAME_TEST(WithoutAProviderTheValueComesBack) {
    const Asked kAsked = ask("", 3);
    RAWFRAME_EXPECT(kAsked.answer == 3 && kAsked.failures == 0);
}

RAWFRAME_TEST(AFailingProviderChangesNothing) {
    // One that never ends runs out of fuel; one that strays past its value
    // is refused. Either way, what it wrote before failing is not the
    // answer.
    for (const std::string_view kProvider : {"spin", "stray"}) {
        const Asked kAsked = ask(kProvider, 3);
        RAWFRAME_EXPECT(kAsked.answer == 3 && kAsked.failures == 1);
    }
}
