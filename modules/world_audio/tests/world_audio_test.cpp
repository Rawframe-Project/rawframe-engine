// A World heard: an emitter's continuous sound follows its pose and its
// playing flag, its cue plays one-shots counted from when it was first
// seen, despawn policies do what they name, two active listeners hear
// nothing, and a game's audio loads from its files against its program.

#include "rawframe/audio/mixer.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/test/test.h"
#include "rawframe/world_audio/world_audio.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace rawframe;
using namespace rawframe::world_audio;

namespace {

constexpr auto kEmitterId = schema::ComponentTypeId::fromText("3c1f0a8e-5b2d-4e7a-9f64-1d8c2b7a0e53");
constexpr auto kListenerId = schema::ComponentTypeId::fromText("9e4b7c21-6d0a-4f38-b5e2-7a1c3d9f8b06");
constexpr std::uint64_t kHum = 0xa1;
constexpr std::uint64_t kClick = 0xa2;

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
    sound.declaration.variants.push_back(audio::Variant{.clip = "x.wav"});
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
           "{\n  \"kind\": \"audio.sound\",\n  \"formatVersion\": 1,\n  \"variants\": [\n    {\n      \"clip\": "
           "\"click.wav\"\n    }\n  ],\n  \"bus\": \"0000000000000001\"\n}\n");
    const std::string kWave{
        "RIFF\x28\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x80\xBB\0\0\0\x77\x01\0\x02\0\x10\0data\x04\0\0\0"
        "\0\x40\0\x40",
        48};
    kWrite("click.wav", kWave);
    kest::CompileSettings compile;
    compile.library = RAWFRAME_KEST_LIBRARY;
    std::string report;
    const auto kProgram = kest::Program::compileFile((kDirectory / "heard.kest").string(), compile, &report);
    RAWFRAME_EXPECT(kProgram.has_value());
    if (kProgram.has_value()) {
        const auto kAudio = loadGameAudio((kDirectory / "heard.game").string(), **kProgram);
        RAWFRAME_EXPECT(kAudio.has_value() && kAudio->emitter == kEmitterId && kAudio->listener == kListenerId &&
                        kAudio->sounds.size() == 1 && kAudio->sounds[0].first == kClick &&
                        kAudio->sounds[0].second.clips[0]->frames() == 2 && kAudio->layout.buses.size() == 1);
    } else {
        std::fprintf(stderr, "%s\n", report.c_str());
    }
    std::filesystem::remove_all(kDirectory);
}
