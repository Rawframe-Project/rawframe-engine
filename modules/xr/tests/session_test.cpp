// A runtime's head-mounted system and a session on it (D591): the render
// module's device made by the runtime, the session begun when the runtime
// says ready, frames ended and their views located while it runs, and the
// session ended when asked. Run under tools/xr_run.sh, which starts
// Monado's service headless (its null compositor and simulated headset)
// on lavapipe; a machine with no runtime skips, unless RAWFRAME_REQUIRE_XR
// is set, as the check sets it where Monado is installed.

#include "rawframe/render/device.h"
#include "rawframe/test/test.h"
#include "rawframe/xr/errors.h"
#include "rawframe/xr/runtime.h"
#include "rawframe/xr/session.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

using namespace rawframe;

namespace {

bool required() {
    const char* value = std::getenv("RAWFRAME_REQUIRE_XR");
    return value != nullptr && value[0] != '\0';
}

/// The runtime's system, or none where no runtime answers and none is
/// required.
std::unique_ptr<xr::Runtime> opened() {
    auto runtime = xr::Runtime::open({.application = "rawframe_xr_tests"});
    if (!runtime.has_value()) {
        const bool kAbsent =
            runtime.error().domain() == xr::kXrDomain && runtime.error().code() == xr::code(xr::XrError::NoRuntime);
        RAWFRAME_EXPECT(kAbsent && !required());
        if (kAbsent) {
            std::puts("skip: no OpenXR runtime");
        }
        return nullptr;
    }
    return std::move(*runtime);
}

/// The device the runtime makes, ready.
std::unique_ptr<render::Device> deviceOf(xr::Runtime& runtime) {
    auto device = render::Device::request({.allowSoftware = true, .vulkan = &runtime});
    RAWFRAME_EXPECT(device.has_value());
    if (!device.has_value()) {
        return nullptr;
    }
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = (*device)->open();
        RAWFRAME_EXPECT(kOpen.has_value());
        if (!kOpen.has_value()) {
            return nullptr;
        }
        if (*kOpen) {
            return std::move(*device);
        }
    }
    RAWFRAME_EXPECT(false);
    return nullptr;
}

} // namespace

RAWFRAME_TEST(TheRuntimesHeadMountedSystemHasStereoViews) {
    const auto kRuntime = opened();
    if (kRuntime == nullptr) {
        return;
    }
    const xr::SystemDescription& kSystem = kRuntime->system();
    std::printf("system: %s, runtime: %s\n", kSystem.name.c_str(), kSystem.runtime.c_str());
    RAWFRAME_EXPECT(!kSystem.name.empty());
    RAWFRAME_EXPECT(kSystem.views.size() == 2);
    for (const xr::ViewSize& kView : kSystem.views) {
        RAWFRAME_EXPECT(kView.width > 0 && kView.height > 0);
    }
    RAWFRAME_EXPECT(kSystem.orientationTracking);
}

RAWFRAME_TEST(ASessionRunsOnTheDeviceItsRuntimeMadeAndEndsWhenAsked) {
    const auto kRuntime = opened();
    if (kRuntime == nullptr) {
        return;
    }
    const auto kDevice = deviceOf(*kRuntime);
    RAWFRAME_EXPECT(kDevice != nullptr);
    if (kDevice == nullptr) {
        return;
    }
    RAWFRAME_EXPECT(kDevice->vulkan().has_value());
    auto made = xr::Session::create(*kRuntime, *kDevice);
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    xr::Session& session = **made;
    // Until the session is focused and has shown frames whose views were
    // located, then asked to end, until it is over: a bound keeps a broken
    // runtime from holding the test.
    bool askedToEnd = false;
    for (int frame = 0; frame < 3000 && !session.over(); ++frame) {
        const auto kFrame = session.frame();
        RAWFRAME_EXPECT(kFrame.has_value());
        if (!kFrame.has_value()) {
            break;
        }
        if (kFrame->shown) {
            RAWFRAME_EXPECT(kFrame->views.size() == 2);
        }
        if (!askedToEnd && session.state() == xr::SessionState::Focused && session.statistics().framesLocated >= 30) {
            RAWFRAME_EXPECT(session.requestExit().has_value());
            askedToEnd = true;
        }
        if (!kFrame->ended) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    const xr::SessionStatistics& kTotals = session.statistics();
    std::printf("frames ended %llu, shown %llu, located %llu, tracked %llu\n",
                static_cast<unsigned long long>(kTotals.framesEnded),
                static_cast<unsigned long long>(kTotals.framesShown),
                static_cast<unsigned long long>(kTotals.framesLocated),
                static_cast<unsigned long long>(kTotals.framesTracked));
    RAWFRAME_EXPECT(askedToEnd);
    RAWFRAME_EXPECT(session.over());
    RAWFRAME_EXPECT(session.state() == xr::SessionState::Exiting);
    RAWFRAME_EXPECT(kTotals.framesLocated >= 30);
    // The session goes before the device it is bound to.
    made->reset();
}
