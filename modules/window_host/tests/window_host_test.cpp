// A Host driven from a window's frames: a frame whose iteration is slow
// runs that one and returns, so the input that came meanwhile is read
// before the next, instead of after several more.

#include "rawframe/base/platform.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/host/host.h"
#include "rawframe/test/test.h"
#include "rawframe/window/testing.h"
#include "rawframe/window_host/window_host.h"

#include <chrono>
#include <optional>
#include <string_view>
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

} // namespace

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
