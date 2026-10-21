// A client's presentation of the World it mirrors (D260): each presentation
// component attached to the entities that have its `on` component, or to
// the client's own player alone (D261), the present systems run over the
// mirror in line order, telling the client's own player from others
// (D390), a refused one changing nothing, and a new mirror bound afresh;
// and a component started as a document says (D619), and started again in
// the fields an edit changed; and a program reloaded as the game plays.

#include "../src/presentation.h"
#include "rawframe/test/test.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_kest/game_files.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_kest;

namespace {

constexpr auto kStickId = schema::ComponentTypeId::fromText("51a3c0de-1111-4a2b-8c3d-4e5f60718293");
constexpr auto kLookId = schema::ComponentTypeId::fromText("51a3c0de-2222-4a2b-8c3d-4e5f60718293");
constexpr auto kViewId = schema::ComponentTypeId::fromText("51a3c0de-3333-4a2b-8c3d-4e5f60718293");

/// A game whose clients give each stick a look, the player's its own, then
/// size it by the stick's run, then check it, failing once a look is too wide; and the player's
/// view, framed a meter further each tick.
constexpr std::string_view kProgram = R"(module shown

import rawframe.canvas
import rawframe.replication
import rawframe.world

struct Stick {
    run: f32
}

fn dress(count: i32, sticks: [Stick], looks: [canvas.Sprite], entities: [world.Entity]) {
    let me = replication.player()
    let i = 0
    while i < count {
        looks[i].texture = u64(7)
        if entities[i] == me {
            looks[i].texture = u64(8)
        }
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

fn frame(count: i32, views: [canvas.Camera]) {
    let i = 0
    while i < count {
        views[i].height = 8.0
        views[i].offsetX = views[i].offsetX + 1.0
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
                                   "component 51a3c0de-3333-4a2b-8c3d-4e5f60718293 shown.view rawframe.canvas.Camera\n"
                                   "presentation shown.look on shown.stick\n"
                                   "presentation shown.view on player\n"
                                   "present shown.frame frame write shown.view\n"
                                   "present shown.dress dress read shown.stick write shown.look entities\n"
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
    std::uint64_t material = 0;
};

struct Camera {
    float offsetX = 0;
    float offsetY = 0;
    float height = 0;
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
        {.id = kLookId, .name = "shown.look", .size = sizeof(Sprite), .alignment = alignof(Sprite), .plainData = true},
        {.id = kViewId, .name = "shown.view", .size = sizeof(Camera), .alignment = alignof(Camera), .plainData = true}};
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
    // rawframe.world's persistent identity, which a World registers itself
    // and a program importing rawframe.world may insert.
    builder.add({.id = world::Persistent::kComponentTypeId,
                 .name = world::Persistent::kComponentName,
                 .size = sizeof(world::Persistent),
                 .alignment = alignof(world::Persistent),
                 .plainData = true});
    const auto kRegistry = *builder.freeze();
    world::World mirror{kRegistry};
    const auto kStick = *kRegistry->find(kStickId);
    const auto kLook = *kRegistry->find(kLookId);
    const auto kView = *kRegistry->find(kViewId);
    const auto kSpawn = [&](float run) {
        const world::EntityHandle kEntity = *mirror.create();
        Stick stick{.run = run};
        RAWFRAME_EXPECT(mirror.insertErased(kEntity, kStick, &stick).has_value());
        return kEntity;
    };
    const auto kLookOf = [&](world::EntityHandle entity) {
        return static_cast<const Sprite*>(mirror.getErased(entity, kLook));
    };
    const auto kViewOf = [&](world::EntityHandle entity) {
        return static_cast<const Camera*>(mirror.getErased(entity, kView));
    };
    const world::EntityHandle kRunner = kSpawn(2.0F);
    const world::EntityHandle kLevel = *mirror.create();
    // The look attached, zeroed, to what has a stick, then dressed (the
    // player's its own) and sized in line order, and checked; the player's
    // view framed.
    RAWFRAME_EXPECT((*presentation)->present(mirror, kRunner, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(kLookOf(kRunner) != nullptr && kLookOf(kRunner)->texture == 8 &&
                    kLookOf(kRunner)->placed[4] == 2.0F && kLookOf(kRunner)->placed[5] == 1.0F &&
                    kLookOf(kLevel) == nullptr && kViewOf(kRunner) != nullptr && kViewOf(kRunner)->height == 8.0F &&
                    kViewOf(kRunner)->offsetX == 1.0F);
    // A runner that joins gets its look the next tick, but no view: it is
    // not this client's player. Nothing is attached twice.
    const world::EntityHandle kLate = kSpawn(1.0F);
    RAWFRAME_EXPECT((*presentation)->present(mirror, kRunner, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(kLookOf(kLate) != nullptr && kLookOf(kLate)->texture == 7 && kLookOf(kLate)->placed[4] == 1.0F &&
                    kLookOf(kRunner)->placed[4] == 4.0F && kViewOf(kLate) == nullptr &&
                    kViewOf(kRunner)->offsetX == 2.0F && (*presentation)->statistics().attached == 3);
    // Past ten meters wide, the check runs out of fuel: its writes are
    // discarded, the others' stand.
    for (int tick = 0; tick < 4; ++tick) {
        RAWFRAME_EXPECT((*presentation)->present(mirror, kRunner, {}, *world::TickRate::of(60)).has_value());
    }
    RAWFRAME_EXPECT(kLookOf(kRunner)->placed[4] == 12.0F && kLookOf(kRunner)->placed[5] == 1.0F &&
                    (*presentation)->statistics().systemsFailed >= 1);
    // A new mirror (a client that made its World again) is bound afresh;
    // before its player arrives, no view is attached, and no stick is the
    // player's.
    world::World again{kRegistry};
    const world::EntityHandle kAgain = *again.create();
    Stick stick{.run = 1.0F};
    RAWFRAME_EXPECT(again.insertErased(kAgain, kStick, &stick).has_value());
    RAWFRAME_EXPECT((*presentation)->present(again, {}, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT((*presentation)->statistics().bound == 2 &&
                    static_cast<const Sprite*>(again.getErased(kAgain, kLook))->texture == 7 &&
                    again.getErased(kAgain, kView) == nullptr);
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

namespace {

/// The view starting part framed, and the look as many bytes as no
/// component has.
class Starts final : public PresentationStarts {
public:
    std::span<const std::byte> startOf(schema::ComponentTypeId component) const noexcept override {
        if (component == kViewId) {
            return std::as_bytes(std::span{&view, 1});
        }
        return component == kLookId ? std::as_bytes(std::span{wrong}) : std::span<const std::byte>{};
    }

    std::uint64_t revision() const noexcept override {
        return edits;
    }

    Camera view{.offsetX = 0, .offsetY = 5, .height = 3};
    std::array<float, 3> wrong{1, 2, 3};
    std::uint64_t edits = 0;
};

} // namespace

RAWFRAME_TEST(APresentationComponentStartsAsItsDocumentSays) {
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
        return;
    }
    const std::vector<schema::ComponentDescriptor> kDescriptors = {
        {.id = kStickId, .name = "shown.stick", .size = sizeof(Stick), .alignment = alignof(Stick), .plainData = true},
        {.id = kLookId, .name = "shown.look", .size = sizeof(Sprite), .alignment = alignof(Sprite), .plainData = true},
        {.id = kViewId, .name = "shown.view", .size = sizeof(Camera), .alignment = alignof(Camera), .plainData = true}};
    auto presentation = ClientPresentation::create(
        PresentationSettings{.program = *kCompiled,
                             .game = &kFiles->description(),
                             .descriptors = kDescriptors,
                             .limits = {.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 100'000}});
    if (!presentation.has_value() || *presentation == nullptr) {
        RAWFRAME_EXPECT(false);
        return;
    }
    Starts starts;
    (*presentation)->startWith(&starts);
    schema::RegistryBuilder builder;
    for (const schema::ComponentDescriptor& descriptor : kDescriptors) {
        builder.add(descriptor);
    }
    builder.add({.id = world::Persistent::kComponentTypeId,
                 .name = world::Persistent::kComponentName,
                 .size = sizeof(world::Persistent),
                 .alignment = alignof(world::Persistent),
                 .plainData = true});
    const auto kRegistry = *builder.freeze();
    world::World mirror{kRegistry};
    const world::EntityHandle kRunner = *mirror.create();
    Stick stick{.run = 2.0F};
    RAWFRAME_EXPECT(mirror.insertErased(kRunner, *kRegistry->find(kStickId), &stick).has_value());
    RAWFRAME_EXPECT((*presentation)->present(mirror, kRunner, {}, *world::TickRate::of(60)).has_value());
    // The view starts as the document says and the frame writes over what
    // it writes; what it leaves stays as started.
    const auto* view = static_cast<const Camera*>(mirror.getErased(kRunner, *kRegistry->find(kViewId)));
    RAWFRAME_EXPECT(view != nullptr && view->offsetY == 5.0F && view->height == 8.0F && view->offsetX == 1.0F);
    // A start of another size is no start: the look starts at nought.
    const auto* look = static_cast<const Sprite*>(mirror.getErased(kRunner, *kRegistry->find(kLookId)));
    RAWFRAME_EXPECT(look != nullptr && look->texture == 8 && look->placed[0] == 0.0F && look->placed[4] == 2.0F);
    // The document edited while the game plays: the field whose start
    // changed and no present system writes takes its new start, the one
    // the frame writes is written again, and the one whose start stayed
    // keeps what the frame made of it.
    starts.view = Camera{.offsetX = 0, .offsetY = 7, .height = 4};
    ++starts.edits;
    RAWFRAME_EXPECT((*presentation)->present(mirror, kRunner, {}, *world::TickRate::of(60)).has_value());
    view = static_cast<const Camera*>(mirror.getErased(kRunner, *kRegistry->find(kViewId)));
    RAWFRAME_EXPECT(view != nullptr && view->offsetY == 7.0F && view->height == 8.0F && view->offsetX == 2.0F);
    RAWFRAME_EXPECT((*presentation)->statistics().restarted == 1);
    // Unedited, nothing is started again.
    RAWFRAME_EXPECT((*presentation)->present(mirror, kRunner, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT((*presentation)->statistics().restarted == 1);
}

RAWFRAME_TEST(APresentSystemSetsThePlayersVolume) {
    // The effects at -12 decibels, each tick the same.
    constexpr std::string_view kQuiet = R"(module quiet

import rawframe.canvas
import rawframe.sound

fn quiet(count: i32, views: [canvas.Camera]) {
    sound.volume(sound.EFFECTS, -12.0)
    sound.volume(u32(0), -12.0)
    sound.volume(u32(9), -12.0)
}
)";
    const auto kFiles =
        GameFiles::fromHeld("quiet.game",
                            {{"quiet.game",
                              "program quiet.kest\n"
                              "component 51a3c0de-3333-4a2b-8c3d-4e5f60718293 shown.view rawframe.canvas.Camera\n"
                              "presentation shown.view on player\n"
                              "present quiet.quiet quiet write shown.view\n"},
                             {"quiet.kest", std::string{kQuiet}}});
    RAWFRAME_EXPECT(kFiles.has_value());
    if (!kFiles.has_value()) {
        return;
    }
    std::string report;
    const auto kCompiled = kFiles->compile("quiet.kest", {}, &report);
    RAWFRAME_EXPECT(kCompiled.has_value());
    if (!kCompiled.has_value()) {
        std::fprintf(stderr, "%s\n", report.c_str());
        return;
    }
    const std::vector<schema::ComponentDescriptor> kDescriptors = {
        {.id = kViewId, .name = "shown.view", .size = sizeof(Camera), .alignment = alignof(Camera), .plainData = true}};
    auto presentation = ClientPresentation::create(
        PresentationSettings{.program = *kCompiled,
                             .game = &kFiles->description(),
                             .descriptors = kDescriptors,
                             .limits = {.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 100'000}});
    if (!presentation.has_value() || *presentation == nullptr) {
        RAWFRAME_EXPECT(false);
        return;
    }
    PresentedVolumes volumes;
    (*presentation)->setVolumesIn(&volumes);
    schema::RegistryBuilder builder;
    builder.add(kDescriptors[0]);
    const auto kRegistry = *builder.freeze();
    world::World mirror{kRegistry};
    const world::EntityHandle kPlayer = *mirror.create();
    RAWFRAME_EXPECT((*presentation)->present(mirror, kPlayer, {}, *world::TickRate::of(60)).has_value());
    // The effects' role set once; none's and one past the roles changed
    // nothing; the same again moves no revision.
    RAWFRAME_EXPECT(volumes.decibels[3] == -12.0F && volumes.decibels[0] == 0 && volumes.revision == 1);
    RAWFRAME_EXPECT((*presentation)->present(mirror, kPlayer, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(volumes.revision == 1);
}

RAWFRAME_TEST(APresentationRunsAReloadedProgram) {
    // The game's sources changed as it plays (kest.reload_every): the dress
    // gives another stick another texture from the next tick, made before
    // or after the presentation was bound; a program that changes a type
    // the mirror holds is refused, and the running one draws on.
    std::string other{kProgram};
    other.replace(other.find("u64(7)"), 6, "u64(9)");
    std::string reshaped{kProgram};
    reshaped.replace(reshaped.find("    run: f32\n"), 13, "    run: f32\n    rise: f32\n");
    const auto kFiles =
        GameFiles::fromHeld("shown.game", {{"shown.game", std::string{kGame}}, {"shown.kest", std::string{kProgram}}});
    const auto kOtherFiles =
        GameFiles::fromHeld("shown.game", {{"shown.game", std::string{kGame}}, {"shown.kest", other}});
    const auto kReshapedFiles =
        GameFiles::fromHeld("shown.game", {{"shown.game", std::string{kGame}}, {"shown.kest", reshaped}});
    RAWFRAME_EXPECT(kFiles.has_value() && kOtherFiles.has_value() && kReshapedFiles.has_value());
    if (!kFiles.has_value() || !kOtherFiles.has_value() || !kReshapedFiles.has_value()) {
        return;
    }
    const auto kCompiled = kFiles->compile("shown.kest");
    const auto kOther = kOtherFiles->compile("shown.kest");
    const auto kReshaped = kReshapedFiles->compile("shown.kest");
    RAWFRAME_EXPECT(kCompiled.has_value() && kOther.has_value() && kReshaped.has_value());
    if (!kCompiled.has_value() || !kOther.has_value() || !kReshaped.has_value()) {
        return;
    }
    const std::vector<schema::ComponentDescriptor> kDescriptors = {
        {.id = kStickId, .name = "shown.stick", .size = sizeof(Stick), .alignment = alignof(Stick), .plainData = true},
        {.id = kLookId, .name = "shown.look", .size = sizeof(Sprite), .alignment = alignof(Sprite), .plainData = true},
        {.id = kViewId, .name = "shown.view", .size = sizeof(Camera), .alignment = alignof(Camera), .plainData = true}};
    const auto kMake = [&] {
        return ClientPresentation::create(
            PresentationSettings{.program = *kCompiled,
                                 .game = &kFiles->description(),
                                 .descriptors = kDescriptors,
                                 .limits = {.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 100'000}});
    };
    auto bound = kMake();
    auto unbound = kMake();
    RAWFRAME_EXPECT(bound.has_value() && *bound != nullptr && unbound.has_value() && *unbound != nullptr);
    if (!bound.has_value() || *bound == nullptr || !unbound.has_value() || *unbound == nullptr) {
        return;
    }
    schema::RegistryBuilder builder;
    for (const schema::ComponentDescriptor& descriptor : kDescriptors) {
        builder.add(descriptor);
    }
    builder.add({.id = world::Persistent::kComponentTypeId,
                 .name = world::Persistent::kComponentName,
                 .size = sizeof(world::Persistent),
                 .alignment = alignof(world::Persistent),
                 .plainData = true});
    const auto kRegistry = *builder.freeze();
    world::World mirror{kRegistry};
    const world::EntityHandle kStick = *mirror.create();
    Stick stick{.run = 1.0F};
    RAWFRAME_EXPECT(mirror.insertErased(kStick, *kRegistry->find(kStickId), &stick).has_value());
    const auto kTexture = [&] {
        return static_cast<const Sprite*>(mirror.getErased(kStick, *kRegistry->find(kLookId)))->texture;
    };
    RAWFRAME_EXPECT((*bound)->present(mirror, {}, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(kTexture() == 7);
    RAWFRAME_EXPECT((*bound)->reload(*kOther).has_value());
    RAWFRAME_EXPECT((*bound)->present(mirror, {}, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(kTexture() == 9 && (*bound)->statistics().bound == 1);
    RAWFRAME_EXPECT(!(*bound)->reload(*kReshaped).has_value());
    RAWFRAME_EXPECT((*bound)->present(mirror, {}, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(kTexture() == 9 && (*bound)->statistics().systemsFailed == 0);
    // Reloaded before it is bound, it binds the new program.
    world::World again{kRegistry};
    const world::EntityHandle kAgain = *again.create();
    RAWFRAME_EXPECT(again.insertErased(kAgain, *kRegistry->find(kStickId), &stick).has_value());
    RAWFRAME_EXPECT((*unbound)->reload(*kOther).has_value());
    RAWFRAME_EXPECT((*unbound)->present(again, {}, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(static_cast<const Sprite*>(again.getErased(kAgain, *kRegistry->find(kLookId)))->texture == 9);
}
