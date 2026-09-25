#pragma once

// Streaming cooked Opus (ADR-0038): a long sound kept encoded in memory and
// decoded a little ahead of where it plays, on an executor's worker, into a
// bounded ring the mix thread reads. The mix thread never decodes and never
// waits: a ring run dry is silence, counted.

#include "rawframe/audio/decode.h"
#include "rawframe/execution/executor.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace rawframe::audio {

struct StreamSettings {
    /// The ring's size in frames: how far ahead decoding may run.
    std::size_t bufferFrames = 48'000;
    /// Plays the sound over and over from its start, never ending.
    bool loop = false;
};

struct StreamStatistics {
    /// Frames the mix thread wanted and the ring did not have.
    std::uint64_t underrunFrames = 0;
    std::uint64_t decodedPackets = 0;
    /// Packets libopus would not decode; the stream ended at the first.
    std::uint64_t decodeErrors = 0;
};

class Stream {
public:
    /// Checks the container and finds every packet; decodes nothing yet.
    /// Refuses (`BadSound`) as `decodeCookedOpus` does, and a ring smaller
    /// than one packet's largest decode.
    [[nodiscard]] static result::Result<std::shared_ptr<Stream>>
    open(std::shared_ptr<const std::vector<std::byte>> cooked,
         const StreamSettings& settings = {},
         const DecodeLimits& limits = {});

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    ~Stream();

    [[nodiscard]] std::uint32_t channels() const noexcept;
    [[nodiscard]] std::uint32_t rate() const noexcept;
    /// Its length once through.
    [[nodiscard]] std::uint64_t frames() const noexcept;

    // The decoder: one thread at a time.

    /// Decodes packets while the ring has room for one more.
    void decodeAhead() noexcept;

    // Any thread.

    /// Whether the ring is under half full and decoding has not ended.
    [[nodiscard]] bool wantsDecoding() const noexcept;
    [[nodiscard]] StreamStatistics statistics() const noexcept;

    // The mix thread.

    /// Frames ready to play.
    [[nodiscard]] std::size_t available() const noexcept;
    /// One sample of a ready frame, `frame` frames past the next.
    [[nodiscard]] float sample(std::size_t frame, std::uint32_t channel) const noexcept;
    void consume(std::size_t frames) noexcept;
    /// Decoding has ended: no frame will be added.
    [[nodiscard]] bool ended() const noexcept;
    /// Decoding has ended and every frame was played.
    [[nodiscard]] bool finished() const noexcept;
    void countUnderrun() noexcept;

private:
    friend class Mixer;
    friend class Streamer;
    struct State;

    explicit Stream(std::unique_ptr<State> state) noexcept;
    /// One mixer voice plays a stream at a time: it has one read position.
    [[nodiscard]] bool claimPlay() noexcept;
    void releasePlay() noexcept;
    /// One decoding task at a time.
    [[nodiscard]] bool claimDecoding() noexcept;
    void releaseDecoding() noexcept;

    std::unique_ptr<State> state_;
};

/// Keeps streams decoded ahead: once a frame, on the owner's thread, it
/// hands each stream that wants decoding to the executor as one task, never
/// two at once for a stream, and forgets a stream no one else holds.
class Streamer {
public:
    /// `owner` must be admitted to `executor` with room for a task a stream.
    Streamer(execution::Executor& executor, execution::OwnerId owner) noexcept;

    void add(std::shared_ptr<Stream> stream);
    void update();

    /// Decoding the executor refused (a full queue); tried again next frame.
    [[nodiscard]] std::uint64_t refused() const noexcept {
        return refused_;
    }

private:
    execution::Executor* executor_;
    execution::OwnerId owner_;
    std::vector<std::shared_ptr<Stream>> streams_;
    std::uint64_t refused_ = 0;
};

} // namespace rawframe::audio
