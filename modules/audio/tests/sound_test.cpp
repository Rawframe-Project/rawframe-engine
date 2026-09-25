// Sound declarations: SPEC-0036's `audio.sound` documents read against a
// layout, every rule refused at its field, and the falloff presets as the
// curves they name.

#include "rawframe/audio/sound.h"
#include "rawframe/document/errors.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::audio;

namespace {

Layout layout() {
    Layout made;
    made.buses.push_back(Bus{.id = 1, .name = "master", .role = Role::Master, .parent = 0});
    made.buses.push_back(Bus{.id = 0xa2, .name = "sfx", .role = Role::Sfx, .parent = 0});
    made.concurrency.push_back(ConcurrencySet{.name = "steps", .maximumInstances = 4});
    return made;
}

constexpr std::string_view kSound = R"({
  "kind": "audio.sound",
  "formatVersion": 1,
  "variants": [
    {
      "clip": "steps/grass_1.wav"
    },
    {
      "clip": "steps/grass_2.wav",
      "weight": 3
    }
  ],
  "selection": "random_no_immediate_repeat",
  "volume": {
    "minimum": -3,
    "maximum": 0
  },
  "pitch": {
    "minimum": 0.9375,
    "maximum": 1.0625
  },
  "loop": {
    "start": 0.5,
    "end": 1.25
  },
  "bus": "00000000000000a2",
  "concurrency": "steps",
  "priority": 5,
  "attenuation": {
    "minimumDistance": 2,
    "maximumDistance": 40,
    "falloff": "linear",
    "noListener": "flat_fallback"
  },
  "virtualization": "track_position",
  "despawn": "fade_out"
}
)";

std::string with(std::string_view from, std::string_view to) {
    std::string text{kSound};
    const std::size_t kAt = text.find(from);
    RAWFRAME_EXPECT(kAt != std::string::npos);
    if (kAt != std::string::npos) {
        text.replace(kAt, from.size(), to);
    }
    return text;
}

std::pair<document::DocumentError, std::string> refusalOf(const std::string& text) {
    const auto kRead = readSound(text, layout());
    if (kRead.has_value()) {
        return {document::DocumentError{}, ""};
    }
    std::string where;
    for (const auto& field : kRead.error().context()) {
        if (field.key == "path" || field.key == "line") {
            where = field.value;
        }
    }
    return {static_cast<document::DocumentError>(kRead.error().code().value), where};
}

} // namespace

RAWFRAME_TEST(ASoundDeclarationReads) {
    const auto kRead = readSound(kSound, layout());
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const SoundDeclaration& sound = *kRead;
    RAWFRAME_EXPECT(sound.variants.size() == 2 && sound.variants[0].weight == 1 && sound.variants[1].weight == 3 &&
                    sound.variants[1].clip == "steps/grass_2.wav");
    RAWFRAME_EXPECT(sound.selection == Selection::RandomNoImmediateRepeat && sound.volumeMinimum == -3 &&
                    sound.volumeMaximum == 0 && sound.pitchMinimum == 0.9375F && sound.pitchMaximum == 1.0625F);
    RAWFRAME_EXPECT(sound.loop && sound.loopStart == 0.5F && sound.loopEnd == 1.25F);
    RAWFRAME_EXPECT(sound.bus == 1 && sound.concurrency == 0 && sound.priority == 5);
    RAWFRAME_EXPECT(sound.attenuation.has_value() && sound.attenuation->minimumDistance == 2 &&
                    sound.attenuation->falloff == Falloff::Linear &&
                    sound.attenuation->noListener == NoListener::FlatFallback);
    RAWFRAME_EXPECT(sound.virtualization == Virtualization::TrackPosition && sound.despawn == Despawn::FadeOut);

    // The least a sound says: one variant and a bus; flat, not looping,
    // sequential, at unity.
    const auto kLeast =
        readSound("{\n  \"kind\": \"audio.sound\",\n  \"formatVersion\": 1,\n  \"variants\": [\n    {\n      \"clip\": "
                  "\"click.wav\"\n    }\n  ],\n  \"bus\": \"0000000000000001\"\n}\n",
                  layout());
    RAWFRAME_EXPECT(kLeast.has_value() && !kLeast->loop && !kLeast->attenuation && kLeast->pitchMinimum == 1 &&
                    kLeast->selection == Selection::Sequential && kLeast->bus == 0 && !kLeast->concurrency &&
                    kLeast->loading == Loading::Preload);
    // Music streamed, looping whole.
    const auto kMusic =
        readSound("{\n  \"kind\": \"audio.sound\",\n  \"formatVersion\": 1,\n  \"variants\": [\n    {\n      \"clip\": "
                  "\"theme.rfopus\"\n    }\n  ],\n  \"loop\": {},\n  \"bus\": \"0000000000000001\",\n  \"loading\": "
                  "\"stream\"\n}\n",
                  layout());
    RAWFRAME_EXPECT(kMusic.has_value() && kMusic->loop && !kMusic->loopStart && kMusic->loading == Loading::Stream);
}

RAWFRAME_TEST(EverySoundRuleIsRefusedAtItsField) {
    using document::DocumentError;
    struct Case {
        std::string_view from;
        std::string_view to;
        DocumentError error;
        std::string_view where;
    };
    const std::vector<Case> kCases = {
        {"\"audio.sound\"", "\"audio.mixer\"", DocumentError::Invalid, "$.kind"},
        {"\"steps/grass_1.wav\"", "\"../grass.wav\"", DocumentError::Invalid, "$.variants[0].clip"},
        {"\"steps/grass_1.wav\"", "\"/grass.wav\"", DocumentError::Invalid, "$.variants[0].clip"},
        {"\"weight\": 3", "\"weight\": 1", DocumentError::NotCanonical, "$.variants[1].weight"},
        {"\"weight\": 3", "\"weight\": 0", DocumentError::Invalid, "$.variants[1].weight"},
        {"\"random_no_immediate_repeat\"", "\"shuffle\"", DocumentError::Invalid, "$.selection"},
        {"\"minimum\": -3", "\"minimum\": 3", DocumentError::Invalid, "$.volume"},
        {"\"minimum\": -3", "\"minimum\": 0", DocumentError::NotCanonical, "$.volume"},
        {"\"minimum\": 0.9375", "\"minimum\": 0", DocumentError::Invalid, "$.pitch"},
        {"\"end\": 1.25", "\"end\": 0.25", DocumentError::Invalid, "$.loop"},
        {"\"00000000000000a2\"", "\"00000000000000a3\"", DocumentError::Invalid, "$.bus"},
        {"\"concurrency\": \"steps\"", "\"concurrency\": \"shots\"", DocumentError::Invalid, "$.concurrency"},
        {"\"priority\": 5", "\"priority\": 5000", DocumentError::Invalid, "$.priority"},
        {"\"priority\": 5,", "\"priority\": 5,\n  \"loading\": \"on_demand\",", DocumentError::Invalid, "$.loading"},
        // A stream loops whole, and this sound has loop points.
        {"\"priority\": 5,", "\"priority\": 5,\n  \"loading\": \"stream\",", DocumentError::Invalid, "$.loading"},
        {"\"priority\": 5,", "\"priority\": 5,\n  \"loading\": \"tape\",", DocumentError::Invalid, "$.loading"},
        {"\"priority\": 5,", "\"priority\": 5,\n  \"loading\": \"preload\",", DocumentError::NotCanonical, "$.loading"},
        {"\"maximumDistance\": 40", "\"maximumDistance\": 2", DocumentError::Invalid, "$.attenuation"},
        {"\"falloff\": \"linear\"", "\"falloff\": \"inverse\"", DocumentError::NotCanonical, "$.attenuation.falloff"},
        {"\"falloff\": \"linear\"", "\"falloff\": \"custom\"", DocumentError::Invalid, "$.attenuation.falloff"},
        {"\"virtualization\": \"track_position\"",
         "\"virtualization\": \"seek\"",
         DocumentError::Invalid,
         "$.virtualization"},
        {"\"despawn\": \"fade_out\"", "\"despawn\": \"stop\"", DocumentError::NotCanonical, "$.despawn"},
    };
    RAWFRAME_EXPECT(refusalOf(std::string{kSound}) == std::pair(DocumentError{}, std::string{}));
    for (const Case& each : kCases) {
        const auto kRefusal = refusalOf(with(each.from, each.to));
        RAWFRAME_EXPECT(kRefusal.first == each.error && kRefusal.second == each.where);
        if (kRefusal.first != each.error || kRefusal.second != each.where) {
            std::fprintf(stderr,
                         "  replacing %.*s: code %u at %s\n",
                         static_cast<int>(each.from.size()),
                         each.from.data(),
                         static_cast<unsigned>(kRefusal.first),
                         kRefusal.second.c_str());
        }
    }
}

RAWFRAME_TEST(FalloffsAreTheCurvesTheyName) {
    Attenuation attenuation{.minimumDistance = 2, .maximumDistance = 32};
    const auto kAt = [&attenuation](Falloff falloff, float distance) {
        attenuation.falloff = falloff;
        return attenuate(attenuation, distance);
    };
    for (const Falloff kFalloff : {Falloff::Inverse, Falloff::InverseSquare, Falloff::Linear, Falloff::Logarithmic}) {
        RAWFRAME_EXPECT(kAt(kFalloff, 0) == 1 && kAt(kFalloff, 2) == 1 && kAt(kFalloff, 32) == 0 &&
                        kAt(kFalloff, 100) == 0 && kAt(kFalloff, std::nanf("")) == 0);
        // Falling all the way.
        float last = 1;
        for (float distance = 2.5F; distance < 32; distance += 0.5F) {
            const float kGain = kAt(kFalloff, distance);
            RAWFRAME_EXPECT(kGain < last && kGain > 0);
            last = kGain;
        }
    }
    RAWFRAME_EXPECT(kAt(Falloff::Inverse, 8) == 0.25F && kAt(Falloff::InverseSquare, 8) == 0.0625F);
    RAWFRAME_EXPECT(kAt(Falloff::Linear, 17) == 0.5F && std::abs(kAt(Falloff::Logarithmic, 8) - 0.5F) < 1e-6F);
}
