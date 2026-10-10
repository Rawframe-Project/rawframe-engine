// A runtime's head-mounted system and a session on it (D591, D592): the
// render module's device made by the runtime, the session begun when the
// runtime says ready, and, while it runs, each frame's views located, the
// frame's picture placed into their images, and the views submitted; the
// session ended when asked. While it has the input focus, both hands'
// controllers are read and located (D596), and a panel is shown over the
// views, a quad layer (D597). Run under tools/xr_run.sh, which starts
// Monado's service headless (its null compositor, simulated headset, and
// simulated simple controllers) on lavapipe; a machine with no runtime
// skips, unless RAWFRAME_REQUIRE_XR is set, as the check sets it where
// Monado is installed.

#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/test/test.h"
#include "rawframe/xr/errors.h"
#include "rawframe/xr/runtime.h"
#include "rawframe/xr/session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

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

RAWFRAME_TEST(ASessionShowsPicturesOnTheDeviceItsRuntimeMadeAndEndsWhenAsked) {
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
    // A panel (D597), placed with the views and shown over them.
    auto made = xr::Session::create(*kRuntime, *kDevice, {.width = 160, .height = 90});
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    xr::Session& session = **made;
    RAWFRAME_EXPECT(session.images().size() == 2);
    RAWFRAME_EXPECT(session.panel().has_value() && session.panel()->width == 160 && session.panel()->height == 90);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(framer.has_value());
    if (!framer.has_value()) {
        return;
    }
    // The picture, the left view's size, and nothing drawn in it but its
    // black; placed into both views' images.
    const xr::ViewSize kLeft = session.images().front();
    // Until the session is focused and has submitted frames whose views
    // were located, then asked to end, until it is over: a bound keeps a
    // broken runtime from holding the test.
    bool askedToEnd = false;
    // Frames whose both hands were active, their grips located.
    int handsHeld = 0;
    for (int frame = 0; frame < 3000 && !session.over(); ++frame) {
        const auto kFrame = session.begin();
        RAWFRAME_EXPECT(kFrame.has_value());
        if (!kFrame.has_value()) {
            break;
        }
        if (!kFrame->begun) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (std::ranges::all_of(kFrame->hands, [](const xr::Hand& hand) {
                return hand.active && hand.grip.located;
            })) {
            ++handsHeld;
            // The left hand is held left of the right.
            RAWFRAME_EXPECT(kFrame->hands[0].grip.position[0] < kFrame->hands[1].grip.position[0]);
        }
        bool drawn = false;
        if (kFrame->shown) {
            RAWFRAME_EXPECT(kFrame->views.size() == 2 && kFrame->images.size() == 2 && kFrame->panel.has_value());
            std::vector<std::uint64_t> images = kFrame->images;
            images.push_back(kFrame->panel.value_or(0));
            const auto kMade = (*framer)->make(
                {},
                render::FrameTarget{
                    .width = kLeft.width, .height = kLeft.height, .images = std::move(images), .panel = 2});
            RAWFRAME_EXPECT(kMade.has_value());
            drawn = kMade.has_value() && *kMade;
        }
        RAWFRAME_EXPECT(session.end(drawn).has_value());
        if (!askedToEnd && session.state() == xr::SessionState::Focused && session.statistics().framesSubmitted >= 30 &&
            session.statistics().framesLocated >= 30 && handsHeld >= 30) {
            RAWFRAME_EXPECT(session.requestExit().has_value());
            askedToEnd = true;
        }
    }
    RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
    const xr::SessionStatistics& kTotals = session.statistics();
    const render::FramerStatistics& kDrawn = (*framer)->statistics();
    std::printf("frames ended %llu, shown %llu, located %llu, tracked %llu, submitted %llu, with the panel %llu, "
                "hands read %llu, "
                "a hand located %llu; images placed %llu, not placed %llu\n",
                static_cast<unsigned long long>(kTotals.framesEnded),
                static_cast<unsigned long long>(kTotals.framesShown),
                static_cast<unsigned long long>(kTotals.framesLocated),
                static_cast<unsigned long long>(kTotals.framesTracked),
                static_cast<unsigned long long>(kTotals.framesSubmitted),
                static_cast<unsigned long long>(kTotals.framesPanelSubmitted),
                static_cast<unsigned long long>(kTotals.framesHandsRead),
                static_cast<unsigned long long>(kTotals.framesHandLocated),
                static_cast<unsigned long long>(kDrawn.imagesPlaced),
                static_cast<unsigned long long>(kDrawn.imagesNotPlaced));
    RAWFRAME_EXPECT(askedToEnd);
    RAWFRAME_EXPECT(session.over());
    RAWFRAME_EXPECT(session.state() == xr::SessionState::Exiting);
    RAWFRAME_EXPECT(kTotals.framesLocated >= 30);
    RAWFRAME_EXPECT(kTotals.framesSubmitted >= 30);
    RAWFRAME_EXPECT(handsHeld >= 30 && kTotals.framesHandLocated >= 30);
    RAWFRAME_EXPECT(kTotals.framesPanelSubmitted == kTotals.framesSubmitted);
    RAWFRAME_EXPECT(kDrawn.imagesPlaced >= 3 * kTotals.framesSubmitted);
    // The frames, then the session, go before the device they are made on.
    framer->reset();
    made->reset();
}
