#pragma once

// A game's sounds read by resource identity (ADR-0013, ADR-0025): every
// declared variant asked of the content store once, as a decoded clip for a
// preloaded sound or as its cooked bytes for a streamed one, and held while
// the loader lives, so nothing a mixer plays is ever evicted under it.

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

/// The two representations a cooked sound clip may have, both admitted
/// wherever sounds are read.
[[nodiscard]] std::vector<content::AdmittedRepresentation> soundRepresentations();

class SoundLoader {
public:
    /// Asks for every variant of `declared` at once, decoding on `cpu` as
    /// `owner`. Refuses what a request refuses: a variant the catalog does
    /// not hold, or holds as another type.
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

    /// Once a frame until ready: true when every variant is, or the first
    /// variant's failure.
    [[nodiscard]] result::Result<bool> update(std::uint64_t tick);

    /// Once ready: each declared sound with its variants, in declaration
    /// order. Their clips and bytes stay held while this loader lives.
    [[nodiscard]] result::Result<std::vector<std::pair<std::uint64_t, audio::LoadedSound>>> sounds(std::uint64_t tick);

    struct State;

private:
    explicit SoundLoader(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::world_audio
