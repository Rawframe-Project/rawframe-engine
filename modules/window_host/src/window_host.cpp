#include "rawframe/window_host/window_host.h"

#include "rawframe/base/platform.h"
#include "rawframe/input_kest/sources.h"

#include <algorithm>
#include <chrono>
#include <utility>

#if RAWFRAME_THREADS
#include <thread>
#endif

namespace rawframe::window_host {

namespace {

// The most iterations one frame runs to catch up: a window hidden for a
// while is not replayed in one frame.
constexpr int kMostIterationsPerFrame = 4;
// The longest a desktop frame waits for the Host, since nothing else paces
// a window that draws nothing: input waits no longer than this to be seen.
// A page's frames are paced by the browser and never wait.
[[maybe_unused]] constexpr auto kLongestWait = std::chrono::milliseconds{4};

} // namespace

WindowHost::WindowHost(const host::HostRequest& request, WindowHostSettings settings)
    : request_(request), settings_(std::move(settings)) {
    lent_.push_back(composition::LentCapability{input_kest::kFeed.name, composition::provideAs(feed_)});
    lent_.push_back(composition::LentCapability{window::kSurfaces.name, composition::provideAs(surfaces_)});
    lent_.insert(lent_.end(), settings_.lent.begin(), settings_.lent.end());
    request_.lent = lent_;
}

result::Status WindowHost::start(window::Windows& windows) {
    RAWFRAME_TRY_ASSIGN(const window::WindowId kWindow,
                        windows.create(window::WindowSettings{.title = settings_.title}));
    surfaces_.watch(kWindow);
    bridge_.emplace(feed_);
    host_ = std::make_unique<host::Host>(request_);
    return {};
}

window::FrameOutcome WindowHost::frame(window::Windows& windows) {
    bool closing = false;
    while (std::optional<window::Event> event = windows.next()) {
        closing = closing || event->kind == window::EventKind::CloseRequested;
        bridge_->take(*event);
    }
    if (closing) {
        return end();
    }
    surfaces_.update(windows);
    for (int ran = 0; ran < kMostIterationsPerFrame && clock_.now() >= host_->due(); ++ran) {
        if (!host_->iterate()) {
            return end();
        }
    }
    // What the iterations asked the player's gamepads to feel.
    bridge_->feel(windows);
#if RAWFRAME_THREADS
    const execution::MonotonicDuration kUntilDue = host_->due() - clock_.now();
    const auto kWait =
        std::min<std::chrono::nanoseconds>(std::chrono::nanoseconds{kUntilDue.nanoseconds}, kLongestWait);
    if (kWait.count() > 0) {
        std::this_thread::sleep_for(kWait);
    }
#endif
    return window::FrameOutcome::Continue;
}

void WindowHost::stop(window::Windows& /*windows*/, const result::Status& /*status*/) {
    if (host_ != nullptr && !exit_.has_value()) {
        exit_ = host_->stop();
    }
}

window::FrameOutcome WindowHost::end() {
    exit_ = host_->stop();
    return window::FrameOutcome::Stop;
}

} // namespace rawframe::window_host
