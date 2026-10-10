// A UI tree as assistive technology reads it (D571): a root's subtree given
// to a copy of the tree, its nodes' roles and names as their owner set
// them, a name changed and a node removed reaching the copy with the next
// update, and nothing given for a root no access reads.

#include "rawframe/test/test.h"
#include "rawframe/ui/access.h"
#include "rawframe/ui/errors.h"
#include "rawframe/ui/tree.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::ui;

namespace {

bool holds(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

} // namespace

RAWFRAME_TEST(ARootsRolesAndNamesReachTheCopy) {
    auto made = Tree::create(16);
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    Tree& ui = **made;
    const Node kPanel = *ui.add(1);
    const Node kTitle = *ui.add(2);
    const Node kPlay = *ui.add(3);
    for (const Node kChild : {kTitle, kPlay}) {
        RAWFRAME_EXPECT(ui.attach(kPanel, kChild).has_value());
    }
    RAWFRAME_EXPECT(ui.setRole(kPanel, Role::Dialog).has_value() && ui.setName(kPanel, "Pause").has_value());
    // A node's own text is read without a name: a label's words.
    RAWFRAME_EXPECT(ui.setText(kTitle, "Paused", {}).has_value());
    RAWFRAME_EXPECT(ui.setRole(kPlay, Role::Button).has_value() && ui.setName(kPlay, "Play on").has_value());
    RAWFRAME_EXPECT(ui.layOut(kPanel, 200, 100).has_value());

    auto access = Access::create(ui, kPanel, {});
    RAWFRAME_EXPECT(access.has_value());
    if (!access.has_value()) {
        return;
    }
    RAWFRAME_EXPECT((*access)->written().empty());
    RAWFRAME_EXPECT((*access)->update().has_value());
    const std::string kFirst = (*access)->written();
    RAWFRAME_EXPECT(holds(kFirst, "dialog") && holds(kFirst, "Pause"));
    RAWFRAME_EXPECT(holds(kFirst, "label") && holds(kFirst, "Paused"));
    RAWFRAME_EXPECT(holds(kFirst, "button") && holds(kFirst, "Play on"));
    if (!holds(kFirst, "Play on")) {
        std::fprintf(stderr, "%s\n", kFirst.c_str());
    }

    // A name changed and a node gone reach the copy with the next update.
    RAWFRAME_EXPECT(ui.setName(kPlay, "Quit").has_value());
    RAWFRAME_EXPECT(ui.remove(kTitle).has_value());
    RAWFRAME_EXPECT((*access)->update().has_value());
    const std::string kSecond = (*access)->written();
    RAWFRAME_EXPECT(holds(kSecond, "Quit") && !holds(kSecond, "Play on") && !holds(kSecond, "Paused"));
    // Nothing changed: the copy stays as it is.
    RAWFRAME_EXPECT((*access)->update().has_value() && (*access)->written() == kSecond);
}

RAWFRAME_TEST(AnAccessIsRefusedWhatItCannotTake) {
    auto made = Tree::create(4);
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    Tree& ui = **made;
    const Node kRoot = *ui.add(1);
    RAWFRAME_EXPECT(!Access::create(ui, kRoot, {.scale = 0}).has_value());
    RAWFRAME_EXPECT(!Access::create(ui, kRoot, {.nodes = 0}).has_value());
    RAWFRAME_EXPECT(!ui.setName(Node{}, "nobody").has_value());
    // Once one is let go, another may read the root again.
    {
        auto first = Access::create(ui, kRoot, {});
        RAWFRAME_EXPECT(first.has_value());
    }
    RAWFRAME_EXPECT(Access::create(ui, kRoot, {}).has_value());
    if (!accessBuilt(AccessPlatform::Atspi)) {
        const auto kRefused = Access::create(ui, kRoot, {.platform = AccessPlatform::Atspi});
        RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().code() == code(UiError::Unavailable));
    }
    // Android's, UI Automation's, AppKit's, and UIKit's need what the tree
    // lies in (D576, D579, D588), and are refused without it where they are
    // built; a copy is no platform's root.
    for (const AccessPlatform kPlatform :
         {AccessPlatform::Android, AccessPlatform::Uia, AccessPlatform::AppKit, AccessPlatform::UiKit}) {
        const auto kHostless = Access::create(ui, kRoot, {.platform = kPlatform});
        RAWFRAME_EXPECT(!kHostless.has_value() &&
                        kHostless.error().code() ==
                            code(accessBuilt(kPlatform) ? UiError::Invalid : UiError::Unavailable));
    }
    const auto kCopy = Access::create(ui, kRoot, {});
    RAWFRAME_EXPECT(kCopy.has_value() && (*kCopy)->root() == nullptr);
}

RAWFRAME_TEST(ASeatReadsItsTreeOnceOpenAndForgetsItOnLeaving) {
    auto made = Tree::create(8);
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    Tree& ui = **made;
    const Node kRoot = *ui.add(1);
    const Node kPlay = *ui.add(2);
    RAWFRAME_EXPECT(ui.attach(kRoot, kPlay).has_value());
    RAWFRAME_EXPECT(ui.setRole(kPlay, Role::Button).has_value() && ui.setName(kPlay, "Play").has_value());
    // Seated, not opened: nothing is read.
    AccessSeat seat;
    RAWFRAME_EXPECT(seat.seat(ui, kRoot).has_value() && seat.access() == nullptr && seat.update().has_value());
    // Opened: read from now on.
    RAWFRAME_EXPECT(seat.open({}).has_value() && seat.access() != nullptr);
    RAWFRAME_EXPECT(seat.update().has_value() && holds(seat.access()->written(), "Play"));
    // Gone with its tree, and read again when a tree is seated.
    seat.leave();
    RAWFRAME_EXPECT(seat.access() == nullptr && seat.update().has_value());
    RAWFRAME_EXPECT(seat.seat(ui, kRoot).has_value() && seat.access() != nullptr);
    RAWFRAME_EXPECT(seat.update().has_value() && holds(seat.access()->written(), "button"));
}

RAWFRAME_TEST(AScreenReadersPressesAndFocusesAreKeptForTheOwner) {
    auto made = Tree::create(8);
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    Tree& ui = **made;
    const Node kRoot = *ui.add(1);
    const Node kPlay = *ui.add(2);
    const Node kQuit = *ui.add(3);
    RAWFRAME_EXPECT(ui.attach(kRoot, kPlay).has_value() && ui.attach(kRoot, kQuit).has_value());
    AccessSeat seat;
    RAWFRAME_EXPECT(seat.seat(ui, kRoot).has_value() && seat.open({}).has_value() && seat.update().has_value());
    Access* access = seat.access();
    RAWFRAME_EXPECT(access != nullptr);
    if (access == nullptr) {
        return;
    }
    // Kept in their order, taken once.
    RAWFRAME_EXPECT(access->ask(AccessRequest::Kind::Focus, kQuit) && access->ask(AccessRequest::Kind::Press, kPlay));
    const std::vector<AccessRequest> kTaken = seat.takeRequests();
    RAWFRAME_EXPECT((kTaken == std::vector<AccessRequest>{{.kind = AccessRequest::Kind::Focus, .node = kQuit},
                                                          {.kind = AccessRequest::Kind::Press, .node = kPlay}}));
    RAWFRAME_EXPECT(seat.takeRequests().empty());
    // A node that is gone asks nothing.
    RAWFRAME_EXPECT(ui.remove(kQuit).has_value());
    RAWFRAME_EXPECT(!access->ask(AccessRequest::Kind::Press, kQuit) && seat.takeRequests().empty());
}

RAWFRAME_TEST(TheFocusItsOwnerGivesIsTheOneAScreenReaderFollows) {
    auto made = Tree::create(8);
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    Tree& ui = **made;
    const Node kRoot = *ui.add(1);
    const Node kPlay = *ui.add(2);
    const Node kLabel = *ui.add(3);
    RAWFRAME_EXPECT(ui.attach(kRoot, kPlay).has_value() && ui.attach(kRoot, kLabel).has_value());
    RAWFRAME_EXPECT(ui.setInteraction(kPlay, {.focusable = true}).has_value());
    RAWFRAME_EXPECT(ui.setRole(kPlay, Role::Button).has_value() && ui.setName(kPlay, "Play").has_value());
    auto access = Access::create(ui, kRoot, {});
    RAWFRAME_EXPECT(access.has_value());
    if (!access.has_value()) {
        return;
    }
    RAWFRAME_EXPECT((*access)->update().has_value() && !(*access)->focused().has_value());
    RAWFRAME_EXPECT(ui.focus(kPlay, true).has_value() && (*access)->update().has_value());
    RAWFRAME_EXPECT((*access)->focused() == kPlay);
    // A node that may not hold focus is refused it; focus taken away.
    RAWFRAME_EXPECT(!ui.focus(kLabel, true).has_value());
    RAWFRAME_EXPECT(ui.focus(std::nullopt, false).has_value() && (*access)->update().has_value());
    RAWFRAME_EXPECT(!(*access)->focused().has_value());
}
