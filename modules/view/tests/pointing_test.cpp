// A finger is the UI's pointer while it touches: the first finger
// down is where the pointer is until it lifts, so the UI sees a press let go
// where the finger left it; a second finger presses but moves nothing; a
// lifted or cancelled finger leaves the window, a cancelled one before its
// press is let go.

#include "rawframe/test/test.h"
#include "rawframe/view/pointing.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

using namespace rawframe;
using Touching = view::UiPointing::Touching;

namespace {

/// What the UI was told, in order, with where the pointer was each time.
struct Told {
    view::UiPointing pointing;
    std::vector<std::string> said;

    Told() {
        pointing.onPress([this](float x, float y) {
            said.push_back("press " + std::to_string(static_cast<int>(x)) + "," + std::to_string(static_cast<int>(y)) +
                           where());
        });
        pointing.onRelease([this] {
            said.push_back("release" + where());
        });
    }

    std::string where() const {
        const std::optional<std::array<float, 2>> kAt = pointing.pointer();
        return kAt.has_value() ? " at " + std::to_string(static_cast<int>((*kAt)[0])) + "," +
                                     std::to_string(static_cast<int>((*kAt)[1]))
                               : " off";
    }
};

} // namespace

RAWFRAME_TEST(AFingerIsThePointerWhileItTouches) {
    Told told;
    told.pointing.touched(Touching::Down, 7, 10, 20);
    told.pointing.touched(Touching::Moved, 7, 30, 20);
    // A second finger presses where it is, and moves nothing.
    told.pointing.touched(Touching::Down, 8, 300, 200);
    told.pointing.touched(Touching::Moved, 8, 310, 200);
    RAWFRAME_EXPECT(told.pointing.pointer() == (std::array<float, 2>{30, 20}));
    told.pointing.touched(Touching::Up, 8, 310, 200);
    // The first lifts where it moved to, then is off the window.
    told.pointing.touched(Touching::Moved, 7, 40, 25);
    told.pointing.touched(Touching::Up, 7, 42, 25);
    RAWFRAME_EXPECT(!told.pointing.pointer().has_value());
    // The next finger down is the pointer; cancelled, it is off before
    // its press is let go.
    told.pointing.touched(Touching::Down, 9, 5, 5);
    told.pointing.touched(Touching::Cancelled, 9, 6, 6);
    const std::vector<std::string> kSaid{"press 10,20 at 10,20",
                                         "press 300,200 at 30,20",
                                         "release at 30,20",
                                         "release at 42,25",
                                         "press 5,5 at 5,5",
                                         "release off"};
    RAWFRAME_EXPECT(told.said == kSaid);
}
