// Declared sounds playing: variants picked as declared, concurrency sets
// resolved by their rule, distance and pan from the listener, virtual
// instances that come back where they would be, and streamed sounds playing
// as their preloaded selves.

#include "rawframe/audio/errors.h"
#include "rawframe/audio/sounds.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
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
    LoadedSound sound{.declaration = std::move(declaration), .clips = {}, .cooked = {}};
    for (const float kValue : values) {
        sound.declaration.variants.push_back(Variant{.resource = base::Bits128{.high = 0, .low = 1}});
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

namespace {

std::shared_ptr<const std::vector<std::byte>> cookedFixture() {
    std::ifstream file{std::string{RAWFRAME_AUDIO_DATA} + "tones.rfopus", std::ios::binary};
    const std::vector<char> kRead{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    auto bytes = std::make_shared<std::vector<std::byte>>(kRead.size());
    std::ranges::transform(kRead, bytes->begin(), [](char each) {
        return static_cast<std::byte>(each);
    });
    return bytes;
}

/// What a sound declared as `declaration` plays over `frames` frames, from
/// its variant held as `loading` asks.
std::vector<float> playedAs(Loading loading, SoundDeclaration declaration, std::size_t frames) {
    Streamer streamer;
    auto mixer = *Mixer::create(layout(), {});
    auto sounds = *Sounds::create(*mixer, layout(), {.streamer = &streamer});
    declaration.loading = loading;
    declaration.variants = {Variant{.resource = base::Bits128{.high = 0, .low = 2}}};
    LoadedSound sound{.declaration = declaration, .clips = {}, .cooked = {}};
    if (loading == Loading::Stream) {
        sound.cooked.push_back(cookedFixture());
    } else {
        sound.clips.push_back(std::make_shared<const Clip>(*decodeCookedOpus(*cookedFixture())));
    }
    const std::size_t kSound = *sounds->add(std::move(sound));
    static_cast<void>(sounds->play(kSound));
    std::vector<float> out(frames * 2);
    for (std::size_t at = 0; at < frames; at += 256) {
        sounds->update(256.0F / 48'000.0F);
        mixer->render(std::span{out}.subspan(at * 2, std::min<std::size_t>(256, frames - at) * 2));
    }
    return out;
}

} // namespace

RAWFRAME_TEST(AStreamedSoundPlaysAsItsPreloadedSelf) {
    const SoundDeclaration kOnce{.bus = kSfx};
    RAWFRAME_EXPECT(playedAs(Loading::Stream, kOnce, 80'000) == playedAs(Loading::Preload, kOnce, 80'000));
    const SoundDeclaration kLooped{.loop = true, .bus = kSfx};
    RAWFRAME_EXPECT(playedAs(Loading::Stream, kLooped, 160'000) == playedAs(Loading::Preload, kLooped, 160'000));

    // Streamed sounds need a streamer, and cooked variants.
    auto mixer = *Mixer::create(layout(), {});
    auto unstreamed = *Sounds::create(*mixer, layout(), {});
    LoadedSound streamed{.declaration = SoundDeclaration{.bus = kSfx, .loading = Loading::Stream},
                         .clips = {},
                         .cooked = {cookedFixture()}};
    streamed.declaration.variants = {Variant{.resource = base::Bits128{.high = 0, .low = 2}}};
    RAWFRAME_EXPECT(!unstreamed->add(streamed).has_value());
    Streamer streamer;
    auto withStreamer = *Sounds::create(*mixer, layout(), {.streamer = &streamer});
    RAWFRAME_EXPECT(withStreamer->add(streamed).has_value());
    streamed.clips.push_back(clipOf(0.5F));
    RAWFRAME_EXPECT(!withStreamer->add(streamed).has_value());
}

namespace {

/// A spatial sound, held as `loading` asks, heard near, then out of range
/// for half a second (virtual), then near again: what the last 0.2 s hold.
std::vector<float> revivedAs(Loading loading) {
    Streamer streamer;
    auto mixer = *Mixer::create(layout(), {});
    auto sounds = *Sounds::create(*mixer, layout(), {.streamer = &streamer});
    SoundDeclaration declaration{.bus = kSfx,
                                 .loading = loading,
                                 .attenuation = Attenuation{.minimumDistance = 1, .maximumDistance = 10},
                                 .virtualization = Virtualization::TrackPosition};
    declaration.variants = {Variant{.resource = base::Bits128{.high = 0, .low = 2}}};
    LoadedSound sound{.declaration = declaration, .clips = {}, .cooked = {}};
    if (loading == Loading::Stream) {
        sound.cooked.push_back(cookedFixture());
    } else {
        sound.clips.push_back(std::make_shared<const Clip>(*decodeCookedOpus(*cookedFixture())));
    }
    const std::size_t kSound = *sounds->add(std::move(sound));
    sounds->setListener(Listener{});
    const auto kInstance = sounds->play(kSound, Position{});
    std::vector<float> out(512);
    const auto kRun = [&](std::size_t blocks) {
        for (std::size_t block = 0; block < blocks; ++block) {
            sounds->update(256.0F / 48'000.0F);
            mixer->render(out);
        }
    };
    kRun(20);
    sounds->setListener(Listener{.position = {.x = 100, .y = 0, .z = 0}});
    kRun(94);
    RAWFRAME_EXPECT(kInstance.has_value() && sounds->state(*kInstance) == InstanceState::Virtual);
    sounds->setListener(Listener{});
    kRun(2);
    RAWFRAME_EXPECT(kInstance.has_value() && sounds->state(*kInstance) == InstanceState::Playing);
    std::vector<float> after;
    for (int block = 0; block < 37; ++block) {
        sounds->update(256.0F / 48'000.0F);
        mixer->render(out);
        after.insert(after.end(), out.begin(), out.end());
    }
    return after;
}

} // namespace

RAWFRAME_TEST(AVirtualStreamComesBackWhereItWouldBe) {
    const std::vector<float> kStreamed = revivedAs(Loading::Stream);
    const std::vector<float> kPreloaded = revivedAs(Loading::Preload);
    double signal = 0;
    double error = 0;
    for (std::size_t index = 0; index < kPreloaded.size(); ++index) {
        signal += static_cast<double>(kPreloaded[index]) * kPreloaded[index];
        error += static_cast<double>(kStreamed[index] - kPreloaded[index]) * (kStreamed[index] - kPreloaded[index]);
    }
    RAWFRAME_EXPECT(signal > 0 && 10.0 * std::log10(error / signal) < -50);
}
