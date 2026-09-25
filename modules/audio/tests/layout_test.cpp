// Mixer layouts: SPEC-0036's `audio.mixer` documents read into one bus tree
// with sends, every rule refused at its field, and a mix order in which
// every bus follows what feeds it.

#include "rawframe/audio/layout.h"
#include "rawframe/document/errors.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::audio;

namespace {

constexpr std::string_view kLayout = R"({
  "kind": "audio.mixer",
  "formatVersion": 1,
  "master": {
    "busId": "0000000000000001",
    "name": "master",
    "role": "master",
    "effects": [
      {
        "type": "rawframe/gain@1",
        "level": -3
      }
    ],
    "children": [
      {
        "busId": "0000000000000002",
        "name": "music",
        "role": "music",
        "volume": -6
      },
      {
        "busId": "0000000000000003",
        "name": "effects",
        "role": "sfx",
        "sends": [
          {
            "target": "0000000000000005",
            "level": -12,
            "position": "pre_fader"
          }
        ],
        "children": [
          {
            "busId": "0000000000000004",
            "name": "footsteps",
            "effects": [
              {
                "type": "rawframe/filter@1",
                "shape": "high_pass",
                "cutoff": 80,
                "slope": 24
              }
            ]
          }
        ]
      },
      {
        "busId": "0000000000000005",
        "name": "echoes",
        "muted": true,
        "effects": [
          {
            "type": "rawframe/delay@1",
            "bypass": true,
            "time": 0.25,
            "feedback": 0.5,
            "offset": 0.125
          }
        ]
      }
    ]
  },
  "concurrency": [
    {
      "name": "steps",
      "maximumInstances": 4,
      "resolution": "stop_oldest"
    },
    {
      "name": "shots",
      "maximumInstances": 16
    }
  ]
}
)";

/// The three effects with parameters: an equalizer and a limiter on the
/// master, music ducked under the voice, and a room's reverb.
constexpr std::string_view kEffects = R"({
  "kind": "audio.mixer",
  "formatVersion": 1,
  "master": {
    "busId": "0000000000000001",
    "name": "master",
    "role": "master",
    "effects": [
      {
        "type": "rawframe/parametric_eq@1",
        "bands": [
          {
            "shape": "low_shelf",
            "frequency": 120,
            "gain": -6
          },
          {
            "shape": "peak",
            "frequency": 2500,
            "gain": 3,
            "q": 2
          },
          {
            "shape": "notch",
            "frequency": 60,
            "q": 8
          }
        ]
      },
      {
        "type": "rawframe/dynamics@1",
        "processor": "limiter",
        "threshold": -1,
        "knee": 2
      }
    ],
    "children": [
      {
        "busId": "0000000000000002",
        "name": "music",
        "role": "music",
        "effects": [
          {
            "type": "rawframe/dynamics@1",
            "processor": "compressor",
            "threshold": -24,
            "ratio": 3,
            "attack": 0.005,
            "release": 0.25,
            "makeup": 4,
            "key": "0000000000000003"
          }
        ]
      },
      {
        "busId": "0000000000000003",
        "name": "voice",
        "role": "voice"
      },
      {
        "busId": "0000000000000004",
        "name": "room",
        "effects": [
          {
            "type": "rawframe/reverb@1",
            "decay": 2.5,
            "preDelay": 0.03,
            "damping": 0.25,
            "mix": 1
          }
        ]
      }
    ]
  }
}
)";

std::string with(std::string_view from, std::string_view to, std::string_view base = kLayout) {
    std::string text{base};
    const std::size_t kAt = text.find(from);
    RAWFRAME_EXPECT(kAt != std::string::npos);
    if (kAt != std::string::npos) {
        text.replace(kAt, from.size(), to);
    }
    return text;
}

std::pair<document::DocumentError, std::string> refusalOf(const std::string& text, const LayoutLimits& limits = {}) {
    const auto kRead = readLayout(text, limits);
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

RAWFRAME_TEST(ALayoutReads) {
    const auto kRead = readLayout(kLayout);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const Layout& layout = *kRead;
    RAWFRAME_EXPECT(layout.buses.size() == 5);
    // Parents before children, in document order.
    RAWFRAME_EXPECT(layout.buses[0].name == "master" && layout.buses[0].parent == 0 &&
                    layout.buses[3].name == "footsteps" && layout.buses[3].parent == 2);
    RAWFRAME_EXPECT(layout.busWithRole(Role::Music) == 1 && layout.busWithRole(Role::Sfx) == 2 &&
                    !layout.busWithRole(Role::Voice).has_value());
    RAWFRAME_EXPECT(layout.buses[1].volume == -6 && layout.buses[4].muted && !layout.buses[1].muted);
    RAWFRAME_EXPECT(layout.buses[0].effects[0].type == EffectType::Gain && layout.buses[0].effects[0].level == -3);
    const Effect& filter = layout.buses[3].effects[0];
    RAWFRAME_EXPECT(filter.type == EffectType::Filter && filter.shape == FilterShape::HighPass && filter.cutoff == 80 &&
                    filter.slope == 24 && std::abs(filter.resonance - 0.7071F) < 1e-6F);
    const Effect& delay = layout.buses[4].effects[0];
    RAWFRAME_EXPECT(delay.bypass && delay.time == 0.25F && delay.feedback == 0.5F && delay.mix == 0.5F &&
                    delay.offset == 0.125F);
    RAWFRAME_EXPECT(layout.buses[2].sends.size() == 1 && layout.buses[2].sends[0].target == 4 &&
                    layout.buses[2].sends[0].level == -12 &&
                    layout.buses[2].sends[0].position == SendPosition::PreFader);
    RAWFRAME_EXPECT(layout.concurrency.size() == 2 && layout.concurrency[0].resolution == Resolution::StopOldest &&
                    layout.concurrency[1].resolution == Resolution::StopFarthestThenOldest &&
                    layout.concurrency[1].maximumInstances == 16);
    // Every bus is mixed after what feeds it: footsteps before effects,
    // effects before echoes (its send) and master, master last.
    const std::vector<std::size_t> kOrder = layout.mixOrder();
    const auto kAt = [&kOrder](std::size_t bus) {
        return std::ranges::find(kOrder, bus) - kOrder.begin();
    };
    RAWFRAME_EXPECT(kOrder.size() == 5 && kOrder.back() == 0 && kAt(3) < kAt(2) && kAt(2) < kAt(4));
    RAWFRAME_EXPECT(std::abs(gainOf(-6) - 0.501187F) < 1e-5F && gainOf(0) == 1.0F);
}

RAWFRAME_TEST(EveryLayoutRuleIsRefusedAtItsField) {
    using document::DocumentError;
    struct Case {
        std::string_view from;
        std::string_view to;
        DocumentError error;
        std::string_view where;
    };
    const std::vector<Case> kCases = {
        {"\"formatVersion\": 1", "\"formatVersion\": 2", DocumentError::Invalid, "formatVersion"},
        {"\"audio.mixer\"", "\"audio.sound\"", DocumentError::Invalid, "$.kind"},
        {"\"name\": \"master\",\n    \"role\": \"master\",",
         "\"name\": \"master\",",
         DocumentError::Invalid,
         "$.master.role"},
        {"\"role\": \"master\"", "\"role\": \"music\"", DocumentError::Invalid, "$.master.role"},
        {"\"role\": \"music\"", "\"role\": \"master\"", DocumentError::Invalid, "$.master.children[0].role"},
        {"\"role\": \"sfx\"", "\"role\": \"music\"", DocumentError::Invalid, "$.master.children[1].role"},
        {"\"role\": \"sfx\"", "\"role\": \"none\"", DocumentError::NotCanonical, "$.master.children[1].role"},
        {"\"role\": \"sfx\"", "\"role\": \"effects\"", DocumentError::Invalid, "$.master.children[1].role"},
        {"\"0000000000000004\"",
         "\"0000000000000002\"",
         DocumentError::Invalid,
         "$.master.children[1].children[0].busId"},
        {"\"name\": \"echoes\"", "\"name\": \"music\"", DocumentError::Invalid, "$.master.children[2].name"},
        {"\"volume\": -6", "\"volume\": 30", DocumentError::Invalid, "$.master.children[0].volume"},
        {"\"volume\": -6", "\"volume\": 0", DocumentError::NotCanonical, "$.master.children[0].volume"},
        {"\"rawframe/gain@1\"", "\"rawframe/chorus@1\"", DocumentError::Invalid, "$.master.effects[0].type"},
        {"\"cutoff\": 80",
         "\"cutoff\": 5",
         DocumentError::Invalid,
         "$.master.children[1].children[0].effects[0].cutoff"},
        {"\"slope\": 24", "\"slope\": 18", DocumentError::Invalid, "$.master.children[1].children[0].effects[0].slope"},
        {"\"shape\": \"high_pass\"",
         "\"shape\": \"notch\"",
         DocumentError::Invalid,
         "$.master.children[1].children[0].effects[0].shape"},
        {"\"feedback\": 0.5", "\"feedback\": 1", DocumentError::Invalid, "$.master.children[2].effects[0]"},
        {"\"time\": 0.25", "\"time\": 1.9375", DocumentError::Invalid, "$.master.children[2].effects[0]"},
        {"\"target\": \"0000000000000005\"",
         "\"target\": \"0000000000000009\"",
         DocumentError::Invalid,
         "$.master.children[1].sends[0].target"},
        {"\"target\": \"0000000000000005\"",
         "\"target\": \"0000000000000003\"",
         DocumentError::Invalid,
         "$.master.children[1].sends[0].target"},
        // A send down into its own child: the child feeds it through the
        // tree, so it would feed itself.
        {"\"target\": \"0000000000000005\"", "\"target\": \"0000000000000004\"", DocumentError::Invalid, "$.master"},
        {"\"position\": \"pre_fader\"",
         "\"position\": \"post_fader\"",
         DocumentError::NotCanonical,
         "$.master.children[1].sends[0].position"},
        {"\"maximumInstances\": 4",
         "\"maximumInstances\": 0",
         DocumentError::Invalid,
         "$.concurrency[0].maximumInstances"},
        {"\"name\": \"shots\"", "\"name\": \"steps\"", DocumentError::Invalid, "$.concurrency[1].name"},
        {"\"resolution\": \"stop_oldest\"",
         "\"resolution\": \"newest_wins\"",
         DocumentError::Invalid,
         "$.concurrency[0].resolution"},
    };
    RAWFRAME_EXPECT(refusalOf(std::string{kLayout}) == std::pair(DocumentError{}, std::string{}));
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

RAWFRAME_TEST(EqualizersDynamicsAndReverbsAreRead) {
    const auto kRead = readLayout(kEffects);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const Layout& layout = *kRead;
    const Effect& eq = layout.buses[0].effects[0];
    RAWFRAME_EXPECT(eq.type == EffectType::ParametricEq && eq.bands.size() == 3);
    RAWFRAME_EXPECT(eq.bands[0].shape == BandShape::LowShelf && eq.bands[0].frequency == 120 &&
                    eq.bands[0].gain == -6 && std::abs(eq.bands[0].q - 0.7071F) < 1e-6F);
    RAWFRAME_EXPECT(eq.bands[1].shape == BandShape::Peak && eq.bands[1].q == 2);
    RAWFRAME_EXPECT(eq.bands[2].shape == BandShape::Notch && eq.bands[2].gain == 0 && eq.bands[2].q == 8);
    const Dynamics& limiter = layout.buses[0].effects[1].dynamics;
    RAWFRAME_EXPECT(limiter.processor == Processor::Limiter && limiter.threshold == -1 && limiter.knee == 2 &&
                    !limiter.key.has_value());
    const Dynamics& ducker = layout.buses[1].effects[0].dynamics;
    RAWFRAME_EXPECT(ducker.processor == Processor::Compressor && ducker.threshold == -24 && ducker.ratio == 3 &&
                    ducker.attack == 0.005F && ducker.release == 0.25F && ducker.makeup == 4 && ducker.key == 2U);
    const Reverb& room = layout.buses[3].effects[0].reverb;
    RAWFRAME_EXPECT(layout.buses[3].effects[0].type == EffectType::Reverb && room.decay == 2.5F &&
                    room.preDelay == 0.03F && room.early == -6 && room.late == 0 && room.damping == 0.25F &&
                    room.density == 1 && room.diffusion == 1 && room.mix == 1);
    // The voice keys the music, so it is mixed first.
    const std::vector<std::size_t> kOrder = layout.mixOrder();
    RAWFRAME_EXPECT(kOrder.size() == 4 && kOrder.back() == 0 &&
                    std::ranges::find(kOrder, 2U) < std::ranges::find(kOrder, 1U));
}

RAWFRAME_TEST(EveryEffectRuleIsRefusedAtItsField) {
    using document::DocumentError;
    struct Case {
        std::string_view from;
        std::string_view to;
        DocumentError error;
        std::string_view where;
    };
    const std::vector<Case> kCases = {
        {"\"shape\": \"peak\"", "\"shape\": \"bell\"", DocumentError::Invalid, "$.master.effects[0].bands[1].shape"},
        {"\"frequency\": 2500", "\"frequency\": 5", DocumentError::Invalid, "$.master.effects[0].bands[1].frequency"},
        {"\"gain\": 3", "\"gain\": 30", DocumentError::Invalid, "$.master.effects[0].bands[1].gain"},
        {"\"q\": 2", "\"q\": 0.7071", DocumentError::NotCanonical, "$.master.effects[0].bands[1].q"},
        {"\"frequency\": 60,",
         "\"frequency\": 60,\n            \"gain\": 3,",
         DocumentError::Invalid,
         "$.master.effects[0].bands[2].gain"},
        {"\"processor\": \"limiter\"",
         "\"processor\": \"ducker\"",
         DocumentError::Invalid,
         "$.master.effects[1].processor"},
        {"\"threshold\": -1,",
         "\"threshold\": -1,\n        \"ratio\": 8,",
         DocumentError::Invalid,
         "$.master.effects[1].ratio"},
        {"\"threshold\": -1", "\"threshold\": 3", DocumentError::Invalid, "$.master.effects[1].threshold"},
        {"\"knee\": 2", "\"knee\": 30", DocumentError::Invalid, "$.master.effects[1].knee"},
        {"\"ratio\": 3", "\"ratio\": 4", DocumentError::NotCanonical, "$.master.children[0].effects[0].ratio"},
        {"\"attack\": 0.005", "\"attack\": 2", DocumentError::Invalid, "$.master.children[0].effects[0].attack"},
        {"\"key\": \"0000000000000003\"",
         "\"key\": \"own_input\"",
         DocumentError::NotCanonical,
         "$.master.children[0].effects[0].key"},
        {"\"key\": \"0000000000000003\"",
         "\"key\": \"0000000000000009\"",
         DocumentError::Invalid,
         "$.master.children[0].effects[0].key"},
        {"\"key\": \"0000000000000003\"",
         "\"key\": \"0000000000000002\"",
         DocumentError::Invalid,
         "$.master.children[0].effects[0].key"},
        // Keyed by its parent, which waits on it: a loop.
        {"\"key\": \"0000000000000003\"", "\"key\": \"0000000000000001\"", DocumentError::Invalid, "$.master"},
        {"\"decay\": 2.5", "\"decay\": 0.05", DocumentError::Invalid, "$.master.children[2].effects[0].decay"},
        {"\"decay\": 2.5,\n            ", "", DocumentError::Invalid, "$.master.children[2].effects[0].decay"},
        {"\"preDelay\": 0.03",
         "\"preDelay\": 0.02",
         DocumentError::NotCanonical,
         "$.master.children[2].effects[0].preDelay"},
        {"\"damping\": 0.25", "\"damping\": 1.5", DocumentError::Invalid, "$.master.children[2].effects[0].damping"},
        {"\"mix\": 1", "\"mix\": 0.3", DocumentError::NotCanonical, "$.master.children[2].effects[0].mix"},
    };
    RAWFRAME_EXPECT(refusalOf(std::string{kEffects}) == std::pair(DocumentError{}, std::string{}));
    for (const Case& each : kCases) {
        const auto kRefusal = refusalOf(with(each.from, each.to, kEffects));
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
    const auto kTooMany = refusalOf(std::string{kEffects}, {.maximumEqBands = 2});
    RAWFRAME_EXPECT(kTooMany.first == DocumentError::Invalid && kTooMany.second == "$.master.effects[0].bands");
}
