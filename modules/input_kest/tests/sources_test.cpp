// Bot input made as a player's is: a hand on the controls the sample game
// binds, its action set, and its Kest sample function, deterministic by
// seed; games without controls, or whose sample does not fit, refused; the
// player's effects felt on its gamepads (D251); the player's view read by
// its sample through `rawframe.view` (D367); and a press the UI takes read
// by the sample as its node's press code, never by an action (D421); and
// each local player's own locale chosen by their sample (D548).

#include "rawframe/input_kest/errors.h"
#include "rawframe/input_kest/sources.h"
#include "rawframe/test/files.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::input_kest;

namespace {

/// runners.Stick: run, jump, aimX, aimY, fire, pointed, targetX, targetY.
using Stick = std::array<float, 8>;

/// The sample game `path` names, its directory's files held in memory as a
/// web client holds them (D166), with `from` replaced by `to` in its
/// description, and `extra` held beside them.
const world_kest::GameFiles& gameAt(std::string_view path,
                                    std::string_view from = {},
                                    std::string_view to = {},
                                    std::vector<std::pair<std::string, std::string>> extra = {}) {
    static std::vector<std::unique_ptr<world_kest::GameFiles>> read;
    const std::size_t kSlash = path.rfind('/');
    const std::string kDirectory = std::string{RAWFRAME_SAMPLE_GAMES} + std::string{path.substr(0, kSlash + 1)};
    const std::string_view kName = path.substr(kSlash + 1);
    std::vector<std::pair<std::string, std::string>> held;
    for (std::string& name : test::filesUnder(kDirectory, "")) {
        std::string text = test::readFile(kDirectory + name);
        if (const std::size_t kAt = text.find(from); !from.empty() && name == kName && kAt != std::string::npos) {
            text.replace(kAt, from.size(), to);
        }
        held.emplace_back(std::move(name), std::move(text));
    }
    std::ranges::move(extra, std::back_inserter(held));
    auto files = world_kest::GameFiles::fromHeld(kName, std::move(held));
    RAWFRAME_EXPECT(files.has_value());
    read.push_back(
        std::make_unique<world_kest::GameFiles>(files.has_value() ? std::move(*files) : world_kest::GameFiles{}));
    return *read.back();
}

SourceSettings runners(std::size_t inputSize = sizeof(Stick)) {
    return SourceSettings{.game = &gameAt("runners/runners.game"), .inputSize = inputSize};
}

std::vector<Stick> play(world_replication::InputSource& source, int ticks) {
    std::vector<Stick> played;
    // Aligned as the lent type is; Kest refuses a buffer that is not.
    alignas(Stick) std::array<std::byte, sizeof(Stick)> input{};
    for (int tick = 1; tick <= ticks; ++tick) {
        RAWFRAME_EXPECT(source.next(static_cast<std::uint64_t>(tick), input).has_value());
        Stick stick{};
        std::memcpy(stick.data(), input.data(), sizeof stick);
        played.push_back(stick);
    }
    return played;
}

} // namespace

RAWFRAME_TEST(ABotPlaysThroughTheGamesActionsAndSample) {
    auto sources = makeInputSources(runners());
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto source = (*sources)->botSource(7);
    RAWFRAME_EXPECT(source.has_value());
    if (!source.has_value()) {
        return;
    }
    const std::vector<Stick> kPlayed = play(**source, 1200);
    std::set<float> runs;
    std::set<float> jumps;
    std::set<float> fires;
    int aimed = 0;
    for (const Stick& stick : kPlayed) {
        runs.insert(stick[0]);
        jumps.insert(stick[1]);
        fires.insert(stick[4]);
        const float kAim = std::hypot(stick[2], stick[3]);
        RAWFRAME_EXPECT(kAim <= 1.0001F);
        aimed += kAim > 0 ? 1 : 0;
    }
    // Keys and pads make whole steps: run is left, still, or right; jump and
    // fire are on or off; and every one of them happened.
    RAWFRAME_EXPECT((runs == std::set<float>{-1, 0, 1}));
    RAWFRAME_EXPECT((jumps == std::set<float>{0, 1}) && (fires == std::set<float>{0, 1}));
    RAWFRAME_EXPECT(aimed > 100);

    // The same seed plays the same; another does not.
    auto again = (*sources)->botSource(7);
    auto other = (*sources)->botSource(8);
    RAWFRAME_EXPECT(again.has_value() && other.has_value());
    if (again.has_value() && other.has_value()) {
        RAWFRAME_EXPECT(play(**again, 1200) == kPlayed);
        RAWFRAME_EXPECT(play(**other, 1200) != kPlayed);
    }
}

RAWFRAME_TEST(GamesWithoutControlsOrWithAMisfitSampleAreRefused) {
    SourceSettings crates = runners();
    crates.game = &gameAt("crates/crates.game");
    const auto kCrates = makeInputSources(crates);
    RAWFRAME_EXPECT(!kCrates.has_value() && kCrates.error().code() == code(InputKestError::NoControls) &&
                    kCrates.error().errorClass() == result::ErrorClass::NotFound);
    const auto kMisfit = makeInputSources(runners(sizeof(Stick) + 4));
    RAWFRAME_EXPECT(!kMisfit.has_value() && kMisfit.error().code() == code(InputKestError::BadSample));
}

RAWFRAME_TEST(ThePlayerPlaysFromTheLentDevices) {
    // Without devices there is no player; with them, one, whose keys reach
    // the sample the tick after they were fed, and whose release when focus
    // goes lets go of what was held.
    const auto kDeviceless = makeInputSources(runners());
    RAWFRAME_EXPECT(kDeviceless.has_value() && !(*kDeviceless)->playerSource(0).has_value());
    input::Feed feed;
    SourceSettings settings = runners();
    settings.feed = &feed;
    auto sources = makeInputSources(settings);
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto source = (*sources)->playerSource(0);
    RAWFRAME_EXPECT(source.has_value());
    const auto kSecond = (*sources)->playerSource(0);
    RAWFRAME_EXPECT(!kSecond.has_value() && kSecond.error().errorClass() == result::ErrorClass::AlreadyExists);
    if (!source.has_value()) {
        return;
    }
    constexpr input::DeviceId kKeyboard{1};
    const auto kKey = [](std::string_view name) {
        return *input::controlNamed(input::DeviceClass::Keyboard, name);
    };
    RAWFRAME_EXPECT(play(**source, 1)[0] == Stick{});
    feed.connect(kKeyboard, input::DeviceClass::Keyboard);
    feed.submit({.device = kKeyboard, .control = kKey("key_d"), .x = 1});
    feed.submit({.device = kKeyboard, .control = kKey("space"), .x = 1});
    const Stick kHeld = play(**source, 1)[0];
    RAWFRAME_EXPECT(kHeld[0] == 1.0F && kHeld[1] == 1.0F && kHeld[4] == 0.0F);
    RAWFRAME_EXPECT(play(**source, 1)[0] == kHeld);
    feed.releaseAll();
    RAWFRAME_EXPECT(play(**source, 1)[0] == Stick{});
}

RAWFRAME_TEST(LocalPlayersPlayFromTheDevicesPairedToThem) {
    // Two local players (D363), keyboard first: the keyboard's jump reaches
    // the first's sample only, the gamepad's the second's only.
    input::Feed feed;
    SourceSettings settings = runners();
    settings.feed = &feed;
    auto sources = makeInputSources(settings);
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto first = (*sources)->playerSource(0);
    auto second = (*sources)->playerSource(1);
    const auto kPast = (*sources)->playerSource(4);
    RAWFRAME_EXPECT(first.has_value() && second.has_value() && !kPast.has_value() &&
                    kPast.error().errorClass() == result::ErrorClass::InvalidArgument);
    if (!first.has_value() || !second.has_value()) {
        return;
    }
    constexpr input::DeviceId kKeyboard{1};
    constexpr input::DeviceId kPad{2};
    feed.connect(kKeyboard, input::DeviceClass::Keyboard);
    feed.connect(kPad, input::DeviceClass::Gamepad);
    feed.submit({.device = kKeyboard, .control = *input::controlNamed(input::DeviceClass::Keyboard, "space"), .x = 1});
    // The first to tick routes what the devices sent to both.
    RAWFRAME_EXPECT(play(**first, 1)[0][1] == 1.0F && play(**second, 1)[0][1] == 0.0F);
    feed.submit({.device = kPad, .control = *input::controlNamed(input::DeviceClass::Gamepad, "face_south"), .x = 1});
    RAWFRAME_EXPECT(play(**second, 1)[0][1] == 1.0F);
    feed.submit({.device = kKeyboard, .control = *input::controlNamed(input::DeviceClass::Keyboard, "space"), .x = 0});
    RAWFRAME_EXPECT(play(**first, 1)[0][1] == 0.0F && play(**second, 1)[0][1] == 1.0F);
}

RAWFRAME_TEST(ThePlayerFeelsAnEffectOnItsGamepads) {
    input::Feed feed;
    SourceSettings settings = runners();
    settings.feed = &feed;
    auto sources = makeInputSources(settings);
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    // Runners' jump (its one effect, kind nought) is felt as a thump.
    RAWFRAME_EXPECT((*sources)->feelsEffects());
    std::vector<input::HapticCommand> felt;
    RAWFRAME_EXPECT(!(*sources)->feelEffect(0).has_value());
    feed.takeFelt(felt);
    RAWFRAME_EXPECT(felt.empty());
    auto source = (*sources)->playerSource(0);
    RAWFRAME_EXPECT(source.has_value());
    if (!source.has_value()) {
        return;
    }
    constexpr input::DeviceId kKeyboard{1};
    constexpr input::DeviceId kPad{2};
    feed.connect(kKeyboard, input::DeviceClass::Keyboard);
    feed.connect(kPad, input::DeviceClass::Gamepad);
    play(**source, 1);
    RAWFRAME_EXPECT((*sources)->feelEffect(0) == 1 && !(*sources)->feelEffect(1).has_value());
    feed.takeFelt(felt);
    RAWFRAME_EXPECT(felt.size() == 1 && felt[0].device == kPad && felt[0].haptic.amplitude == 0.4F &&
                    felt[0].haptic.frequency == 60 && felt[0].haptic.milliseconds == 90);
}

RAWFRAME_TEST(AnEffectFeltByAnUndeclaredHapticIsRefused) {
    const auto kSources = makeInputSources(SourceSettings{
        .game = &gameAt("runners/runners.game", "felt thump", "felt rumble"), .inputSize = sizeof(Stick)});
    RAWFRAME_EXPECT(!kSources.has_value() && kSources.error().code() == code(InputKestError::UnknownHaptic));
}

namespace {

/// A sample that asks the player's view everything (D367) and writes the
/// answers into runners' stick: the canvas view's left (a thousand per
/// failure), the scene ray's failure, the World point under view point
/// (320, 180), and that point shown back (a thousand per failure).
constexpr std::string_view kPointed = R"(module pointed

import controls
import rawframe.view

fn sample(into: [controls.Stick]) {
    let place = view.canvasPlace()
    let world = view.pointToWorld2D(320.0, 180.0)
    let back = view.world2DToPoint(world.x, world.y)
    let ray = view.pointToRay(1.0, 1.0)
    let shown = view.worldToPoint(f64(0.0), f64(0.0), f64(-10.0))
    into[0].run = place.left + f32(place.failure) * 1000.0
    into[0].jump = f32(ray.failure) + f32(shown.failure) * 10.0 + f32(view.scenePlace().failure) * 100.0
    into[0].aimX = f32(world.x)
    into[0].aimY = f32(world.y)
    into[0].fire = back.x + f32(back.failure) * 1000.0
}
)";

} // namespace

RAWFRAME_TEST(TheSampleReadsThePlayersView) {
    input::Feed feed;
    view::PlayerViews views;
    SourceSettings settings{.game = &gameAt("runners/runners.game",
                                            "sample sample.kest sample",
                                            "sample pointed.kest sample",
                                            {{"pointed.kest", std::string{kPointed}}}),
                            .inputSize = sizeof(Stick),
                            .feed = &feed,
                            .views = &views};
    auto sources = makeInputSources(settings);
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto player = (*sources)->playerSource(0);
    auto bot = (*sources)->botSource(3);
    RAWFRAME_EXPECT(player.has_value() && bot.has_value());
    if (!player.has_value() || !bot.has_value()) {
        return;
    }
    // Before the presentation tells a view, and for a bot always: no view.
    const Stick kNone{1000, 111, 0, 0, 1000};
    RAWFRAME_EXPECT(play(**player, 1)[0] == kNone);
    // The canvas's view, the window's right half: its place, and view
    // point (320, 180) a quarter of its height above its middle, which is
    // ten meters tall; shown back where it was picked. The scene draws
    // nothing, so its verbs have no view.
    views.window({.width = 1280, .height = 720});
    views.tell(0, {.left = 0.5F, .width = 0.5F}, view::Orthographic{.middle = {10, 5}, .height = 10});
    const Stick kCanvas = play(**player, 1)[0];
    RAWFRAME_EXPECT(kCanvas[0] == 640 && kCanvas[1] == 111 && std::abs(kCanvas[2] - 10) < 1e-4F &&
                    std::abs(kCanvas[3] - 7.5F) < 1e-4F && std::abs(kCanvas[4] - 320) < 1e-3F);
    RAWFRAME_EXPECT(play(**bot, 1)[0] == kNone);
    // A scene view looking along -Z from the origin: its ray found, the
    // point ahead shown, and one behind the near plane named so.
    views.tell(0, {}, view::Perspective{.eye = {0, 0, 0}});
    RAWFRAME_EXPECT(play(**player, 1)[0][1] == 0);
    views.tell(0, {}, view::Perspective{.eye = {0, 0, -20}});
    RAWFRAME_EXPECT(play(**player, 1)[0][1] == 50);
    // A camera that sees nothing is named, never a NaN.
    views.tell(0, {}, view::Perspective{.eye = {0, 0, 0}, .fovY = 0});
    RAWFRAME_EXPECT(play(**player, 1)[0][1] == 44);
    // A window without size gives a view without size.
    views.window({});
    RAWFRAME_EXPECT(play(**player, 1)[0][4] == 2000);
}

RAWFRAME_TEST(ARunnerAimsWhereTheMousePoints) {
    // Runners' sample (D368): the pointer, less the view's place, picked
    // onto the level's plane; the aim, when pushed, wins.
    input::Feed feed;
    view::PlayerViews views;
    SourceSettings settings = runners();
    settings.feed = &feed;
    settings.views = &views;
    auto sources = makeInputSources(settings);
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto source = (*sources)->playerSource(0);
    RAWFRAME_EXPECT(source.has_value());
    if (!source.has_value()) {
        return;
    }
    constexpr input::DeviceId kKeyboard{1};
    constexpr input::DeviceId kMouse{2};
    feed.connect(kKeyboard, input::DeviceClass::Keyboard);
    feed.connect(kMouse, input::DeviceClass::Mouse);
    const input::Control kPointer = *input::controlNamed(input::DeviceClass::Mouse, "pointer");
    feed.submit({.device = kMouse, .control = kPointer, .x = 960, .y = 180});
    // No view yet: nothing pointed at.
    RAWFRAME_EXPECT(play(**source, 1)[0][5] == 0);
    views.window({.width = 1280, .height = 720});
    views.tell(0, {.left = 0.5F, .width = 0.5F}, view::Orthographic{.middle = {10, 5}, .height = 10});
    const Stick kAtPointer = play(**source, 1)[0];
    RAWFRAME_EXPECT(kAtPointer[5] == 1 && std::abs(kAtPointer[6] - 10) < 1e-4F &&
                    std::abs(kAtPointer[7] - 7.5F) < 1e-4F);
    feed.submit(
        {.device = kKeyboard, .control = *input::controlNamed(input::DeviceClass::Keyboard, "arrow_up"), .x = 1});
    const Stick kAimed = play(**source, 1)[0];
    RAWFRAME_EXPECT(kAimed[5] == 0 && kAimed[3] == 1);
}

RAWFRAME_TEST(APressTheUiTakesReachesTheSampleAndNoAction) {
    // Runners' fire is the mouse's left button; a sample that reads it, and
    // the press code of the UI node a press lands on.
    const world_kest::GameFiles* game = &gameAt("runners/runners.game",
                                                "sample sample.kest sample",
                                                "sample pressed.kest sample",
                                                {{"pressed.kest",
                                                  "module pressed\n"
                                                  "import controls\n"
                                                  "import rawframe.input\n"
                                                  "fn sample(into: [controls.Stick]) {\n"
                                                  "    into[0].fire = 0.0\n"
                                                  "    if input.on(0xd1e5c72fda17a16f) {\n"
                                                  "        into[0].fire = 1.0\n"
                                                  "    }\n"
                                                  "    into[0].targetX = f32(input.uiPressed())\n"
                                                  "}\n"}});
    input::Feed feed;
    view::UiPointing pointing;
    // A panel over the window's left 100 pixels, whose press code is 7.
    pointing.answer([](float x, float /*y*/, bool /*pressing*/) -> std::optional<std::int64_t> {
        return x < 100 ? std::optional<std::int64_t>{7} : std::nullopt;
    });
    auto sources = makeInputSources(
        SourceSettings{.game = game, .inputSize = sizeof(Stick), .feed = &feed, .pointing = &pointing});
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto source = (*sources)->playerSource(0);
    RAWFRAME_EXPECT(source.has_value());
    if (!source.has_value()) {
        return;
    }
    constexpr input::DeviceId kMouse{1};
    const input::Control kLeft = *input::controlNamed(input::DeviceClass::Mouse, "left");
    const input::Control kPointer = *input::controlNamed(input::DeviceClass::Mouse, "pointer");
    feed.connect(kMouse, input::DeviceClass::Mouse);
    const auto kPress = [&](float x, bool down) {
        feed.submit({.device = kMouse, .control = kPointer, .x = x, .y = 30});
        feed.submit({.device = kMouse, .control = kLeft, .x = down ? 1.0F : 0.0F});
        return play(**source, 1)[0];
    };
    // On the panel: the sample reads its code, and fire never sees it.
    const Stick kOnPanel = kPress(40, true);
    RAWFRAME_EXPECT(kOnPanel[4] == 0.0F && kOnPanel[6] == 7.0F);
    const Stick kLetGo = kPress(40, false);
    RAWFRAME_EXPECT(kLetGo[4] == 0.0F && kLetGo[6] == 0.0F);
    // Past it: the game's.
    const Stick kInWorld = kPress(300, true);
    RAWFRAME_EXPECT(kInWorld[4] == 1.0F && kInWorld[6] == 0.0F);
    RAWFRAME_EXPECT(kPress(300, false)[4] == 0.0F);
    // No UI answering: every press is the game's.
    pointing.answer({});
    RAWFRAME_EXPECT(kPress(40, true)[4] == 1.0F);
}

RAWFRAME_TEST(AFieldsTextReachesTheSampleAndItsKeysNoAction) {
    // A sample that reads runners' run and the text a field gave (D426):
    // its press code, its length, and its first byte.
    const world_kest::GameFiles* game = &gameAt("runners/runners.game",
                                                "sample sample.kest sample",
                                                "sample typed.kest sample",
                                                {{"typed.kest",
                                                  "module typed\n"
                                                  "import controls\n"
                                                  "import rawframe.input\n"
                                                  "fn sample(into: [controls.Stick]) {\n"
                                                  "    into[0].run = input.x(0x9182d0b16cbd7c9c)\n"
                                                  "    let given = input.uiTyped()\n"
                                                  "    into[0].targetX = f32(input.uiSubmitted())\n"
                                                  "    into[0].targetY = f32(given.length)\n"
                                                  "    into[0].aimX = f32(given.bytes[0])\n"
                                                  "}\n"}});
    input::Feed feed;
    view::UiTyping typing;
    auto sources =
        makeInputSources(SourceSettings{.game = game, .inputSize = sizeof(Stick), .feed = &feed, .typing = &typing});
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto source = (*sources)->playerSource(0);
    RAWFRAME_EXPECT(source.has_value());
    if (!source.has_value()) {
        return;
    }
    // Given once, on the tick after: code 9, "hey".
    typing.submit({.press = 9, .text = "hey"});
    const Stick kGiven = play(**source, 1)[0];
    RAWFRAME_EXPECT(kGiven[6] == 9.0F && kGiven[7] == 3.0F && kGiven[2] == static_cast<float>('h'));
    const Stick kAfter = play(**source, 1)[0];
    RAWFRAME_EXPECT(kAfter[6] == 0.0F && kAfter[7] == 0.0F);
    // While a field holds the keyboard, D moves no runner; once it lets go,
    // it does again.
    constexpr input::DeviceId kKeyboard{1};
    const input::Control kD = *input::controlNamed(input::DeviceClass::Keyboard, "key_d");
    feed.connect(kKeyboard, input::DeviceClass::Keyboard);
    typing.answer([](const view::Typing&) {});
    typing.focus(view::UiTyping::Caret{0, 0, 1, 10});
    feed.submit({.device = kKeyboard, .control = kD, .x = 1});
    RAWFRAME_EXPECT(play(**source, 1)[0][0] == 0.0F);
    typing.focus(std::nullopt);
    feed.submit({.device = kKeyboard, .control = kD, .x = 0});
    play(**source, 1);
    feed.submit({.device = kKeyboard, .control = kD, .x = 1});
    RAWFRAME_EXPECT(play(**source, 1)[0][0] == 1.0F);
}

namespace {

/// A sample that gives the player's locale as `run`, then asks for the next
/// one, round to the first.
constexpr std::string_view kSpoken = R"(module spoken

import controls
import rawframe.input

fn sample(into: [controls.Stick]) {
    into[0].run = f32(input.locale())
    input.chooseLocale((input.locale() + u32(1)) % input.locales())
}
)";

} // namespace

RAWFRAME_TEST(EachLocalPlayerChoosesTheirOwnLocale) {
    // ADR-0050's locale as per-player presentation state (D548): the first
    // player's choice leaves the second's as configured, and theirs the
    // first's.
    localization::StringTable table{.sourceLocale = *localization::parseLocale("en"), .entries = {}};
    table.entries["menu.play"] = localization::SourceEntry{.message = "Play", .description = {}};
    const base::Bits128 kHud{0, 0xa1};
    localization::Translations turkish{.table = kHud, .locale = *localization::parseLocale("tr"), .entries = {}};
    turkish.entries["menu.play"] =
        localization::TranslatedEntry{.message = "Oyna", .sourceHash = localization::sourceHashOf("Play")};
    const std::vector<localization::TableDocument> kTables{{.id = kHud, .table = table}};
    const std::vector<localization::Translations> kTranslations{turkish};
    world_localization::GameText text{*localization::Catalog::build(kTables, kTranslations),
                                      {{"hud.strings", kHud}},
                                      *localization::parseLocale("en"),
                                      *localization::parseLocale("en"),
                                      {*localization::parseLocale("en"), *localization::parseLocale("tr")}};
    input::Feed feed;
    SourceSettings settings{.game = &gameAt("runners/runners.game",
                                            "sample sample.kest sample",
                                            "sample spoken.kest sample",
                                            {{"spoken.kest", std::string{kSpoken}}}),
                            .inputSize = sizeof(Stick),
                            .feed = &feed,
                            .text = &text};
    auto sources = makeInputSources(settings);
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto first = (*sources)->playerSource(0);
    auto second = (*sources)->playerSource(1);
    RAWFRAME_EXPECT(first.has_value() && second.has_value());
    if (!first.has_value() || !second.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(play(**first, 1)[0][0] == 0.0F && text.chosen(0) == 1 && text.chosen(1) == 0);
    RAWFRAME_EXPECT(play(**second, 1)[0][0] == 0.0F && text.chosen(1) == 1 && text.chosen(0) == 1);
    RAWFRAME_EXPECT(play(**first, 1)[0][0] == 1.0F && text.chosen(0) == 0 && text.chosen(1) == 1);
    RAWFRAME_EXPECT(text.revision() == 3);
}
