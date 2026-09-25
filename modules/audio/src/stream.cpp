#include "rawframe/audio/stream.h"

#include "cooked_opus.h"
#include "rawframe/audio/errors.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <span>
#include <utility>

namespace rawframe::audio {

namespace {

/// Frames decoded and dropped before a start inside a stream: 200 ms. RFC
/// 7845's 80 ms left the first 10 ms 20 dB from the whole decode here;
/// 200 ms leaves it 55 dB away.
constexpr std::uint64_t kPreRoll = 9'600;

} // namespace

struct Stream::State {
    std::shared_ptr<const std::vector<std::byte>> bytes;
    CookedOpus cooked;
    OpusDecoderOwner decoder;
    bool loop = false;
    std::vector<float> scratch;
    /// The ring, `capacity` frames of interleaved samples; `written` and
    /// `read` count frames from the start and only grow.
    std::vector<float> ring;
    std::size_t capacity = 0;
    alignas(64) std::atomic<std::size_t> written{0};
    alignas(64) std::atomic<std::size_t> read{0};
    // The decoder's: the next packet, pre-skip left to drop, and frames
    // given to the ring this time through.
    std::size_t packet = 0;
    std::uint32_t skip = 0;
    std::uint64_t emitted = 0;
    std::atomic<bool> decodingEnded{false};
    std::atomic<bool> decoding{false};
    std::atomic<bool> playing{false};
    std::atomic<std::uint64_t> underrunFrames{0};
    std::atomic<std::uint64_t> decodedPackets{0};
    std::atomic<std::uint64_t> decodeErrors{0};
};

Stream::Stream(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Stream::~Stream() = default;

result::Result<std::shared_ptr<Stream>> Stream::open(std::shared_ptr<const std::vector<std::byte>> cooked,
                                                     const StreamSettings& settings,
                                                     const DecodeLimits& limits) {
    if (cooked == nullptr || settings.bufferFrames < static_cast<std::size_t>(kLargestPacketFrames) * 2 ||
        settings.bufferFrames > limits.maximumFrames) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kAudioDomain,
                            code(AudioError::BadSound),
                            "a stream needs cooked bytes and a ring of two packets' decode or more");
    }
    auto state = std::make_unique<State>();
    RAWFRAME_TRY_ASSIGN(state->cooked, readCookedOpus(*cooked, limits));
    RAWFRAME_TRY_ASSIGN(state->decoder, makeOpusDecoder(state->cooked.channels));
    if (settings.startFrame >= state->cooked.frames) {
        return result::fail(
            result::ErrorClass::InvalidArgument, kAudioDomain, code(AudioError::BadSound), "a start past the end");
    }
    state->bytes = std::move(cooked);
    state->loop = settings.loop;
    state->skip = state->cooked.preSkip;
    if (settings.startFrame > 0) {
        // Decoding begins a pre-roll before the start, so the decoder has
        // settled by the first frame kept.
        const std::uint64_t kTarget = settings.startFrame + state->cooked.preSkip;
        const std::uint64_t kBegin = kTarget - std::min<std::uint64_t>(kTarget, kPreRoll);
        const auto kPacket = std::ranges::upper_bound(state->cooked.packets, kBegin, {}, &CookedPacket::start) - 1;
        state->packet = static_cast<std::size_t>(kPacket - state->cooked.packets.begin());
        state->skip = static_cast<std::uint32_t>(kTarget - kPacket->start);
        state->emitted = settings.startFrame;
    }
    state->scratch.assign(static_cast<std::size_t>(kLargestPacketFrames) * state->cooked.channels, 0.0F);
    state->capacity = std::bit_ceil(settings.bufferFrames);
    state->ring.assign(state->capacity * state->cooked.channels, 0.0F);
    return std::shared_ptr<Stream>{new Stream{std::move(state)}};
}

std::uint32_t Stream::channels() const noexcept {
    return state_->cooked.channels;
}

std::uint32_t Stream::rate() const noexcept {
    return kOpusRate;
}

std::uint64_t Stream::frames() const noexcept {
    return state_->cooked.frames;
}

void Stream::decodeAhead(std::size_t packets) noexcept {
    State& state = *state_;
    const std::uint32_t kChannels = state.cooked.channels;
    for (std::size_t decoded = 0; decoded < packets && !state.decodingEnded.load(std::memory_order_relaxed);
         ++decoded) {
        const std::size_t kWritten = state.written.load(std::memory_order_relaxed);
        const std::size_t kRoom = state.capacity - (kWritten - state.read.load(std::memory_order_acquire));
        if (kRoom < static_cast<std::size_t>(kLargestPacketFrames)) {
            return;
        }
        if (state.packet == state.cooked.packets.size() || state.emitted == state.cooked.frames) {
            if (!state.loop) {
                state.decodingEnded.store(true, std::memory_order_release);
                return;
            }
            // Again from the start, exactly as the first time.
            opus_decoder_ctl(state.decoder.get(), OPUS_RESET_STATE);
            state.packet = 0;
            state.skip = state.cooked.preSkip;
            state.emitted = 0;
        }
        const CookedPacket& packet = state.cooked.packets[state.packet++];
        const int kDecoded =
            opus_decode_float(state.decoder.get(),
                              reinterpret_cast<const unsigned char*>(state.bytes->data() + packet.offset),
                              static_cast<opus_int32>(packet.length),
                              state.scratch.data(),
                              kLargestPacketFrames,
                              0);
        if (kDecoded < 0) {
            state.decodeErrors.fetch_add(1, std::memory_order_relaxed);
            state.decodingEnded.store(true, std::memory_order_release);
            return;
        }
        state.decodedPackets.fetch_add(1, std::memory_order_relaxed);
        const auto kFrames = static_cast<std::size_t>(kDecoded);
        const std::size_t kDropped = std::min<std::size_t>(state.skip, kFrames);
        state.skip -= static_cast<std::uint32_t>(kDropped);
        const std::size_t kTaken =
            std::min<std::size_t>(kFrames - kDropped, static_cast<std::size_t>(state.cooked.frames - state.emitted));
        for (std::size_t frame = 0; frame < kTaken; ++frame) {
            const std::size_t kAt = ((kWritten + frame) & (state.capacity - 1)) * kChannels;
            for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
                state.ring[kAt + channel] = state.scratch[((kDropped + frame) * kChannels) + channel];
            }
        }
        state.emitted += kTaken;
        state.written.store(kWritten + kTaken, std::memory_order_release);
    }
}

bool Stream::wantsDecoding() const noexcept {
    const State& state = *state_;
    if (state.decodingEnded.load(std::memory_order_acquire)) {
        return false;
    }
    const std::size_t kBuffered =
        state.written.load(std::memory_order_acquire) - state.read.load(std::memory_order_acquire);
    return kBuffered < state.capacity / 2;
}

StreamStatistics Stream::statistics() const noexcept {
    const State& state = *state_;
    return StreamStatistics{.underrunFrames = state.underrunFrames.load(std::memory_order_relaxed),
                            .decodedPackets = state.decodedPackets.load(std::memory_order_relaxed),
                            .decodeErrors = state.decodeErrors.load(std::memory_order_relaxed)};
}

std::size_t Stream::available() const noexcept {
    return state_->written.load(std::memory_order_acquire) - state_->read.load(std::memory_order_relaxed);
}

float Stream::sample(std::size_t frame, std::uint32_t channel) const noexcept {
    const State& state = *state_;
    const std::size_t kAt = (state.read.load(std::memory_order_relaxed) + frame) & (state.capacity - 1);
    return state.ring[(kAt * state.cooked.channels) + std::min(channel, state.cooked.channels - 1)];
}

void Stream::consume(std::size_t frames) noexcept {
    state_->read.store(state_->read.load(std::memory_order_relaxed) + frames, std::memory_order_release);
}

bool Stream::ended() const noexcept {
    return state_->decodingEnded.load(std::memory_order_acquire);
}

bool Stream::finished() const noexcept {
    // Ended first: its release makes every frame written before it visible.
    return state_->decodingEnded.load(std::memory_order_acquire) && available() == 0;
}

void Stream::countUnderrun() noexcept {
    state_->underrunFrames.fetch_add(1, std::memory_order_relaxed);
}

bool Stream::claimPlay() noexcept {
    return !state_->playing.exchange(true, std::memory_order_acq_rel);
}

void Stream::releasePlay() noexcept {
    state_->playing.store(false, std::memory_order_release);
}

bool Stream::claimDecoding() noexcept {
    return !state_->decoding.exchange(true, std::memory_order_acq_rel);
}

void Stream::releaseDecoding() noexcept {
    state_->decoding.store(false, std::memory_order_release);
}

Streamer::Streamer(execution::Executor& executor, execution::OwnerId owner) noexcept
    : executor_(&executor), owner_(owner) {
}

void Streamer::add(std::shared_ptr<Stream> stream) {
    streams_.push_back(std::move(stream));
}

void Streamer::update() {
    std::erase_if(streams_, [](const std::shared_ptr<Stream>& stream) {
        return stream.use_count() == 1;
    });
    for (const std::shared_ptr<Stream>& stream : streams_) {
        if (!stream->wantsDecoding() || !stream->claimDecoding()) {
            continue;
        }
        if (executor_ == nullptr) {
            stream->decodeAhead();
            stream->releaseDecoding();
            continue;
        }
        const result::Status kSubmitted =
            executor_->submit(owner_, execution::Priority::Normal, execution::Task{[stream]() noexcept {
                                  stream->decodeAhead();
                                  stream->releaseDecoding();
                              }});
        if (!kSubmitted.has_value()) {
            stream->releaseDecoding();
            ++refused_;
        }
    }
}

} // namespace rawframe::audio
