// Declared sounds playing: variants picked as declared, concurrency sets
// resolved by their rule, distance and pan from the listener, virtual
// instances that come back where they would be, and a sound loaded from
// its files.

#include "rawframe/audio/errors.h"
#include "rawframe/audio/sounds.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using namespace rawframe::audio;

namespace {

constexpr std::size_t kSfx = 1;

Layout layout(Resolution resolution = Resolution::StopOldest) {
    Layout made;
    made.buses.push_back(Bus{.id = 1, .name = "master", .role = Role::Master, .parent = 0});
    made.buses.push_back(Bus{.id = 2, .name = "sfx", .role = Role::Sfx, .parent = 0});
    made.concurrency.push_back(ConcurrencySet{.name = "pair", .maximumInstances = 2, .resolution = resolution});
    return made;
}

/// A mono clip of one value, a second long at 48 kHz unless told.
std::shared_ptr<const Clip> clipOf(float value, std::size_t frames = 48'000) {
    auto clip = std::make_shared<Clip>();
    clip->samples.assign(frames, value);
    return clip;
}

LoadedSound declared(std::vector<float> values, SoundDeclaration declaration) {
    LoadedSound sound{.declaration = std::move(declaration), .clips = {}};
    for (const float kValue : values) {
        sound.declaration.variants.push_back(Variant{.clip = "x.wav"});
        sound.clips.push_back(clipOf(kValue));
    }
    return sound;
}

/// What the mixer plays now, left and right, after a render.
std::pair<float, float> heard(Mixer& mixer) {
    std::vector<float> out(512);
    mixer.render(out);
    return {out[510], out[511]};
}

} // namespace

RAWFRAME_TEST(VariantsArePickedAsDeclared) {
    auto mixer = *Mixer::create(layout(), {});
    auto sounds = *Sounds::create(*mixer, layout(), {.seed = 3});
    const std::size_t kSequential =
        *sounds->add(declared({0.1F, 0.2F, 0.3F}, SoundDeclaration{.selection = Selection::Sequential, .bus = kSfx}));
    std::vector<float> order;
    for (int play = 0; play < 4; ++play) {
        const auto kInstance = sounds->play(kSequential);
        RAWFRAME_EXPECT(kInstance.has_value());
        order.push_back(heard(*mixer).first);
        sounds->stop(*kInstance);
        static_cast<void>(heard(*mixer));
        sounds->update(0.02F);
    }
    const float kCentre = 1.0F / std::sqrt(2.0F);
    RAWFRAME_EXPECT(std::abs(order[0] - (0.1F * kCentre)) < 1e-5F && std::abs(order[2] - (0.3F * kCentre)) < 1e-5F &&
                    std::abs(order[3] - (0.1F * kCentre)) < 1e-5F);

    // Weighted and never the same twice running.
    auto choice =
        declared({0.1F, 0.2F}, SoundDeclaration{.selection = Selection::RandomNoImmediateRepeat, .bus = kSfx});
    choice.declaration.variants[1].weight = 9;
    const std::size_t kNoRepeat = *sounds->add(choice);
    float last = -1;
    for (int play = 0; play < 50; ++play) {
        const auto kInstance = sounds->play(kNoRepeat);
        const float kNow = heard(*mixer).first;
        RAWFRAME_EXPECT(std::abs(kNow - last) > 1e-6F);
        last = kNow;
        sounds->stop(*kInstance);
        static_cast<void>(heard(*mixer));
        sounds->update(0.02F);
    }
}

RAWFRAME_TEST(AFullConcurrencySetFollowsItsRule) {
    const auto kPlays = [](Resolution resolution, std::vector<std::int32_t> priorities, std::vector<float> distances) {
        auto mixer = *Mixer::create(layout(resolution), {});
        auto sounds = *Sounds::create(*mixer, layout(resolution), {});
        sounds->setListener(Listener{});
        std::vector<Instance> made;
        std::vector<bool> refused;
        for (std::size_t index = 0; index < priorities.size(); ++index) {
            SoundDeclaration declaration{.bus = kSfx, .concurrency = 0, .priority = priorities[index]};
            declaration.attenuation = Attenuation{.minimumDistance = 1, .maximumDistance = 100};
            const std::size_t kSound = *sounds->add(declared({0.1F}, declaration));
            const auto kPlay = sounds->play(kSound, Position{.x = distances[index]});
            refused.push_back(!kPlay.has_value());
            made.push_back(kPlay.value_or(Instance{.slot = 999}));
        }
        std::vector<bool> alive;
        for (const Instance& instance : made) {
            alive.push_back(sounds->state(instance) == InstanceState::Playing);
        }
        return std::pair{alive, refused};
    };
    // Oldest out.
    RAWFRAME_EXPECT(kPlays(Resolution::StopOldest, {0, 0, 0}, {1, 1, 1}).first ==
                    std::vector<bool>({false, true, true}));
    // None in.
    const auto kPrevent = kPlays(Resolution::PreventNew, {0, 0, 0}, {1, 1, 1});
    RAWFRAME_EXPECT(kPrevent.first == std::vector<bool>({true, true, false}) && kPrevent.second[2]);
    // The farthest out, the newcomer too when it is the farthest.
    RAWFRAME_EXPECT(kPlays(Resolution::StopFarthestThenOldest, {0, 0, 0}, {5, 30, 10}).first ==
                    std::vector<bool>({true, false, true}));
    RAWFRAME_EXPECT(kPlays(Resolution::StopFarthestThenOldest, {0, 0, 0}, {5, 3, 50}).second[2]);
    // The lowest priority out; equals, the oldest.
    RAWFRAME_EXPECT(kPlays(Resolution::StopLowestPriorityThenOldest, {5, 1, 3}, {1, 1, 1}).first ==
                    std::vector<bool>({true, false, true}));
    RAWFRAME_EXPECT(kPlays(Resolution::StopLowestPriorityThenOldest, {2, 2, 2}, {1, 1, 1}).first ==
                    std::vector<bool>({false, true, true}));
    // The quietest out: the farther one here, as all fall the same way.
    RAWFRAME_EXPECT(kPlays(Resolution::StopQuietest, {0, 0, 0}, {40, 2, 20}).first ==
                    std::vector<bool>({false, true, true}));
}

RAWFRAME_TEST(SpatialSoundsFadeWithDistanceAndPanToTheirSide) {
    auto mixer = *Mixer::create(layout(), {});
    auto sounds = *Sounds::create(*mixer, layout(), {});
    SoundDeclaration declaration{.loop = true, .bus = kSfx, .virtualization = Virtualization::TrackPosition};
    declaration.attenuation = Attenuation{.minimumDistance = 1, .maximumDistance = 21, .falloff = Falloff::Linear};
    const std::size_t kSound = *sounds->add(declared({0.5F}, declaration));
    // No listener yet: silent, as declared, which it is by going virtual.
    const auto kPlay = *sounds->play(kSound, Position{.x = 11});
    RAWFRAME_EXPECT(heard(*mixer) == std::pair(0.0F, 0.0F) && sounds->state(kPlay) == InstanceState::Virtual);
    // Ten meters to the right, halfway down a linear falloff: all on the
    // right at half level.
    sounds->setListener(Listener{});
    sounds->update(0.01F);
    static_cast<void>(heard(*mixer));
    auto [left, right] = heard(*mixer);
    RAWFRAME_EXPECT(std::abs(right - 0.25F) < 1e-4F && std::abs(left) < 1e-4F);
    // Out of range: virtual, no voice; back in range: playing again.
    sounds->move(kPlay, Position{.x = 0, .y = 0, .z = -30});
    sounds->update(0.01F);
    RAWFRAME_EXPECT(sounds->state(kPlay) == InstanceState::Virtual && sounds->statistics().virtualized == 2);
    // Its voice fades out over 50 ms and is gone.
    for (int frame = 0; frame < 12; ++frame) {
        static_cast<void>(heard(*mixer));
        sounds->update(0.01F);
    }
    RAWFRAME_EXPECT(heard(*mixer) == std::pair(0.0F, 0.0F));
    sounds->move(kPlay, Position{.x = -1});
    sounds->update(0.01F);
    std::tie(left, right) = heard(*mixer);
    RAWFRAME_EXPECT(sounds->state(kPlay) == InstanceState::Playing && sounds->statistics().revived == 2 &&
                    std::abs(left - 0.5F) < 1e-4F);

    // Without virtualization, out of range is the end of it.
    SoundDeclaration once = declaration;
    once.virtualization = Virtualization::Disabled;
    const std::size_t kOnce = *sounds->add(declared({0.5F}, once));
    RAWFRAME_EXPECT(!sounds->play(kOnce, Position{.x = 50}).has_value());
    const auto kCulled = *sounds->play(kOnce, Position{.x = 5});
    sounds->move(kCulled, Position{.x = 50});
    sounds->update(0.01F);
    static_cast<void>(heard(*mixer));
    sounds->update(0.01F);
    RAWFRAME_EXPECT(sounds->state(kCulled) == InstanceState::Finished && sounds->statistics().culled == 1);
}

RAWFRAME_TEST(AnInstanceWithoutAVoiceWaitsVirtual) {
    auto mixer = *Mixer::create(layout(), {.voices = 1});
    auto sounds = *Sounds::create(*mixer, layout(), {});
    SoundDeclaration declaration{.bus = kSfx, .virtualization = Virtualization::TrackPosition};
    const std::size_t kSound = *sounds->add(declared({0.25F}, declaration));
    SoundDeclaration flat = declaration;
    flat.virtualization = Virtualization::Disabled;
    const std::size_t kFlat = *sounds->add(declared({0.25F}, flat));
    const auto kFirst = *sounds->play(kSound);
    // No voice: the one that may go virtual does; the other is refused.
    const auto kSecond = *sounds->play(kSound);
    RAWFRAME_EXPECT(sounds->state(kSecond) == InstanceState::Virtual);
    const auto kRefused = sounds->play(kFlat);
    RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().code() == code(AudioError::NoVoice));
    // The first stops; the second comes back, a tenth of a second in.
    sounds->stop(kFirst);
    static_cast<void>(heard(*mixer));
    sounds->update(0.1F);
    RAWFRAME_EXPECT(sounds->state(kFirst) == InstanceState::Finished &&
                    sounds->state(kSecond) == InstanceState::Playing && sounds->statistics().revived == 1);
    // A second-long clip, a tenth already gone: it ends before a full
    // second more of updates.
    for (int frame = 0; frame < 100 && sounds->state(kSecond) != InstanceState::Finished; ++frame) {
        std::vector<float> out(960);
        mixer->render(out);
        sounds->update(0.01F);
    }
    RAWFRAME_EXPECT(sounds->state(kSecond) == InstanceState::Finished);
}

RAWFRAME_TEST(ASoundLoadsFromItsFiles) {
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-sound-" + std::to_string(::getpid()));
    std::filesystem::create_directories(kDirectory);
    {
        // A 16-bit mono WAVE of four samples.
        std::ofstream wave{kDirectory / "blip.wav", std::ios::binary};
        const std::uint8_t kBytes[] = {'R', 'I', 'F', 'F',  44, 0, 0,    0, 'W',  'A', 'V',  'E',  'f',
                                       'm', 't', ' ', 16,   0,  0, 0,    1, 0,    1,   0,    0x80, 0xBB,
                                       0,   0,   0,   0x77, 1,  0, 2,    0, 16,   0,   'd',  'a',  't',
                                       'a', 8,   0,   0,    0,  0, 0x40, 0, 0x40, 0,   0x40, 0,    0x40};
        wave.write(reinterpret_cast<const char*>(kBytes), sizeof kBytes);
        std::ofstream declaration{kDirectory / "blip.sound"};
        declaration << "{\n  \"kind\": \"audio.sound\",\n  \"formatVersion\": 1,\n  \"variants\": [\n    {\n      "
                       "\"clip\": \"blip.wav\"\n    }\n  ],\n  \"bus\": \"0000000000000002\"\n}\n";
    }
    const auto kLoaded = loadSound((kDirectory / "blip.sound").string(), layout());
    RAWFRAME_EXPECT(kLoaded.has_value() && kLoaded->clips.size() == 1 && kLoaded->clips[0]->frames() == 4 &&
                    kLoaded->clips[0]->samples[0] == 0.5F && kLoaded->declaration.bus == kSfx);
    const auto kMissing = loadSound((kDirectory / "none.sound").string(), layout());
    RAWFRAME_EXPECT(!kMissing.has_value());
    std::filesystem::remove_all(kDirectory);
}
