// Mixer layouts: SPEC-0036's `audio.mixer` documents read into one bus tree
// with sends, every rule refused at its field, and a mix order in which
// every bus follows what feeds it.

#include "rawframe/audio/layout.h"
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

std::string with(std::string_view from, std::string_view to) {
    std::string text{kLayout};
    const std::size_t kAt = text.find(from);
    RAWFRAME_EXPECT(kAt != std::string::npos);
    if (kAt != std::string::npos) {
        text.replace(kAt, from.size(), to);
    }
    return text;
}

std::pair<document::DocumentError, std::string> refusalOf(const std::string& text) {
    const auto kRead = readLayout(text);
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
        {"\"rawframe/gain@1\"", "\"rawframe/reverb@1\"", DocumentError::Invalid, "$.master.effects[0].type"},
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
