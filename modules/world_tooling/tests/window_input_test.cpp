// What an author does in a preview an editor shows (D790): events read
// whole and written back the same, and hostile ones refused whole.

#include "rawframe/document/json.h"
#include "rawframe/test/test.h"
#include "rawframe/world_tooling/window_input.h"

using namespace rawframe;
using world_tooling::WindowInput;

RAWFRAME_TEST(WindowInputsReadAndWriteBack) {
    const auto kParsed =
        document::parse(R"([{"type":"move","x":12.5,"y":40},{"type":"press","x":12.5,"y":40,"button":1,"modifiers":2},)"
                        R"({"type":"wheel","deltaX":0,"deltaY":-1},{"type":"key_down","usage":26,"repeat":true},)"
                        R"({"type":"text","text":"w"},{"type":"leave"}])");
    RAWFRAME_EXPECT(kParsed.has_value());
    if (!kParsed.has_value()) {
        return;
    }
    const auto kEvents = world_tooling::windowInputsOf(*kParsed);
    RAWFRAME_EXPECT(kEvents.has_value() && kEvents->size() == 6);
    if (!kEvents.has_value() || kEvents->size() != 6) {
        return;
    }
    RAWFRAME_EXPECT((*kEvents)[1].kind == WindowInput::Kind::Press && (*kEvents)[1].button == 1 &&
                    (*kEvents)[1].modifiers == 2 && (*kEvents)[1].x == 12.5F);
    RAWFRAME_EXPECT((*kEvents)[3].usage == 26 && (*kEvents)[3].repeat && (*kEvents)[4].text == "w");
    const auto kAgain = world_tooling::windowInputsOf(world_tooling::windowInputsValue(*kEvents));
    RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == *kEvents);
}

RAWFRAME_TEST(HostileWindowInputsAreRefusedWhole) {
    for (const char* kHostile : {R"([{"type":"press","x":1,"y":1,"button":3}])",
                                 R"([{"type":"move","x":1e9,"y":0}])",
                                 R"([{"type":"key_down","usage":70000}])",
                                 R"([{"type":"teleport"}])",
                                 R"([{"type":"text","text":""}])",
                                 R"([{"type":"move","x":1,"y":1},3])",
                                 R"({"type":"move"})"}) {
        const auto kParsed = document::parse(kHostile);
        RAWFRAME_EXPECT(kParsed.has_value() && !world_tooling::windowInputsOf(*kParsed).has_value());
    }
    std::string many = "[";
    for (std::size_t each = 0; each <= world_tooling::kMostWindowInputs; ++each) {
        many += std::string{each == 0 ? "" : ","} + R"({"type":"leave"})";
    }
    const auto kMany = document::parse(many + "]");
    RAWFRAME_EXPECT(kMany.has_value() && !world_tooling::windowInputsOf(*kMany).has_value());
}
