// A World heard: an emitter's continuous sound follows its pose and its
// playing flag, its cue plays one-shots counted from when it was first
// seen, despawn policies do what they name, two active listeners hear
// nothing, a game's audio loads from its files against its program, and its
// sounds are read from cooked content by resource identity, on-demand ones
// when first wanted.

#include "rawframe/assets/errors.h"
#include "rawframe/audio/decode.h"
#include "rawframe/audio/mixer.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/test/test.h"
#include "rawframe/world_audio/sound_loader.h"
#include "rawframe/world_audio/world_audio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <unistd.h>

using namespace rawframe;
using namespace rawframe::world_audio;

namespace {

constexpr auto kEmitterId = schema::ComponentTypeId::fromText("3c1f0a8e-5b2d-4e7a-9f64-1d8c2b7a0e53");
constexpr auto kListenerId = schema::ComponentTypeId::fromText("9e4b7c21-6d0a-4f38-b5e2-7a1c3d9f8b06");
constexpr std::uint64_t kHum = 0xa1;
constexpr std::uint64_t kClick = 0xa2;
constexpr base::Bits128 kClickClip{.high = 0, .low = 0xc1};

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(schema::ComponentDescriptor{.id = kEmitterId,
                                            .name = "test.emitter",
                                            .size = sizeof(Emitter),
                                            .alignment = alignof(Emitter),
                                            .plainData = true});
    builder.add(schema::ComponentDescriptor{.id = kListenerId,
                                            .name = "test.listener",
                                            .size = sizeof(Listener),
                                            .alignment = alignof(Listener),
                                            .plainData = true});
    builder.add<physics2d::Pose2D>();
    return *builder.freeze();
}

audio::Layout layout() {
    audio::Layout made;
    made.buses.push_back(audio::Bus{.id = 1, .name = "master", .role = audio::Role::Master, .parent = 0});
    return made;
}

audio::LoadedSound constant(float value, bool loop) {
    auto clip = std::make_shared<audio::Clip>();
    clip->samples.assign(4800, value);
    audio::LoadedSound sound{.declaration = {}, .clips = {clip}};
    sound.declaration.variants.push_back(audio::Variant{.resource = base::Bits128{.high = 0, .low = 1}});
    sound.declaration.loop = loop;
    sound.declaration.attenuation =
        audio::Attenuation{.minimumDistance = 1, .maximumDistance = 21, .falloff = audio::Falloff::Linear};
    return sound;
}

struct Rig {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<audio::Mixer> mixer = *audio::Mixer::create(layout(), {});
    std::unique_ptr<audio::Sounds> sounds = *audio::Sounds::create(*mixer, layout(), {});
    std::unique_ptr<WorldAudio> heard;

    Rig() {
        const std::size_t kHumIndex = *sounds->add(constant(0.5F, true));
        const std::size_t kClickIndex = *sounds->add(constant(0.1F, false));
        heard = *WorldAudio::create(
            *schema,
            *sounds,
            {.emitter = kEmitterId, .listener = kListenerId, .sounds = {{kHum, kHumIndex}, {kClick, kClickIndex}}});
    }

    world::EntityHandle spawn(std::optional<Emitter> emitter, std::optional<Listener> listener, double x) {
        const world::EntityHandle kEntity = *world.create();
        if (emitter) {
            RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kEmitterId), &*emitter).has_value());
        }
        if (listener) {
            RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kListenerId), &*listener).has_value());
        }
        RAWFRAME_EXPECT(
            world.insert(kEntity, *schema->key<physics2d::Pose2D>(), physics2d::Pose2D{.x = x}).has_value());
        return kEntity;
    }

    Emitter& emitterOf(world::EntityHandle entity) {
        return *static_cast<Emitter*>(world.getErased(entity, *schema->find(kEmitterId)));
    }

    /// One frame: the World heard, then 10 ms rendered; the last sample.
    std::pair<float, float> frame() {
        heard->update(world, 0.01F);
        std::vector<float> out(960);
        mixer->render(out);
        return {out[958], out[959]};
    }
};

bool near(float value, float expected) {
    return std::abs(value - expected) < 1e-4F;
}

} // namespace

RAWFRAME_TEST(AnEmitterSoundsFromItsPose) {
    Rig rig;
    rig.spawn(std::nullopt, Listener{.active = true}, 0);
    const auto kHummer = rig.spawn(Emitter{.sound = kHum, .playing = true}, std::nullopt, 11);
    // Ten meters right, halfway down the falloff: a quarter, on the right.
    static_cast<void>(rig.frame());
    auto [left, right] = rig.frame();
    RAWFRAME_EXPECT(near(right, 0.25F) && near(left, 0));
    // It moves to the left, a meter off: full, on the left.
    rig.world.get(kHummer, *rig.schema->key<physics2d::Pose2D>())->x = -1;
    static_cast<void>(rig.frame());
    std::tie(left, right) = rig.frame();
    RAWFRAME_EXPECT(near(left, 0.5F) && near(right, 0));
    // Not playing: it stops.
    rig.emitterOf(kHummer).playing = false;
    for (int frame = 0; frame < 8; ++frame) {
        std::tie(left, right) = rig.frame();
    }
    RAWFRAME_EXPECT(left == 0 && right == 0);
}

RAWFRAME_TEST(CuesPlayOneShotsFromWhenFirstSeen) {
    Rig rig;
    rig.spawn(std::nullopt, Listener{.active = true}, 0);
    // A cue of five before it is first seen plays nothing.
    const auto kClicker = rig.spawn(Emitter{.sound = kClick, .cue = 5}, std::nullopt, 0);
    auto [left, right] = rig.frame();
    RAWFRAME_EXPECT(left == 0 && rig.heard->statistics().cues == 0);
    // Two more: two clicks at once.
    rig.emitterOf(kClicker).cue = 7;
    std::tie(left, right) = rig.frame();
    RAWFRAME_EXPECT(near(left, 2 * 0.1F / std::sqrt(2.0F)) && rig.heard->statistics().cues == 2);
    // A hundred more at once plays only the most one update starts.
    rig.emitterOf(kClicker).cue = 107;
    static_cast<void>(rig.frame());
    RAWFRAME_EXPECT(rig.heard->statistics().cues == 2 + kMaximumCuesPerUpdate);
    // A looping sound is never cued.
    const auto kHummer = rig.spawn(Emitter{.sound = kHum}, std::nullopt, 0);
    static_cast<void>(rig.frame());
    rig.emitterOf(kHummer).cue = 1;
    static_cast<void>(rig.frame());
    RAWFRAME_EXPECT(rig.heard->statistics().loopingCues == 1);
}

RAWFRAME_TEST(DespawnPoliciesDoWhatTheyName) {
    const auto kSilentAfter = [](std::uint8_t policy, bool loop) {
        Rig rig;
        rig.spawn(std::nullopt, Listener{.active = true}, 0);
        const auto kEmitter =
            rig.spawn(Emitter{.sound = loop ? kHum : kClick, .playing = true, .despawn = policy}, std::nullopt, 0);
        static_cast<void>(rig.frame());
        RAWFRAME_EXPECT(rig.world.destroy(kEmitter).has_value());
        // Frames until it is silent, at most a second.
        for (int frame = 1; frame <= 100; ++frame) {
            if (rig.frame() == std::pair(0.0F, 0.0F)) {
                return frame;
            }
        }
        return 101;
    };
    // Stop: at once (the shortest fade). Fade out: a quarter second.
    RAWFRAME_EXPECT(kSilentAfter(kStop, true) == 1);
    const int kFade = kSilentAfter(kFadeOut, true);
    RAWFRAME_EXPECT(kFade >= 24 && kFade <= 27);
    // Detached, a tenth-second sound plays out its tenth; a loop fades.
    const int kDetached = kSilentAfter(kDetachToCompletion, false);
    RAWFRAME_EXPECT(kDetached >= 8 && kDetached <= 11);
    RAWFRAME_EXPECT(kSilentAfter(kDetachToCompletion, true) == kFade);
}

RAWFRAME_TEST(TwoActiveListenersHearNothing) {
    Rig rig;
    const auto kFirst = rig.spawn(std::nullopt, Listener{.active = true}, 0);
    rig.spawn(std::nullopt, Listener{.active = true}, 5);
    rig.spawn(Emitter{.sound = kHum, .playing = true}, std::nullopt, 0);
    static_cast<void>(rig.frame());
    RAWFRAME_EXPECT(rig.frame() == std::pair(0.0F, 0.0F) && rig.heard->statistics().listenerConflicts == 2);
    // One steps down: heard again.
    static_cast<Listener*>(rig.world.getErased(kFirst, *rig.schema->find(kListenerId)))->active = false;
    static_cast<void>(rig.frame());
    RAWFRAME_EXPECT(rig.frame().first > 0);
}

RAWFRAME_TEST(AQuietEmitterCountsItsCuesAndABoundListenerHears) {
    Rig rig;
    // Two active listener components, which alone hear nothing; bound to an
    // entity, the binding hears.
    rig.spawn(std::nullopt, Listener{.active = true}, 0);
    rig.spawn(std::nullopt, Listener{.active = true}, 0);
    const auto kOwn = rig.spawn(std::nullopt, std::nullopt, 0);
    rig.heard->bindListener(kOwn);
    // Named no sound yet, first seen at cue nought: its first cue, with the
    // sound it names, plays.
    const auto kShooter = rig.spawn(Emitter{}, std::nullopt, 0);
    static_cast<void>(rig.frame());
    rig.emitterOf(kShooter) = Emitter{.sound = kClick, .cue = 1};
    const auto [kLeft, kRight] = rig.frame();
    RAWFRAME_EXPECT(near(kLeft, 0.1F / std::sqrt(2.0F)) && rig.heard->statistics().cues == 1 &&
                    rig.heard->statistics().unknownSounds == 0 && rig.heard->statistics().listenerConflicts == 0);
    // Bound to an entity that is gone: nothing spatial is heard.
    RAWFRAME_EXPECT(rig.world.destroy(kOwn).has_value());
    rig.emitterOf(kShooter).cue = 2;
    static_cast<void>(rig.frame());
    RAWFRAME_EXPECT(rig.frame() == std::pair(0.0F, 0.0F));
}

RAWFRAME_TEST(AGamesAudioLoadsAgainstItsProgram) {
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-world-audio-" + std::to_string(::getpid()));
    std::filesystem::create_directories(kDirectory);
    const auto kWrite = [&kDirectory](std::string_view name, std::string_view text) {
        std::ofstream file{kDirectory / name, std::ios::binary};
        file << text;
    };
    // A project's sources are relative to it.
    kWrite("kest.project",
           "project heard\nsource .\nsource " +
               std::filesystem::relative(RAWFRAME_WORLD_AUDIO_MODULES, kDirectory).string() + "\n");
    kWrite("heard.kest",
           "module heard\n\nimport rawframe.sound\n\n// A Kest type is laid out when a function uses it.\nfn "
           "hear(count: i32, emitters: [sound.Emitter], listeners: [sound.Listener]) {\n}\n");
    kWrite("heard.game",
           "program heard.kest\n"
           "component 3c1f0a8e-5b2d-4e7a-9f64-1d8c2b7a0e53 heard.emitter Emitter\n"
           "component 9e4b7c21-6d0a-4f38-b5e2-7a1c3d9f8b06 heard.listener Listener\n"
           "mixer heard.mixer\nsound 00000000000000a2 click.sound\n");
    kWrite("heard.mixer",
           "{\n  \"kind\": \"audio.mixer\",\n  \"formatVersion\": 1,\n  \"master\": {\n    \"busId\": "
           "\"0000000000000001\",\n    \"name\": \"master\",\n    \"role\": \"master\"\n  }\n}\n");
    kWrite("click.sound",
           "{\n  \"kind\": \"audio.sound\",\n  \"formatVersion\": 1,\n  \"variants\": [\n    {\n      "
           "\"resource\": \"000000000000000000000000000000c1\"\n    }\n  ],\n  \"bus\": \"0000000000000001\"\n}\n");
    kest::CompileSettings compile;
    compile.library = RAWFRAME_KEST_LIBRARY;
    std::string report;
    const auto kProgram = kest::Program::compileFile((kDirectory / "heard.kest").string(), compile, &report);
    RAWFRAME_EXPECT(kProgram.has_value());
    if (kProgram.has_value()) {
        const auto kAudio = loadGameAudio((kDirectory / "heard.game").string(), **kProgram);
        RAWFRAME_EXPECT(kAudio.has_value() && kAudio->emitter == kEmitterId && kAudio->listener == kListenerId &&
                        kAudio->sounds.size() == 1 && kAudio->sounds[0].first == kClick &&
                        kAudio->sounds[0].second.variants.size() == 1 &&
                        kAudio->sounds[0].second.variants[0].resource == kClickClip &&
                        kAudio->layout.buses.size() == 1);
    } else {
        std::fprintf(stderr, "%s\n", report.c_str());
    }
    std::filesystem::remove_all(kDirectory);
}

namespace {

std::vector<std::byte> waveOf(std::size_t frames, float value) {
    audio::Clip clip;
    clip.channels = 1;
    clip.rate = 48'000;
    clip.samples.assign(frames, value);
    return audio::encodeWav(clip);
}

content::ManifestEntry entryOf(std::uint64_t id, const std::string& locator, const std::vector<std::byte>& bytes) {
    return content::ManifestEntry{.id = content::ResourceId{base::Bits128{.high = 0, .low = id}},
                                  .type = content::ResourceTypeId{audio::kSoundClipType},
                                  .representation = *content::RepresentationId::parse("rawframe.audio.wave"),
                                  .byteLength = bytes.size(),
                                  .digest = content::ContentDigest::of(bytes),
                                  .locator = locator};
}

audio::SoundDeclaration declared(std::vector<std::uint64_t> resources, audio::Loading loading) {
    audio::SoundDeclaration declaration;
    for (const std::uint64_t kResource : resources) {
        declaration.variants.push_back(audio::Variant{.resource = base::Bits128{.high = 0, .low = kResource}});
    }
    declaration.loading = loading;
    return declaration;
}

/// A content store over three cooked clips, as a client composes it from a
/// game's cooked output: 1 and 2 decode, 3's bytes are no clip at all.
struct Content {
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    execution::Executor cpu{execution::ExecutorSettings{.kind = execution::ExecutorKind::Cpu, .workers = 1}};
    std::unique_ptr<content::ContentStore> store;

    Content() {
        RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 16}).has_value());
        RAWFRAME_EXPECT(cpu.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 16}).has_value());
        const std::vector<std::byte> kShort = waveOf(2, 0.5F);
        const std::vector<std::byte> kLong = waveOf(6, 0.25F);
        const std::vector<std::byte> kNoClip(16, std::byte{7});
        std::vector<content::ContentSource> sources;
        sources.push_back(std::move(*content::ContentSource::memory({{"a", kShort}, {"b", kLong}, {"c", kNoClip}})));
        store = std::move(*content::ContentStore::create(io, execution::OwnerId{1}, root, clock, std::move(sources)));
        const std::vector<content::BoundManifest> kManifests = {
            {.entries = {entryOf(1, "a", kShort), entryOf(2, "b", kLong), entryOf(3, "c", kNoClip)}, .source = 0}};
        store->publish(*content::ContentCatalog::build(kManifests, soundRepresentations(), 1, 1));
    }
    ~Content() {
        store.reset();
        cpu.stop();
        io.stop();
    }
    Content(const Content&) = delete;
    Content& operator=(const Content&) = delete;

    result::Result<std::unique_ptr<SoundLoader>>
    loader(std::vector<std::pair<std::uint64_t, audio::SoundDeclaration>> sounds) {
        return SoundLoader::create(*store, cpu, execution::OwnerId{1}, root, clock, std::move(sounds));
    }
};

/// Updates `loader` until it is ready or fails, within ten seconds.
result::Result<bool> settle(SoundLoader& loader) {
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        auto done = loader.update(1);
        if (!done.has_value() || *done || std::chrono::steady_clock::now() >= kDeadline) {
            return done;
        }
        std::this_thread::yield();
    }
}

} // namespace

RAWFRAME_TEST(SoundsAreReadByIdentityFromCookedContent) {
    Content content;
    {
        // A preloaded sound of two variants decodes each; a streamed one
        // keeps its cooked bytes as they are.
        auto loader = content.loader(
            {{kHum, declared({1, 2}, audio::Loading::Preload)}, {kClick, declared({2}, audio::Loading::Stream)}});
        RAWFRAME_EXPECT(loader.has_value());
        if (!loader.has_value()) {
            return;
        }
        const auto kReady = settle(**loader);
        RAWFRAME_EXPECT(kReady.has_value() && *kReady);
        const auto kSounds = (*loader)->sounds(1);
        RAWFRAME_EXPECT(kSounds.has_value() && kSounds->size() == 2);
        if (kSounds.has_value() && kSounds->size() == 2) {
            const audio::LoadedSound& hum = (*kSounds)[0].second;
            const audio::LoadedSound& click = (*kSounds)[1].second;
            RAWFRAME_EXPECT((*kSounds)[0].first == kHum && hum.clips.size() == 2 && hum.cooked.empty() &&
                            hum.clips[0]->frames() == 2 && hum.clips[1]->frames() == 6 &&
                            std::abs(hum.clips[1]->samples[0] - 0.25F) < 1e-3F);
            RAWFRAME_EXPECT((*kSounds)[1].first == kClick && click.clips.empty() && click.cooked.size() == 1 &&
                            click.cooked[0]->size() == waveOf(6, 0.25F).size());
        }
    }
    // A variant the content does not hold is refused when asked for.
    RAWFRAME_EXPECT(!content.loader({{kHum, declared({9}, audio::Loading::Preload)}}).has_value());
    {
        // A variant that is no clip fails the whole loader, typed.
        auto loader = content.loader({{kHum, declared({1, 3}, audio::Loading::Preload)}});
        RAWFRAME_EXPECT(loader.has_value());
        if (loader.has_value()) {
            const auto kReady = settle(**loader);
            RAWFRAME_EXPECT(!kReady.has_value() && kReady.error().domain() == assets::kAssetsDomain &&
                            kReady.error().code() == code(assets::AssetError::DecodeFailed));
        }
    }
}

RAWFRAME_TEST(OnDemandSoundsAreReadWhenFirstWanted) {
    Content content;
    // Nothing on demand is asked for up front: not even a resource the
    // content lacks.
    auto loader = content.loader({{kHum, declared({1, 2}, audio::Loading::OnDemand)},
                                  {kClick, declared({3}, audio::Loading::OnDemand)},
                                  {0xa3, declared({9}, audio::Loading::OnDemand)},
                                  {0xa4, declared({1}, audio::Loading::Preload)}});
    RAWFRAME_EXPECT(loader.has_value());
    if (!loader.has_value()) {
        return;
    }
    SoundLoader& sounds = **loader;
    const auto kReady = settle(sounds);
    RAWFRAME_EXPECT(kReady.has_value() && *kReady);
    const auto kMade = sounds.sounds(1);
    RAWFRAME_EXPECT(kMade.has_value() && kMade->size() == 4 && (*kMade)[0].second.clips.size() == 2 &&
                    (*kMade)[0].second.clips[0] == nullptr && (*kMade)[3].second.clips[0] != nullptr);
    RAWFRAME_EXPECT(sounds.arrivals(1).arrived.empty());

    // Wanted: both variants arrive, once; asking again asks for nothing.
    RAWFRAME_EXPECT(sounds.demand(0).has_value() && sounds.demand(0).has_value());
    RAWFRAME_EXPECT(sounds.demand(1).has_value());
    std::vector<Arrival> arrived;
    std::vector<std::size_t> failed;
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((arrived.size() < 2 || failed.empty()) && std::chrono::steady_clock::now() < kDeadline) {
        static_cast<void>(sounds.update(2));
        Arrivals each = sounds.arrivals(2);
        std::ranges::move(each.arrived, std::back_inserter(arrived));
        for (const auto& [kSound, kError] : each.failed) {
            RAWFRAME_EXPECT(kError.code() == code(assets::AssetError::DecodeFailed));
            failed.push_back(kSound);
        }
        std::this_thread::yield();
    }
    RAWFRAME_EXPECT(arrived.size() == 2 && arrived[0].sound == 0 && arrived[0].variant == 0 &&
                    arrived[0].clip->frames() == 2 && arrived[1].variant == 1 && arrived[1].clip->frames() == 6);
    // The sound that is no clip fails, once.
    RAWFRAME_EXPECT(failed == std::vector<std::size_t>{1});
    const Arrivals kLater = sounds.arrivals(3);
    RAWFRAME_EXPECT(kLater.arrived.empty() && kLater.failed.empty());
    // A resource the content lacks is refused when wanted; a sound that is
    // not on demand is never asked for on demand.
    RAWFRAME_EXPECT(!sounds.demand(2).has_value() && !sounds.demand(3).has_value() && !sounds.demand(4).has_value());
}

RAWFRAME_TEST(AMixerHearsAnOnDemandSoundOnceServed) {
    Content content;
    auto loader = content.loader(
        {{kHum, declared({1, 2}, audio::Loading::OnDemand)}, {kClick, declared({3}, audio::Loading::OnDemand)}});
    RAWFRAME_EXPECT(loader.has_value() && settle(**loader).has_value());
    if (!loader.has_value()) {
        return;
    }
    auto mixer = *audio::Mixer::create(layout(), {});
    auto sounds = *audio::Sounds::create(*mixer, layout(), {});
    auto made = (*loader)->sounds(1);
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    for (auto& [kId, sound] : *made) {
        RAWFRAME_EXPECT(sounds->add(std::move(sound)).has_value());
    }
    // The first plays are missed; served frame by frame, the hum is heard
    // and the click, which is no clip, is named unread once.
    RAWFRAME_EXPECT(!sounds->play(0).has_value() && !sounds->play(1).has_value());
    std::vector<std::size_t> unread;
    bool heard = false;
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (std::uint64_t tick = 2; !heard && std::chrono::steady_clock::now() < kDeadline; ++tick) {
        static_cast<void>((*loader)->update(tick));
        for (const auto& [kSound, kError] : (*loader)->serve(*sounds, tick)) {
            unread.push_back(kSound);
        }
        heard = sounds->play(0).has_value();
        std::this_thread::yield();
    }
    std::vector<float> out(4);
    mixer->render(out);
    RAWFRAME_EXPECT(heard && out[0] > 0.3F);
    // The click's failure may land after the hum is heard.
    for (std::uint64_t tick = 0; unread.empty() && std::chrono::steady_clock::now() < kDeadline; ++tick) {
        static_cast<void>((*loader)->update(100 + tick));
        for (const auto& [kSound, kError] : (*loader)->serve(*sounds, 100 + tick)) {
            unread.push_back(kSound);
        }
        std::this_thread::yield();
    }
    RAWFRAME_EXPECT(unread == std::vector<std::size_t>{1} && !sounds->play(1).has_value() &&
                    (*loader)->serve(*sounds, 999).empty());
}
