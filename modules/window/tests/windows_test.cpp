// SPEC-0025's contract tests, on the headless test platform: the two-phase
// close with a refusal, requests answered after the notifications they
// caused and superseded when replaced, stale ids refused, input reset on
// focus loss, raw input and drops carried whole, gamepads about no window,
// a program whose start fails, and the seam's window side: a surface
// generation's handles given once, none while the surface is lost.

#include "rawframe/test/test.h"
#include "rawframe/window/errors.h"
#include "rawframe/window/surfaces.h"
#include "rawframe/window/testing.h"
#include "rawframe/window/windows.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::window;

namespace {

/// A program that opens one window, drains every frame's records into
/// `events`, and after frame i runs step i; it stops when the steps run
/// out. What a step posts or asks is answered in the next frame.
class Script final : public Program {
public:
    using Step = std::function<void(Windows&, Script&)>;

    explicit Script(std::vector<Step> steps) : steps_(std::move(steps)) {
    }

    result::Status start(Windows& windows) override {
        if (failStart) {
            return result::fail(
                result::ErrorClass::FailedPrecondition, kWindowDomain, code(WindowError::State), "refused");
        }
        RAWFRAME_TRY_ASSIGN(window, windows.create(WindowSettings{.title = "Rawframe"}));
        return {};
    }

    FrameOutcome frame(Windows& windows) override {
        while (std::optional<Event> event = windows.next()) {
            events.push_back(std::move(*event));
        }
        if (frames_ == steps_.size()) {
            return FrameOutcome::Stop;
        }
        steps_[frames_++](windows, *this);
        return FrameOutcome::Continue;
    }

    void stop(Windows& /*windows*/, const result::Status& status) override {
        stopped = true;
        stoppedWell = status.has_value();
    }

    [[nodiscard]] std::size_t count(EventKind kind) const {
        return static_cast<std::size_t>(std::ranges::count(events, kind, &Event::kind));
    }

    /// The position of the first record of a kind at or after `from`.
    [[nodiscard]] std::size_t find(EventKind kind, std::size_t from = 0) const {
        for (std::size_t index = from; index < events.size(); ++index) {
            if (events[index].kind == kind) {
                return index;
            }
        }
        return events.size();
    }

    bool failStart = false;
    WindowId window;
    std::vector<Event> events;
    bool stopped = false;
    bool stoppedWell = false;

private:
    std::vector<Step> steps_;
    std::size_t frames_ = 0;
};

Event report(EventKind kind, WindowId window) {
    Event event;
    event.kind = kind;
    event.window = window;
    return event;
}

bool failsWith(const result::Status& status, WindowError error) {
    return !status.has_value() && status.error().domain() == kWindowDomain && status.error().code() == code(error);
}

template <typename T> bool failsWith(const result::Result<T>& outcome, WindowError error) {
    return !outcome.has_value() && outcome.error().domain() == kWindowDomain && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(CloseIsTwoPhaseAndARefusalKeepsTheWindow) {
    bool refusedStillThere = false;
    bool staleAfter = false;
    Script script{{
        [](Windows& windows, Script& self) {
            RAWFRAME_EXPECT(testing::post(windows, report(EventKind::CloseRequested, self.window)).has_value());
        },
        // Refused: nothing closes by itself.
        [&](Windows& windows, Script& self) {
            refusedStillThere = windows.state(self.window).has_value();
            RAWFRAME_EXPECT(testing::post(windows, report(EventKind::CloseRequested, self.window)).has_value());
        },
        [](Windows& windows, Script& self) {
            RAWFRAME_EXPECT(windows.destroy(self.window).has_value());
        },
        [&](Windows& windows, Script& self) {
            staleAfter = failsWith(windows.requestTitle(self.window, "again"), WindowError::Stale) &&
                         failsWith(windows.state(self.window), WindowError::Stale) &&
                         failsWith(windows.destroy(self.window), WindowError::Stale);
        },
    }};
    RAWFRAME_EXPECT(testing::run(script, RunSettings{}).has_value());
    RAWFRAME_EXPECT(script.stopped && script.stoppedWell);
    RAWFRAME_EXPECT(script.find(EventKind::WindowCreated) < script.find(EventKind::CloseRequested));
    RAWFRAME_EXPECT(script.count(EventKind::CloseRequested) == 2);
    RAWFRAME_EXPECT(refusedStillThere && staleAfter);
    RAWFRAME_EXPECT(script.count(EventKind::WindowDestroyed) == 1 &&
                    script.find(EventKind::WindowDestroyed) > script.find(EventKind::CloseRequested));
}

RAWFRAME_TEST(ARequestIsAnsweredAfterWhatItCausedAndSupersededWhenReplaced) {
    RequestId size;
    RequestId firstTitle;
    RequestId secondTitle;
    RequestId mode;
    bool sizeSeen = false;
    Script script{{
        [&](Windows& windows, Script& self) {
            size = *windows.requestSize(self.window, {.width = 640, .height = 480});
        },
        [&](Windows& windows, Script& self) {
            sizeSeen = windows.state(self.window)->size.width == 640;
            RAWFRAME_EXPECT(testing::hold(windows, true).has_value());
            firstTitle = *windows.requestTitle(self.window, "one");
            secondTitle = *windows.requestTitle(self.window, "two");
            RAWFRAME_EXPECT(testing::answer(windows, RequestKind::Mode, Outcome::Denied).has_value());
            mode = *windows.requestMode(self.window, Mode::BorderlessFullscreen);
            RAWFRAME_EXPECT(testing::hold(windows, false).has_value());
        },
        [](Windows&, Script&) {},
    }};
    RAWFRAME_EXPECT(testing::run(script, RunSettings{}).has_value());
    RAWFRAME_EXPECT(sizeSeen);
    const auto completion = [&](RequestId request) -> std::optional<std::pair<std::size_t, Outcome>> {
        for (std::size_t index = 0; index < script.events.size(); ++index) {
            const Event& event = script.events[index];
            if (event.kind == EventKind::RequestCompleted && event.completion.request == request) {
                return std::pair{index, event.completion.outcome};
            }
        }
        return std::nullopt;
    };
    const auto sized = completion(size);
    RAWFRAME_EXPECT(sized && sized->second == Outcome::Done);
    RAWFRAME_EXPECT(sized && script.find(EventKind::Resized) < sized->first);
    const auto first = completion(firstTitle);
    const auto second = completion(secondTitle);
    const auto denied = completion(mode);
    RAWFRAME_EXPECT(first && first->second == Outcome::Superseded);
    RAWFRAME_EXPECT(second && second->second == Outcome::Done);
    RAWFRAME_EXPECT(denied && denied->second == Outcome::Denied);
    if (!sized || !first || !second || !denied) {
        // What came instead, for a platform where this fails.
        std::fprintf(stderr,
                     "  asked: size %u.%u, titles %u.%u and %u.%u, mode %u.%u\n",
                     size.index,
                     size.generation,
                     firstTitle.index,
                     firstTitle.generation,
                     secondTitle.index,
                     secondTitle.generation,
                     mode.index,
                     mode.generation);
        for (const Event& event : script.events) {
            std::fprintf(stderr,
                         "  record %u: request %u.%u kind %u outcome %u\n",
                         static_cast<unsigned>(event.kind),
                         event.completion.request.index,
                         event.completion.request.generation,
                         static_cast<unsigned>(event.completion.kind),
                         static_cast<unsigned>(event.completion.outcome));
        }
    }
    // Refused: the mode the window was created in is the only one it had.
    RAWFRAME_EXPECT(std::ranges::none_of(script.events, [](const Event& event) {
        return event.kind == EventKind::ModeChanged && event.mode != Mode::Windowed;
    }));
}

RAWFRAME_TEST(RawInputArrivesWholeAndFocusLossResetsIt) {
    Script script{{
        [](Windows& windows, Script& self) {
            Event key = report(EventKind::KeyDown, self.window);
            key.key = {.usage = 26, .meaning = 'w', .modifiers = 1, .repeat = false};
            RAWFRAME_EXPECT(testing::post(windows, key).has_value());
            Event text = report(EventKind::TextInput, self.window);
            text.text = "\xc3\xa7";
            RAWFRAME_EXPECT(testing::post(windows, text).has_value());
            Event button = report(EventKind::ButtonDown, self.window);
            button.pointer = {.position = {.x = 10, .y = 20}, .buttons = 1, .button = MouseButton::Left, .clicks = 2};
            RAWFRAME_EXPECT(testing::post(windows, button).has_value());
            Event delta = report(EventKind::RawPointerDelta, self.window);
            delta.motion = {.x = 3, .y = -4};
            RAWFRAME_EXPECT(testing::post(windows, delta).has_value());
            // Text that is not UTF-8 never enters the stream.
            Event bad = report(EventKind::TextInput, self.window);
            bad.text = "\xff";
            RAWFRAME_EXPECT(failsWith(testing::post(windows, bad), WindowError::Invalid));
            RAWFRAME_EXPECT(testing::post(windows, report(EventKind::FocusLost, self.window)).has_value());
        },
        [](Windows&, Script&) {},
    }};
    RAWFRAME_EXPECT(testing::run(script, RunSettings{}).has_value());
    const std::size_t down = script.find(EventKind::KeyDown);
    RAWFRAME_EXPECT(down < script.events.size());
    if (down < script.events.size()) {
        const Key& key = script.events[down].key;
        RAWFRAME_EXPECT(key.usage == 26 && key.meaning == 'w' && key.modifiers == 1 && !key.repeat);
    }
    const std::size_t text = script.find(EventKind::TextInput);
    RAWFRAME_EXPECT(text < script.events.size() && script.events[text].text == "\xc3\xa7");
    RAWFRAME_EXPECT(script.count(EventKind::TextInput) == 1);
    const std::size_t button = script.find(EventKind::ButtonDown);
    RAWFRAME_EXPECT(button < script.events.size() && script.events[button].pointer.button == MouseButton::Left &&
                    script.events[button].pointer.clicks == 2 && script.events[button].pointer.position.y == 20);
    const std::size_t delta = script.find(EventKind::RawPointerDelta);
    RAWFRAME_EXPECT(delta < script.events.size() && script.events[delta].motion.y == -4);
    const std::size_t lost = script.find(EventKind::FocusLost);
    RAWFRAME_EXPECT(lost < script.events.size() &&
                    script.find(EventKind::InputStateReset, lost) < script.events.size());
    RAWFRAME_EXPECT(down < text && text < button && button < lost);
}

RAWFRAME_TEST(DroppedFilesArriveAsInertPaths) {
    Script script{{
        [](Windows& windows, Script& self) {
            using namespace std::string_view_literals;
            RAWFRAME_EXPECT(
                testing::drop(windows, self.window, {.x = 5, .y = 6}, "/tmp/a.png\0/tmp/b c.txt\0"sv).has_value());
        },
        [](Windows&, Script&) {},
    }};
    RAWFRAME_EXPECT(testing::run(script, RunSettings{}).has_value());
    const std::size_t dropped = script.find(EventKind::FilesDropped);
    RAWFRAME_EXPECT(dropped < script.events.size());
    if (dropped < script.events.size()) {
        const Event& event = script.events[dropped];
        RAWFRAME_EXPECT((event.paths == std::vector<std::string>{"/tmp/a.png", "/tmp/b c.txt"}));
        RAWFRAME_EXPECT(!event.truncated && event.position.x == 5 && event.window == script.window);
    }
}

RAWFRAME_TEST(GamepadsAreAboutNoWindowAndRumble) {
    GamepadId pad;
    Script script{{
        [&](Windows& windows, Script&) {
            pad = *testing::addGamepad(windows);
            RAWFRAME_EXPECT(testing::pressGamepad(windows, pad, GamepadButton::FaceSouth, true).has_value());
            RAWFRAME_EXPECT(testing::moveGamepad(windows, pad, GamepadAxis::StickLeftX, -0.5f).has_value());
            RAWFRAME_EXPECT(testing::pressGamepad(windows, pad, GamepadButton::FaceSouth, false).has_value());
            // Its motors run as asked; the latest wins.
            RAWFRAME_EXPECT(windows.rumble(pad, 1.0f, 0.25f, 200).has_value() &&
                            windows.rumble(pad, 0.5f, 0.0f, 80).has_value());
            const auto kRumble = testing::rumbleOf(windows, pad);
            RAWFRAME_EXPECT(kRumble.has_value() && kRumble->low == 0.5f && kRumble->high == 0.0f &&
                            kRumble->milliseconds == 80 && kRumble->count == 2);
            RAWFRAME_EXPECT(!windows.rumble(pad, 2.0f, 0.0f, 80).has_value());
            RAWFRAME_EXPECT(testing::removeGamepad(windows, pad).has_value());
            RAWFRAME_EXPECT(!windows.rumble(pad, 1.0f, 0.0f, 80).has_value());
        },
        [](Windows&, Script&) {},
    }};
    RAWFRAME_EXPECT(testing::run(script, RunSettings{}).has_value());
    const std::size_t added = script.find(EventKind::GamepadAdded);
    const std::size_t down = script.find(EventKind::GamepadButtonDown);
    const std::size_t axis = script.find(EventKind::GamepadAxisMoved);
    const std::size_t up = script.find(EventKind::GamepadButtonUp);
    const std::size_t removed = script.find(EventKind::GamepadRemoved);
    RAWFRAME_EXPECT(added < down && down < axis && axis < up && up < removed && removed < script.events.size());
    if (removed < script.events.size()) {
        for (const std::size_t index : {added, down, axis, up, removed}) {
            RAWFRAME_EXPECT(script.events[index].gamepad == pad && !script.events[index].window.valid());
        }
        const GamepadInput& press = script.events[down].gamepadInput;
        RAWFRAME_EXPECT(press.control == static_cast<std::uint8_t>(GamepadButton::FaceSouth) && !press.raw &&
                        press.value == 1.0f);
        RAWFRAME_EXPECT(script.events[axis].gamepadInput.value == -0.5f &&
                        script.events[up].gamepadInput.value == 0.0f);
    }
}

RAWFRAME_TEST(AFailedStartSkipsTheFramesAndReachesStop) {
    Script script{{[](Windows&, Script&) {
        RAWFRAME_EXPECT(false);
    }}};
    script.failStart = true;
    const result::Status status = testing::run(script, RunSettings{});
    RAWFRAME_EXPECT(failsWith(status, WindowError::State));
    RAWFRAME_EXPECT(script.stopped && !script.stoppedWell && script.events.empty());
}

RAWFRAME_TEST(AZeroLimitIsRefused) {
    Script script{{}};
    RunSettings settings;
    settings.limits.windows = 0;
    RAWFRAME_EXPECT(failsWith(testing::run(script, settings), WindowError::Invalid));
    RAWFRAME_EXPECT(!script.stopped);
}

RAWFRAME_TEST(EachSurfaceGenerationsHandlesAreGivenOnce) {
    Surfaces surfaces;
    std::uint32_t first = 0;
    Script script{{
        [&](Windows& windows, Script& self) {
            surfaces.watch(self.window);
            surfaces.update(windows);
            RAWFRAME_EXPECT(surfaces.states().size() == 1);
            first = surfaces.states()[0].generation;
            RAWFRAME_EXPECT(first != 0);
            const auto kBundle = surfaces.take(self.window);
            RAWFRAME_EXPECT(kBundle.has_value() && kBundle->generation == first &&
                            std::holds_alternative<TestHandles>(kBundle->handles));
            RAWFRAME_EXPECT(!surfaces.take(self.window).has_value());
            // Updated again without a new generation, nothing more is given.
            surfaces.update(windows);
            RAWFRAME_EXPECT(!surfaces.take(self.window).has_value());
            RAWFRAME_EXPECT(testing::post(windows, report(EventKind::SurfaceLost, self.window)).has_value());
        },
        [&](Windows& windows, Script& self) {
            surfaces.update(windows);
            RAWFRAME_EXPECT(surfaces.states()[0].generation == 0 && !surfaces.take(self.window).has_value());
            RAWFRAME_EXPECT(testing::post(windows, report(EventKind::SurfaceRestored, self.window)).has_value());
        },
        [&](Windows& windows, Script& self) {
            surfaces.update(windows);
            const std::uint32_t kSecond = surfaces.states()[0].generation;
            RAWFRAME_EXPECT(kSecond != 0 && kSecond != first);
            const auto kBundle = surfaces.take(self.window);
            RAWFRAME_EXPECT(kBundle.has_value() && kBundle->generation == kSecond);
            RAWFRAME_EXPECT(windows.destroy(self.window).has_value());
        },
        [&](Windows& windows, Script& self) {
            // A window gone is no longer watched.
            surfaces.update(windows);
            RAWFRAME_EXPECT(surfaces.states().empty() && !surfaces.take(self.window).has_value());
        },
    }};
    RAWFRAME_EXPECT(testing::run(script, RunSettings{}).has_value());
    RAWFRAME_EXPECT(script.stoppedWell);
}
