// A Host driven from a window's frames: a frame whose iteration is slow
// runs that one and returns, so the input that came meanwhile is read
// before the next, instead of after several more.

#include "rawframe/composition/configuration.h"
#include "rawframe/host/host.h"
#include "rawframe/test/test.h"
#include "rawframe/window/testing.h"
#include "rawframe/window_host/window_host.h"

#include <optional>

using namespace rawframe;

namespace {

/// The WindowHost's frames, counted.
struct Counted final : window::Program {
    window_host::WindowHost& host;
    int frames = 0;

    explicit Counted(window_host::WindowHost& player) : host(player) {
    }

    result::Status start(window::Windows& windows) override {
        return host.start(windows);
    }
    window::FrameOutcome frame(window::Windows& windows) override {
        ++frames;
        return host.frame(windows);
    }
    void stop(window::Windows& windows, const result::Status& status) override {
        host.stop(windows, status);
    }
};

} // namespace

// A shipping build refuses `host.drawing_ms`.
#if !RAWFRAME_SHIPPING
RAWFRAME_TEST(slow_iterations_one_a_frame) {
    // Each iteration holds 50 ms before it presents, longer than a frame
    // catches up for and than the Host's period, so each is due again as
    // it ends. Eight iterations take eight frames, not two of four.
    auto configuration = composition::Configuration::parse("host.drawing_ms = 50\nhost.maximum_iterations = 8\n");
    RAWFRAME_EXPECT(configuration.has_value());
    if (!configuration.has_value()) {
        return;
    }
    window_host::WindowHost player{
        host::HostRequest{.role = composition::TargetRole::Client, .configuration = &*configuration}};
    Counted counted{player};
    RAWFRAME_EXPECT(window::testing::run(counted, window::RunSettings{}).has_value());
    RAWFRAME_EXPECT(player.exit() == std::optional{host::HostExit::Stopped});
    RAWFRAME_EXPECT(counted.frames >= 8);
}
#endif
