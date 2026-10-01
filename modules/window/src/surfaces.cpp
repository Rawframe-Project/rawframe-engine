#include "rawframe/window/surfaces.h"

#include <utility>

namespace rawframe::window {

std::optional<HandleBundle> Surfaces::take(WindowId window) {
    for (std::size_t at = 0; at < states_.size(); ++at) {
        if (states_[at].window == window) {
            return std::exchange(pending_[at], std::nullopt);
        }
    }
    return std::nullopt;
}

void Surfaces::watch(WindowId window) {
    for (const SurfaceState& state : states_) {
        if (state.window == window) {
            return;
        }
    }
    states_.push_back(SurfaceState{.window = window});
    pending_.emplace_back();
    read_.push_back(0);
}

void Surfaces::update(const Windows& windows) {
    std::size_t kept = 0;
    for (std::size_t at = 0; at < states_.size(); ++at) {
        const auto kState = windows.state(states_[at].window);
        if (!kState.has_value()) {
            continue;
        }
        SurfaceState state{.window = states_[at].window,
                           .generation = kState->created && !kState->surfaceLost ? kState->surfaceGeneration : 0,
                           .pixelSize = kState->pixelSize,
                           .occluded = kState->occluded || kState->mode == Mode::Minimized ||
                                       kState->pixelSize.width == 0 || kState->pixelSize.height == 0,
                           .display = windows.display(states_[at].window)};
        std::optional<HandleBundle> pending = std::move(pending_[at]);
        std::uint32_t read = read_[at];
        if (state.generation == 0) {
            pending.reset();
        } else if (state.generation != read) {
            // A new generation's handles, read once; a window that cannot
            // give them yet is asked again next frame.
            if (auto bundle = windows.handles(state.window); bundle.has_value()) {
                pending = std::move(*bundle);
                read = state.generation;
            }
        }
        states_[kept] = state;
        pending_[kept] = std::move(pending);
        read_[kept] = read;
        ++kept;
    }
    states_.resize(kept);
    pending_.resize(kept);
    read_.resize(kept);
}

} // namespace rawframe::window
