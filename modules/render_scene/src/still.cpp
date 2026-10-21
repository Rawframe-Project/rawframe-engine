#include "rawframe/render_scene/still.h"

#include <algorithm>

namespace rawframe::render_scene {

namespace {

/// A temporal resolve's frames: its jitter's sequence (temporalJitter).
constexpr std::uint64_t kTemporalFrames = 8;
/// An adapting exposure's: three seconds at sixty frames a second.
constexpr std::uint64_t kMeteringFrames = 180;

} // namespace

bool movesWithTime(const SceneFrame& frame) noexcept {
    // A trail's or a beam's points are the frame's own, compared as the
    // rest is; an emitter's particles move on the device by its clock.
    if (!frame.particles.emitters.empty() || !frame.postProcesses.empty()) {
        return true;
    }
    return std::ranges::any_of(frame.draws, [&](const SceneDraw& draw) {
        return draw.material < frame.programs.size() && frame.programs[draw.material] != nullptr;
    });
}

bool Stillness::still(const SceneFrame& frame) {
    bool same = false;
    if (last_.has_value() && (timeHeld_ || !movesWithTime(frame))) {
        // What changes with time alone is taken as the frame's, so the rest
        // is what decides.
        last_->time = frame.time;
        last_->temporal.jitter = frame.temporal.jitter;
        last_->temporal.history = frame.temporal.history;
        last_->metering.elapsed = frame.metering.elapsed;
        last_->metering.snap = frame.metering.snap;
        last_->particles.clock = frame.particles.clock;
        // An edited World's emitters and skinned poses are time's too: they
        // play on the client while the documents stand (D979).
        if (timeHeld_) {
            last_->particles = frame.particles;
            last_->palette = frame.palette;
        }
        same = *last_ == frame;
    }
    if (same) {
        ++stood_;
    } else {
        stood_ = 0;
        last_ = frame;
    }
    const std::uint64_t kNeeded = frame.metering.enabled   ? kMeteringFrames
                                  : frame.temporal.enabled ? kTemporalFrames
                                                           : 1;
    return stood_ >= kNeeded;
}

} // namespace rawframe::render_scene
