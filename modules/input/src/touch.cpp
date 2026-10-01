#include "rawframe/input/touch.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace rawframe::input {

namespace {

constexpr std::array<std::string_view, 2> kButtons = {"left", "right"};
constexpr std::array<std::string_view, 2> kSticks = {"stick_left", "stick_right"};

} // namespace

TouchControls::TouchControls(Feed& feed, DeviceId device) noexcept : feed_(&feed), device_(device) {
}

void TouchControls::resize(float width) noexcept {
    if (std::isfinite(width) && width > 0) {
        width_ = width;
    }
}

void TouchControls::down(std::uint64_t touch, float x, float y) {
    if (!connected_) {
        feed_->connect(device_, DeviceClass::Touch);
        connected_ = true;
    }
    feed_->submit({.device = device_, .control = *controlNamed(DeviceClass::Touch, "pointer"), .x = x, .y = y});
    const std::size_t kHalf = width_ > 0 && x >= width_ / 2 ? 1 : 0;
    Half& half = halves_[kHalf];
    if (half.touch.has_value()) {
        return;
    }
    half = Half{.touch = touch, .x = x, .y = y};
    feed_->submit({.device = device_, .control = *controlNamed(DeviceClass::Touch, kButtons[kHalf]), .x = 1});
    stick(kHalf, x, y);
}

void TouchControls::move(std::uint64_t touch, float x, float y) {
    if (!connected_) {
        return;
    }
    feed_->submit({.device = device_, .control = *controlNamed(DeviceClass::Touch, "pointer"), .x = x, .y = y});
    for (std::size_t half = 0; half < halves_.size(); ++half) {
        if (halves_[half].touch == touch) {
            stick(half, x, y);
        }
    }
}

void TouchControls::up(std::uint64_t touch) {
    for (std::size_t half = 0; half < halves_.size(); ++half) {
        if (halves_[half].touch != touch) {
            continue;
        }
        halves_[half].touch.reset();
        feed_->submit({.device = device_, .control = *controlNamed(DeviceClass::Touch, kButtons[half]), .x = 0});
        feed_->submit({.device = device_, .control = *controlNamed(DeviceClass::Touch, kSticks[half])});
    }
}

void TouchControls::forget() noexcept {
    for (Half& half : halves_) {
        half.touch.reset();
    }
}

void TouchControls::stick(std::size_t half, float x, float y) {
    // Its way from where it began, up positive, a full tilt at the radius.
    float tiltX = (x - halves_[half].x) / kStickRadius;
    float tiltY = (halves_[half].y - y) / kStickRadius;
    const float kLength = std::hypot(tiltX, tiltY);
    if (kLength > 1) {
        tiltX /= kLength;
        tiltY /= kLength;
    }
    feed_->submit(
        {.device = device_, .control = *controlNamed(DeviceClass::Touch, kSticks[half]), .x = tiltX, .y = tiltY});
}

} // namespace rawframe::input
