#include "rawframe/world_audio/sound_loader.h"

#include "rawframe/audio/decode.h"
#include "rawframe/world_audio/errors.h"

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
    bool streamed = false;
    assets::RequesterId requester;
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
    /// Every variant, in declaration order.
    std::vector<Wanted> wanted;
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
        const bool kStreamed = declaration.loading == audio::Loading::Stream;
        for (const audio::Variant& variant : declaration.variants) {
            const content::ResourceRef kReference{.id = content::ResourceId{variant.resource}, .type = kClipType};
            RAWFRAME_TRY_ASSIGN(const assets::RequesterId kRequester,
                                (kStreamed ? state->cooked : state->clips)->request(kReference));
            state->wanted.push_back(Wanted{.sound = sound, .streamed = kStreamed, .requester = kRequester});
        }
    }
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
        made.emplace_back(kId, audio::LoadedSound{.declaration = kDeclaration, .clips = {}, .cooked = {}});
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

} // namespace rawframe::world_audio
