#pragma once

// A local player's view's render scale as its frames keep up with the
// device (ADR-0052's render scale, D533): drawn at fewer pixels while
// frames certainly take well over what they are to, and at more again
// once they take well under it, or have kept within it a while, between
// the least and the most a client allows.

#include "rawframe/execution/time.h"

#include <array>
#include <cstdint>

namespace rawframe::render_scene {

/// The frames a judgement is made over.
inline constexpr std::uint32_t kScaleWindow = 8;
/// The windows in a row every frame of which was done within six tenths of
/// its budget before the scale grows.
inline constexpr std::uint32_t kRoomyWindows = 3;
/// The windows in a row no frame of which was over its budget before the
/// scale grows to try, at first and at most: a device that waits for the
/// display (vsync) is never seen to have room (D533). A try followed by a
/// window over doubles the wait.
inline constexpr std::uint32_t kFirstPatience = 16;
inline constexpr std::uint32_t kMostPatience = 512;
/// The hundredths a scale grows by, and shrinks by at least.
inline constexpr std::uint32_t kScaleStep = 5;

class RenderScale {
public:
    /// Between `leastPercent` and `mostPercent` hundredths of the region's
    /// pixels each way, from the most; one that never changes where the
    /// two are the same.
    RenderScale(std::uint32_t mostPercent, std::uint32_t leastPercent) noexcept;

    [[nodiscard]] std::uint32_t percent() const noexcept {
        return percent_;
    }
    [[nodiscard]] float scale() const noexcept {
        return static_cast<float>(percent_) / 100.0F;
    }
    /// Whether the scale follows the frames at all.
    [[nodiscard]] bool follows() const noexcept {
        return least_ < most_;
    }

    /// A frame done on the device: it certainly took `atLeast`, and at most
    /// `atMost`, where it was to take no longer than `budget`. True when
    /// the scale changed. More than half a window certainly over five
    /// fourths of its budget (a display's wait is never that long) shrinks
    /// the scale by about the share of pixels that brings the window's
    /// middle frame within it, a frame's time taken to go as its pixels.
    bool paced(execution::MonotonicDuration atLeast,
               execution::MonotonicDuration atMost,
               execution::MonotonicDuration budget) noexcept;

private:
    std::uint32_t most_;
    std::uint32_t least_;
    std::uint32_t percent_;
    std::array<std::int64_t, kScaleWindow> atLeast_{};
    std::uint32_t frames_ = 0;
    std::uint32_t over_ = 0;
    std::uint32_t roomy_ = 0;
    std::uint32_t roomyWindows_ = 0;
    std::uint32_t quietWindows_ = 0;
    std::uint32_t patience_ = kFirstPatience;
    /// The scale grew to try, and no window has passed since.
    bool trying_ = false;
};

} // namespace rawframe::render_scene
