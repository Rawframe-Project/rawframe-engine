// A client's presentation of the World it mirrors (D260): each presentation
// component attached to the entities that have its `on` component, the
// present systems run over the mirror in line order, a refused one changing
// nothing, and a new mirror bound afresh.

#include "../src/presentation.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/game_files.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_kest;

namespace {

constexpr auto kStickId = schema::ComponentTypeId::fromText("51a3c0de-1111-4a2b-8c3d-4e5f60718293");
constexpr auto kLookId = schema::ComponentTypeId::fromText("51a3c0de-2222-4a2b-8c3d-4e5f60718293");

/// A game whose clients give each stick a look, then size it by the stick's
/// run, then check it, failing once a look is too wide.
constexpr std::string_view kProgram = R"(module shown

import rawframe.canvas

struct Stick {
    run: f32
}

fn dress(count: i32, sticks: [Stick], looks: [canvas.Sprite]) {
    let i = 0
    while i < count {
        looks[i].texture = u64(7)
        i = i + 1
    }
}

fn size(count: i32, sticks: [Stick], looks: [canvas.Sprite]) {
    let i = 0
    while i < count {
        looks[i].width = looks[i].width + sticks[i].run
        i = i + 1
    }
}

fn check(count: i32, looks: [canvas.Sprite]) {
    let i = 0
    while i < count {
        looks[i].height = 1.0
        // Past ten meters it never ends, and runs out of fuel.
        while looks[i].width > 10.0 {
            looks[i].height = looks[i].height + 1.0
        }
        i = i + 1
    }
}
)";

constexpr std::string_view kGame = "program shown.kest\n"
                                   "component 51a3c0de-1111-4a2b-8c3d-4e5f60718293 shown.stick Stick\n"
                                   "component 51a3c0de-2222-4a2b-8c3d-4e5f60718293 shown.look rawframe.canvas.Sprite\n"
                                   "presentation shown.look on shown.stick\n"
                                   "present shown.dress dress read shown.stick write shown.look\n"
                                   "present shown.size size read shown.stick write shown.look\n"
                                   "present shown.check check write shown.look\n";

struct Stick {
    float run = 0;
};

struct Sprite {
    std::uint64_t texture = 0;
    std::array<float, 8> placed{};
    std::uint32_t color = 0;
    std::int32_t layer = 0;
    std::uint32_t frame = 0;
    std::uint32_t columns = 0;
};

} // namespace

RAWFRAME_TEST(AClientPresentsTheWorldItMirrors) {
    const auto kFiles =
        GameFiles::fromHeld("shown.game", {{"shown.game", std::string{kGame}}, {"shown.kest", std::string{kProgram}}});
    RAWFRAME_EXPECT(kFiles.has_value());
    if (!kFiles.has_value()) {
        return;
    }
    std::string report;
    const auto kCompiled = kFiles->compile("shown.kest", {}, &report);
    RAWFRAME_EXPECT(kCompiled.has_value());
    if (!kCompiled.has_value()) {
        std::fprintf(stderr, "%s\n", report.c_str());
        return;
    }
    const std::vector<schema::ComponentDescriptor> kDescriptors = {
        {.id = kStickId, .name = "shown.stick", .size = sizeof(Stick), .alignment = alignof(Stick), .plainData = true},
        {.id = kLookId, .name = "shown.look", .size = sizeof(Sprite), .alignment = alignof(Sprite), .plainData = true}};
    auto presentation = ClientPresentation::create(
        PresentationSettings{.program = *kCompiled,
                             .game = &kFiles->description(),
                             .descriptors = kDescriptors,
                             .limits = {.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 100'000}});
    RAWFRAME_EXPECT(presentation.has_value() && *presentation != nullptr);
    if (!presentation.has_value() || *presentation == nullptr) {
        return;
    }
    schema::RegistryBuilder builder;
    for (const schema::ComponentDescriptor& descriptor : kDescriptors) {
        builder.add(descriptor);
    }
    const auto kRegistry = *builder.freeze();
    world::World mirror{kRegistry};
    const auto kStick = *kRegistry->find(kStickId);
    const auto kLook = *kRegistry->find(kLookId);
    const auto kSpawn = [&](float run) {
        const world::EntityHandle kEntity = *mirror.create();
        Stick stick{.run = run};
        RAWFRAME_EXPECT(mirror.insertErased(kEntity, kStick, &stick).has_value());
        return kEntity;
    };
    const auto kLookOf = [&](world::EntityHandle entity) {
        return static_cast<const Sprite*>(mirror.getErased(entity, kLook));
    };
    const world::EntityHandle kRunner = kSpawn(2.0F);
    const world::EntityHandle kLevel = *mirror.create();
    // The look attached, zeroed, to what has a stick, then dressed and sized
    // in line order, and checked.
    RAWFRAME_EXPECT((*presentation)->present(mirror, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(kLookOf(kRunner) != nullptr && kLookOf(kRunner)->texture == 7 &&
                    kLookOf(kRunner)->placed[4] == 2.0F && kLookOf(kRunner)->placed[5] == 1.0F &&
                    kLookOf(kLevel) == nullptr);
    // A runner that joins gets its look the next tick; nothing is attached
    // twice.
    const world::EntityHandle kLate = kSpawn(1.0F);
    RAWFRAME_EXPECT((*presentation)->present(mirror, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(kLookOf(kLate) != nullptr && kLookOf(kLate)->placed[4] == 1.0F &&
                    kLookOf(kRunner)->placed[4] == 4.0F && (*presentation)->statistics().attached == 2);
    // Past ten meters wide, the check runs out of fuel: its writes are
    // discarded, the others' stand.
    for (int tick = 0; tick < 4; ++tick) {
        RAWFRAME_EXPECT((*presentation)->present(mirror, *world::TickRate::of(60)).has_value());
    }
    RAWFRAME_EXPECT(kLookOf(kRunner)->placed[4] == 12.0F && kLookOf(kRunner)->placed[5] == 1.0F &&
                    (*presentation)->statistics().systemsFailed >= 1);
    // A new mirror (a client that made its World again) is bound afresh.
    world::World again{kRegistry};
    const world::EntityHandle kAgain = *again.create();
    Stick stick{.run = 1.0F};
    RAWFRAME_EXPECT(again.insertErased(kAgain, kStick, &stick).has_value());
    RAWFRAME_EXPECT((*presentation)->present(again, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT((*presentation)->statistics().bound == 2 &&
                    static_cast<const Sprite*>(again.getErased(kAgain, kLook))->texture == 7);
    // A game that presents nothing has no presentation.
    const auto kPlain = GameFiles::fromHeld(
        "plain.game", {{"plain.game", "program shown.kest\n"}, {"shown.kest", std::string{kProgram}}});
    RAWFRAME_EXPECT(kPlain.has_value());
    if (kPlain.has_value()) {
        const auto kNone = ClientPresentation::create(
            PresentationSettings{.program = *kCompiled, .game = &kPlain->description(), .descriptors = kDescriptors});
        RAWFRAME_EXPECT(kNone.has_value() && *kNone == nullptr);
    }
}
