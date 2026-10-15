#pragma once

// A window's input handed between window hosts through the tooling
// protocol (D790), so an editor shows a game in its own viewport and the
// game is played there. An editor's window host lends a `Forwarding` as
// `rawframe.tooling.forwarding`: the editor says over which area of its
// window it shows the game, and the host gives it what lands there in
// place of acting on it: the pointer, presses, and the wheel over the
// area, and the keys while the area has the keyboard, from a press in it
// until a press outside. The editor sends it as `authoring.preview_input`,
// which reaches the game as `tooling.input`. The game's window host is
// given an `Injection` by the program that shows the preview, and acts on
// what it holds as its own window's input, each frame before the window's.

#include "rawframe/composition/participant.h"
#include "rawframe/world_tooling/window_input.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::world_tooling {

class Forwarding {
public:
    Forwarding() = default;
    Forwarding(const Forwarding&) = delete;
    Forwarding& operator=(const Forwarding&) = delete;

    /// The editor's side: what lands on `area` (x, y, width, height,
    /// logical pixels from the window's top left) is handed over from now
    /// on; none to stop, which lets the keyboard go.
    void forwardOver(const std::optional<std::array<float, 4>>& area) noexcept {
        area_ = area;
        if (!area_.has_value()) {
            focused_ = false;
        }
    }
    [[nodiscard]] const std::optional<std::array<float, 4>>& area() const noexcept {
        return area_;
    }

    /// Whether the area has the keyboard.
    [[nodiscard]] bool focused() const noexcept {
        return focused_;
    }

    /// The host's side: a press at `x`, `y` gives the area the keyboard if
    /// it lands in it and takes it away if not; whether it landed in it.
    bool pressed(float x, float y) noexcept {
        focused_ = over(x, y);
        return focused_;
    }
    /// Whether `x`, `y` is over the area.
    [[nodiscard]] bool over(float x, float y) const noexcept {
        return area_.has_value() && x >= (*area_)[0] && y >= (*area_)[1] && x < (*area_)[0] + (*area_)[2] &&
               y < (*area_)[1] + (*area_)[3];
    }
    /// The host's side: `event` handed over, its point already the area's.
    void give(world_tooling::WindowInput event) {
        given_.push_back(std::move(event));
    }

    /// The editor's side: what was handed over since it last took, oldest
    /// first.
    [[nodiscard]] std::vector<world_tooling::WindowInput> take() {
        return std::exchange(given_, {});
    }

private:
    std::optional<std::array<float, 4>> area_;
    bool focused_ = false;
    std::vector<world_tooling::WindowInput> given_;
};

class Injection {
public:
    Injection() = default;
    Injection(const Injection&) = delete;
    Injection& operator=(const Injection&) = delete;

    /// The tooling's side: `events`, to be the window's input next.
    void inject(const std::vector<world_tooling::WindowInput>& events) {
        held_.insert(held_.end(), events.begin(), events.end());
    }
    /// The window host's side: what to act on, oldest first.
    [[nodiscard]] std::vector<world_tooling::WindowInput> take() {
        return std::exchange(held_, {});
    }

private:
    std::vector<world_tooling::WindowInput> held_;
};

inline constexpr composition::Capability<Forwarding> kForwarding{"rawframe.tooling.forwarding"};

} // namespace rawframe::world_tooling
