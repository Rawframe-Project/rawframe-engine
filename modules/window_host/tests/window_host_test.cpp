// A Host driven from a window's frames: a frame whose iteration is slow
// runs that one and returns, so the input that came meanwhile is read
// before the next, instead of after several more. An editor's file
// dialog is answered with what the window chose: the paths, whether
// chosen files were left out, and whether the dialog failed.

#include "rawframe/base/platform.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/execution/time.h"
#include "rawframe/host/host.h"
#include "rawframe/test/test.h"
#include "rawframe/view/dialogs.h"
#include "rawframe/window/testing.h"
#include "rawframe/window_host/window_host.h"

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if RAWFRAME_THREADS
#include <thread>
#endif

using namespace rawframe;

namespace {

/// The WindowHost's frames, counted; the second comes `late`, as a
/// platform's frame held up comes.
struct Counted final : window::Program {
    window_host::WindowHost& host;
    std::chrono::milliseconds late{};
    int frames = 0;

    explicit Counted(window_host::WindowHost& player) : host(player) {
    }

    result::Status start(window::Windows& windows) override {
        return host.start(windows);
    }
    window::FrameOutcome frame(window::Windows& windows) override {
#if RAWFRAME_THREADS
        if (++frames == 2) {
            std::this_thread::sleep_for(late);
        }
#else
        ++frames;
#endif
        return host.frame(windows);
    }
    void stop(window::Windows& windows, const result::Status& status) override {
        host.stop(windows, status);
    }
};

// A shipping build refuses `host.drawing_ms`, which every use sets.
#if !RAWFRAME_SHIPPING
/// The frames a client of `text`'s configuration took to end.
std::optional<int> framesOf(std::string_view text, std::chrono::milliseconds late) {
    auto configuration = composition::Configuration::parse(text);
    RAWFRAME_EXPECT(configuration.has_value());
    if (!configuration.has_value()) {
        return std::nullopt;
    }
    window_host::WindowHost player{
        host::HostRequest{.role = composition::TargetRole::Client, .configuration = &*configuration}};
    Counted counted{player};
    counted.late = late;
    RAWFRAME_EXPECT(window::testing::run(counted, window::RunSettings{}).has_value());
    RAWFRAME_EXPECT(player.exit() == std::optional{host::HostExit::Stopped});
    return counted.frames;
}
#endif

/// The answers the editor below was given, oldest first.
std::vector<view::FileDialogs::Answer> answers;

/// An editor that asks for one file dialog as it first runs, and keeps
/// every answer.
class Asking final : public composition::Participant {
public:
    result::Status start(composition::ParticipantContext& context) noexcept override {
        RAWFRAME_TRY_ASSIGN(dialogs_, context.capability(view::kFileDialogs));
        return {};
    }
    void stop() noexcept override {
    }
    void runHostPhase(composition::HostPhase /*phase*/, const composition::HostFrame& /*frame*/) noexcept override {
        if (!asked_) {
            asked_ = true;
            dialogs_->ask(view::FileDialogs::Asked{.ticket = 7, .kind = view::FileDialogs::Kind::OpenMany});
        }
        while (std::optional<view::FileDialogs::Answer> answer = dialogs_->take()) {
            answers.push_back(std::move(*answer));
        }
    }

private:
    view::FileDialogs* dialogs_ = nullptr;
    bool asked_ = false;
};

result::Result<composition::ParticipantOwner> makeAsking(composition::ParticipantContext&) noexcept {
    return composition::ParticipantOwner{new Asking};
}

constexpr std::array<std::string_view, 1> kDialogs = {view::kFileDialogs.name};

void registerAsking(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "test.asking",
        .factory = &makeAsking,
        .scope = composition::LifetimeScope::Runtime,
        .requiredCapabilities = kDialogs,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::RunWorlds),
    });
}

const std::array<composition::RegistrarEntry, 1> kAsking = {composition::RegistrarEntry{
    "window_host_test", &registerAsking, composition::scopeBit(composition::LifetimeScope::Runtime)}};

/// The window host, the test platform choosing `files` for its dialogs.
struct Choosing final : window::Program {
    window_host::WindowHost& host;
    std::string_view files;

    Choosing(window_host::WindowHost& player, std::string_view chosen) : host(player), files(chosen) {
    }

    result::Status start(window::Windows& windows) override {
        RAWFRAME_TRY(window::testing::chooseFiles(windows, files));
        return host.start(windows);
    }
    window::FrameOutcome frame(window::Windows& windows) override {
        return host.frame(windows);
    }
    void stop(window::Windows& windows, const result::Status& status) override {
        host.stop(windows, status);
    }
};

/// The answers an editor's one dialog got, the platform choosing `files`.
std::vector<view::FileDialogs::Answer> answersOf(std::string_view files) {
    answers.clear();
    auto configuration = composition::Configuration::parse("host.maximum_iterations = 30\n");
    RAWFRAME_EXPECT(configuration.has_value());
    if (!configuration.has_value()) {
        return {};
    }
    window_host::WindowHost player{host::HostRequest{
        .role = composition::TargetRole::Client, .registrars = kAsking, .configuration = &*configuration}};
    Choosing choosing{player, files};
    RAWFRAME_EXPECT(window::testing::run(choosing, window::RunSettings{}).has_value());
    return std::exchange(answers, {});
}

} // namespace

RAWFRAME_TEST(a_dialog_answers_its_paths_and_what_it_left_out) {
    using namespace std::string_view_literals;
    const auto kWhole = answersOf("/tmp/a.png\0/tmp/b c.txt\0"sv);
    RAWFRAME_EXPECT(kWhole.size() == 1 && kWhole[0].ticket == 7 &&
                    kWhole[0].paths == (std::vector<std::string>{"/tmp/a.png", "/tmp/b c.txt"}) &&
                    !kWhole[0].truncated && !kWhole[0].failed && !kWhole[0].unsupported);
    // A file the platform cannot give as a path, here not UTF-8, left out.
    const auto kPartial = answersOf("/tmp/a.png\0/tmp/\xff.png\0"sv);
    RAWFRAME_EXPECT(kPartial.size() == 1 && kPartial[0].paths == std::vector<std::string>{"/tmp/a.png"} &&
                    kPartial[0].truncated && !kPartial[0].failed);
    // No file it could give: the dialog failed, which is no closing of it.
    const auto kNone = answersOf("/tmp/\xff.png\0"sv);
    RAWFRAME_EXPECT(kNone.size() == 1 && kNone[0].paths.empty() && kNone[0].failed && !kNone[0].unsupported);
}

// A shipping build refuses `host.drawing_ms`.
#if !RAWFRAME_SHIPPING
RAWFRAME_TEST(slow_iterations_one_a_frame) {
    // Each iteration holds 50 ms before it presents, longer than a frame
    // catches up for and than the Host's period, so each is due again as
    // it ends. Eight iterations take eight frames, not two of four.
    const auto kFrames = framesOf("host.drawing_ms = 50\nhost.maximum_iterations = 8\n", {});
    RAWFRAME_EXPECT(kFrames.has_value() && *kFrames >= 8);
}

// Without threads nothing holds an iteration, and every one is quick.
#if RAWFRAME_THREADS
RAWFRAME_TEST(slow_iterations_due_together_one_a_frame) {
    // Two iterations a second, each holding 50 ms. The first runs in the
    // first frame, the second frame begins 1.2 s after the Host started,
    // 0.7 s behind: more than a period, so the second and third
    // iterations are both due as it begins, and too little for the Host to
    // give up catching up (more than 0.95 s). The frame runs the second
    // only, which took longer than a frame catches up for, and the third,
    // which finds the bound of two reached, falls to a third frame.
    const auto kFrames = framesOf("host.iteration_rate = 2\nhost.drawing_ms = 50\nhost.maximum_iterations = 2\n",
                                  std::chrono::milliseconds{1150});
    RAWFRAME_EXPECT(kFrames.has_value() && *kFrames >= 3);
}
#endif
#endif
