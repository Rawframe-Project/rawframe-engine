#include "rawframe/world_audio/sound_loader.h"

#include "rawframe/audio/decode.h"
#include "rawframe/world_audio/errors.h"

#include <algorithm>
#include <iterator>

namespace rawframe::world_audio {

namespace {

constexpr content::ResourceTypeId kClipType{audio::kSoundClipType};

/// A preloaded variant: the cooked bytes decoded into a clip.
result::Result<assets::DecodedForm> decodeClip(const content::VerifiedContent& content) {
    RAWFRAME_TRY_ASSIGN(audio::Clip clip, audio::decodeCooked(content.bytes()));
    const std::uint64_t kBytes = clip.samples.size() * sizeof(float);
    return assets::DecodedForm{.value = std::make_shared<const audio::Clip>(std::move(clip)), .bytes = kBytes};
}

/// A streamed variant: the cooked bytes as they are, decoded as they play.
result::Result<assets::DecodedForm> keepCooked(const content::VerifiedContent& content) {
    return assets::DecodedForm{.value = content.shared(), .bytes = content.bytes().size()};
}

struct Wanted {
    std::size_t sound = 0;
    std::size_t variant = 0;
    bool streamed = false;
    assets::RequesterId requester;
    /// On demand: whether it was reported arrived or failed.
    bool reported = false;
};

} // namespace

std::vector<content::AdmittedRepresentation> soundRepresentations() {
    return {{.type = kClipType, .representation = *content::RepresentationId::parse("rawframe.audio.wave")},
            {.type = kClipType, .representation = *content::RepresentationId::parse("rawframe.audio.opus")}};
}

struct SoundLoader::State {
    std::vector<std::pair<std::uint64_t, audio::SoundDeclaration>> declared;
    std::unique_ptr<assets::AssetSet> clips;
    std::unique_ptr<assets::AssetSet> cooked;
    /// Every variant asked for at creation, in declaration order.
    std::vector<Wanted> wanted;
    /// Every on-demand variant asked for since, in the order asked.
    std::vector<Wanted> demanded;
    std::vector<bool> askedFor;
    /// On-demand sounds that failed, each reported once.
    std::vector<bool> failed;
};

SoundLoader::SoundLoader(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

SoundLoader::~SoundLoader() = default;

result::Result<std::unique_ptr<SoundLoader>>
SoundLoader::create(content::ContentStore& store,
                    execution::Executor& cpu,
                    execution::OwnerId owner,
                    execution::CancellationScope& parent,
                    const execution::MonotonicSource& clock,
                    std::vector<std::pair<std::uint64_t, audio::SoundDeclaration>> declared) {
    auto state = std::make_unique<State>();
    RAWFRAME_TRY_ASSIGN(
        state->clips,
        assets::AssetSet::create(store, cpu, owner, parent, clock, {.type = kClipType, .decode = &decodeClip}));
    RAWFRAME_TRY_ASSIGN(
        state->cooked,
        assets::AssetSet::create(store, cpu, owner, parent, clock, {.type = kClipType, .decode = &keepCooked}));
    for (std::size_t sound = 0; sound < declared.size(); ++sound) {
        const audio::SoundDeclaration& declaration = declared[sound].second;
        if (declaration.loading == audio::Loading::OnDemand) {
            continue;
        }
        const bool kStreamed = declaration.loading == audio::Loading::Stream;
        for (std::size_t variant = 0; variant < declaration.variants.size(); ++variant) {
            const content::ResourceRef kReference{.id = content::ResourceId{declaration.variants[variant].resource},
                                                  .type = kClipType};
            RAWFRAME_TRY_ASSIGN(const assets::RequesterId kRequester,
                                (kStreamed ? state->cooked : state->clips)->request(kReference));
            state->wanted.push_back(
                Wanted{.sound = sound, .variant = variant, .streamed = kStreamed, .requester = kRequester});
        }
    }
    state->askedFor.assign(declared.size(), false);
    state->failed.assign(declared.size(), false);
    state->declared = std::move(declared);
    return std::unique_ptr<SoundLoader>{new SoundLoader{std::move(state)}};
}

result::Result<bool> SoundLoader::update(std::uint64_t tick) {
    State& state = *state_;
    state.clips->update(tick);
    state.cooked->update(tick);
    bool ready = true;
    for (const Wanted& wanted : state.wanted) {
        const assets::AssetSet& set = wanted.streamed ? *state.cooked : *state.clips;
        const assets::Readiness kReadiness = set.readiness(wanted.requester);
        if (kReadiness == assets::Readiness::Failed) {
            return std::unexpected<result::Error>{set.failure(wanted.requester)->clone()};
        }
        ready = ready && kReadiness == assets::Readiness::Ready;
    }
    return ready;
}

result::Result<std::vector<std::pair<std::uint64_t, audio::LoadedSound>>> SoundLoader::sounds(std::uint64_t tick) {
    State& state = *state_;
    std::vector<std::pair<std::uint64_t, audio::LoadedSound>> made;
    for (const auto& [kId, kDeclaration] : state.declared) {
        audio::LoadedSound sound{.declaration = kDeclaration, .clips = {}, .cooked = {}};
        if (kDeclaration.loading == audio::Loading::OnDemand) {
            sound.clips.resize(kDeclaration.variants.size());
        }
        made.emplace_back(kId, std::move(sound));
    }
    for (const Wanted& wanted : state.wanted) {
        audio::LoadedSound& sound = made[wanted.sound].second;
        const assets::AssetSet& set = wanted.streamed ? *state.cooked : *state.clips;
        const std::optional<assets::AssetHandle> kHandle = set.handle(wanted.requester);
        if (!kHandle.has_value()) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                               kWorldAudioDomain,
                                                               code(WorldAudioError::NoAudio),
                                                               "a sound's variants are not ready")
                                                      .error()};
        }
        if (wanted.streamed) {
            RAWFRAME_TRY_ASSIGN(auto bytes,
                                assets::Assets<std::vector<std::byte>>{*state.cooked}.share(*kHandle, tick));
            sound.cooked.push_back(std::move(bytes));
        } else {
            RAWFRAME_TRY_ASSIGN(auto clip, assets::Assets<audio::Clip>{*state.clips}.share(*kHandle, tick));
            sound.clips.push_back(std::move(clip));
        }
    }
    return made;
}

result::Status SoundLoader::demand(std::size_t sound) {
    State& state = *state_;
    if (sound >= state.declared.size() || state.declared[sound].second.loading != audio::Loading::OnDemand) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           kWorldAudioDomain,
                                                           code(WorldAudioError::NoAudio),
                                                           "only an on-demand sound is asked for on demand")
                                                  .error()};
    }
    if (state.askedFor[sound]) {
        return {};
    }
    state.askedFor[sound] = true;
    const std::vector<audio::Variant>& variants = state.declared[sound].second.variants;
    for (std::size_t variant = 0; variant < variants.size(); ++variant) {
        const content::ResourceRef kReference{.id = content::ResourceId{variants[variant].resource}, .type = kClipType};
        RAWFRAME_TRY_ASSIGN(const assets::RequesterId kRequester, state.clips->request(kReference));
        state.demanded.push_back(Wanted{.sound = sound, .variant = variant, .requester = kRequester});
    }
    return {};
}

Arrivals SoundLoader::arrivals(std::uint64_t tick) {
    State& state = *state_;
    Arrivals made;
    for (Wanted& wanted : state.demanded) {
        if (wanted.reported || state.failed[wanted.sound]) {
            continue;
        }
        const assets::Readiness kReadiness = state.clips->readiness(wanted.requester);
        if (kReadiness == assets::Readiness::Failed) {
            state.failed[wanted.sound] = true;
            made.failed.emplace_back(wanted.sound, state.clips->failure(wanted.requester)->clone());
            continue;
        }
        if (kReadiness != assets::Readiness::Ready) {
            continue;
        }
        const std::optional<assets::AssetHandle> kHandle = state.clips->handle(wanted.requester);
        auto clip = assets::Assets<audio::Clip>{*state.clips}.share(*kHandle, tick);
        if (!clip.has_value()) {
            state.failed[wanted.sound] = true;
            made.failed.emplace_back(wanted.sound, std::move(clip).error());
            continue;
        }
        wanted.reported = true;
        made.arrived.push_back(Arrival{.sound = wanted.sound, .variant = wanted.variant, .clip = std::move(*clip)});
    }
    return made;
}

std::vector<std::pair<std::size_t, result::Error>> SoundLoader::serve(audio::Sounds& into, std::uint64_t tick) {
    std::vector<std::pair<std::size_t, result::Error>> unread;
    for (const std::size_t kSound : into.takeWanted()) {
        if (auto asked = demand(kSound); !asked.has_value()) {
            unread.emplace_back(kSound, std::move(asked).error());
        }
    }
    Arrivals made = arrivals(tick);
    for (Arrival& each : made.arrived) {
        if (auto supplied = into.supply(each.sound, each.variant, std::move(each.clip)); !supplied.has_value()) {
            unread.emplace_back(each.sound, std::move(supplied).error());
        }
    }
    std::ranges::move(made.failed, std::back_inserter(unread));
    return unread;
}

} // namespace rawframe::world_audio
