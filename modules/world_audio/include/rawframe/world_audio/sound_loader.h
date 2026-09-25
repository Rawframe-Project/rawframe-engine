#pragma once

// A game's sounds read by resource identity (ADR-0013, ADR-0025): every
// declared variant asked of the content store once, as a decoded clip for a
// preloaded or on-demand sound or as its cooked bytes for a streamed one,
// and held while the loader lives, so nothing a mixer plays is ever evicted
// under it. An on-demand sound's variants are asked for only when it is
// first wanted.

#include "rawframe/assets/assets.h"
#include "rawframe/audio/sounds.h"
#include "rawframe/content/store.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace rawframe::world_audio {

/// An on-demand sound's variant, decoded.
struct Arrival {
    /// The sound by its place in the declared list.
    std::size_t sound = 0;
    std::size_t variant = 0;
    std::shared_ptr<const audio::Clip> clip;
};

/// What on-demand sounds brought since last asked: the variants that
/// arrived, and the sounds that cannot be read, by their place, with why.
struct Arrivals {
    std::vector<Arrival> arrived;
    std::vector<std::pair<std::size_t, result::Error>> failed;
};

/// What one frame's serving did, each sound by its place.
struct Served {
    /// On-demand sounds that cannot be read: they go unheard.
    std::vector<std::pair<std::size_t, result::Error>> unread;
    /// Sounds with a variant a reload replaced.
    std::vector<std::size_t> reloaded;
    /// Sounds with a variant a reload could not replace: the old one
    /// plays on.
    std::vector<std::pair<std::size_t, result::Error>> notReloaded;
};

/// The two representations a cooked sound clip may have, both admitted
/// wherever sounds are read.
[[nodiscard]] std::vector<content::AdmittedRepresentation> soundRepresentations();

class SoundLoader {
public:
    /// Asks for every variant of `declared` at once but the on-demand
    /// ones, decoding on `cpu` as `owner`. Refuses what a request refuses:
    /// a variant the catalog does not hold, or holds as another type.
    [[nodiscard]] static result::Result<std::unique_ptr<SoundLoader>>
    create(content::ContentStore& store,
           execution::Executor& cpu,
           execution::OwnerId owner,
           execution::CancellationScope& parent,
           const execution::MonotonicSource& clock,
           std::vector<std::pair<std::uint64_t, audio::SoundDeclaration>> declared);

    SoundLoader(const SoundLoader&) = delete;
    SoundLoader& operator=(const SoundLoader&) = delete;
    ~SoundLoader();

    /// Once a frame: true when every variant asked for at creation is
    /// ready, or the first one's failure.
    [[nodiscard]] result::Result<bool> update(std::uint64_t tick);

    /// Once ready: each declared sound with its variants, in declaration
    /// order, an on-demand sound's clips null. Their clips and bytes stay
    /// held while this loader lives.
    [[nodiscard]] result::Result<std::vector<std::pair<std::uint64_t, audio::LoadedSound>>> sounds(std::uint64_t tick);

    /// Asks for the variants of on-demand sound `sound`, by its place in
    /// the declared list; asking again does nothing. Refused for a sound
    /// that is not on demand, and as a request is refused.
    [[nodiscard]] result::Status demand(std::size_t sound);
    /// After `update`: the on-demand variants that became ready, and the
    /// sounds that failed, each reported once.
    [[nodiscard]] Arrivals arrivals(std::uint64_t tick);
    /// After `update`, once a frame, for the sounds `sounds(tick)` made,
    /// added to `into` in declaration order: asks for the on-demand sounds
    /// it wanted since the last frame, supplies what arrived, and, when the
    /// store's catalog was replaced, supplies each variant's new revision
    /// once it is published (SPEC-0027 reload).
    [[nodiscard]] Served serve(audio::Sounds& into, std::uint64_t tick);

    struct State;

private:
    explicit SoundLoader(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::world_audio
