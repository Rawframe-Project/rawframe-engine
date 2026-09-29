#pragma once

// Where a host that owns no audio thread takes what a World sounds like
// (D259): a page, whose browser plays sound from its own thread and can call
// into the client only from the page's. The host lends one; the world audio
// player renders into it once a frame, as much as the frame's time holds and
// the sink has room for, and the host hands it on to the device it has.

#include "rawframe/composition/participant.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rawframe::world_audio {

class FrameSink {
public:
    FrameSink() = default;
    FrameSink(const FrameSink&) = delete;
    FrameSink& operator=(const FrameSink&) = delete;
    virtual ~FrameSink() = default;

    /// Frames a second the host plays at; the mixer renders at it.
    [[nodiscard]] virtual std::uint32_t rate() const noexcept = 0;
    /// Frames it can take now.
    [[nodiscard]] virtual std::size_t room() const noexcept = 0;
    /// Takes interleaved stereo frames, at most `room()` of them.
    virtual void write(std::span<const float> frames) noexcept = 0;
};

inline constexpr composition::Capability<FrameSink> kFrameSink{"rawframe.audio.frame_sink"};

} // namespace rawframe::world_audio
