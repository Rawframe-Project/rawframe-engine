#pragma once

// A window's input handed between window hosts through the tooling
// protocol, so an editor shows a game in its own viewport and the
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

#include <algorithm>
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

    /// The editor's side: the parts of the area it draws its own controls
    /// over, which keep what lands on them, each as the area is.
    void exceptOver(std::vector<std::array<float, 4>> holes) {
        holes_ = std::move(holes);
    }
    /// The editor's side: whether the keys are handed over only while a
    /// press handed over is held, as an editor's viewport flies its
    /// camera with them while a button is down and keeps them otherwise.
    void keysWhileHeld(bool held) noexcept {
        keysWhileHeld_ = held;
    }
    [[nodiscard]] bool keysOnlyWhileHeld() const noexcept {
        return keysWhileHeld_;
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
        const auto kIn = [x, y](const std::array<float, 4>& box) {
            return x >= box[0] && y >= box[1] && x < box[0] + box[2] && y < box[1] + box[3];
        };
        return area_.has_value() && kIn(*area_) && std::ranges::none_of(holes_, kIn);
    }
    /// The host's side: `event` handed over, its point already the area's.
    void give(WindowInput event) {
        given_.push_back(std::move(event));
    }

    /// The editor's side: what was handed over since it last took, oldest
    /// first.
    [[nodiscard]] std::vector<WindowInput> take() {
        return std::exchange(given_, {});
    }

private:
    std::optional<std::array<float, 4>> area_;
    std::vector<std::array<float, 4>> holes_;
    bool keysWhileHeld_ = false;
    bool focused_ = false;
    std::vector<WindowInput> given_;
};

class Injection {
public:
    Injection() = default;
    Injection(const Injection&) = delete;
    Injection& operator=(const Injection&) = delete;

    /// The tooling's side: `events`, to be the window's input next.
    void inject(const std::vector<WindowInput>& events) {
        held_.insert(held_.end(), events.begin(), events.end());
    }
    /// The window host's side: what to act on, oldest first.
    [[nodiscard]] std::vector<WindowInput> take() {
        return std::exchange(held_, {});
    }

private:
    std::vector<WindowInput> held_;
};

inline constexpr composition::Capability<Forwarding> kForwarding{"rawframe.tooling.forwarding"};

} // namespace rawframe::world_tooling
