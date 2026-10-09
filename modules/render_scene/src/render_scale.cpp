#include "rawframe/render_scene/render_scale.h"

#include <algorithm>
#include <cmath>

namespace rawframe::render_scene {

RenderScale::RenderScale(std::uint32_t mostPercent, std::uint32_t leastPercent) noexcept
    : most_(mostPercent), least_(std::min(leastPercent, mostPercent)), percent_(mostPercent) {
}

bool RenderScale::paced(execution::MonotonicDuration atLeast,
                        execution::MonotonicDuration atMost,
                        execution::MonotonicDuration budget) noexcept {
    if (!follows() || budget.nanoseconds <= 0) {
        return false;
    }
    atLeast_[frames_] = atLeast.nanoseconds;
    ++frames_;
    if (atLeast.nanoseconds * 4 > budget.nanoseconds * 5) {
        ++over_;
    }
    if (atMost.nanoseconds * 10 <= budget.nanoseconds * 6) {
        ++roomy_;
    }
    if (frames_ < kScaleWindow) {
        return false;
    }
    const std::uint32_t kWas = percent_;
    if (over_ * 2 > kScaleWindow) {
        std::ranges::nth_element(atLeast_, atLeast_.begin() + kScaleWindow / 2);
        const double kMiddle = static_cast<double>(atLeast_[kScaleWindow / 2]);
        const double kAsked =
            static_cast<double>(percent_) * std::sqrt(static_cast<double>(budget.nanoseconds) / kMiddle);
        // Down to a whole step, at least one below where it was.
        const auto kStepped = static_cast<std::uint32_t>(kAsked) / kScaleStep * kScaleStep;
        percent_ = std::max(least_, std::min(kStepped, percent_ > kScaleStep ? percent_ - kScaleStep : 0U));
        // A try that did not hold goes back where it was, and is tried
        // again only after twice the wait.
        if (trying_) {
            percent_ = kWas - kScaleStep;
            patience_ = std::min(kMostPatience, patience_ * 2);
        }
        trying_ = false;
        roomyWindows_ = 0;
        quietWindows_ = 0;
    } else {
        trying_ = false;
        roomyWindows_ = roomy_ == kScaleWindow ? roomyWindows_ + 1 : 0;
        quietWindows_ = over_ == 0 ? quietWindows_ + 1 : 0;
        if (percent_ < most_ && roomyWindows_ >= kRoomyWindows) {
            // Room seen: a later try need not wait long.
            percent_ = std::min(most_, percent_ + kScaleStep);
            patience_ = kFirstPatience;
            roomyWindows_ = 0;
            quietWindows_ = 0;
        } else if (percent_ < most_ && quietWindows_ >= patience_) {
            percent_ = std::min(most_, percent_ + kScaleStep);
            trying_ = true;
            roomyWindows_ = 0;
            quietWindows_ = 0;
        }
    }
    frames_ = 0;
    over_ = 0;
    roomy_ = 0;
    return percent_ != kWas;
}

} // namespace rawframe::render_scene
