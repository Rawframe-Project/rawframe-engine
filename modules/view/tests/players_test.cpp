// The local players' views (D367): a view placed in the window by its
// region and the window's logical size, a player without one having none,
// the scene's and the canvas's views kept apart, and a window without size
// giving views that every verb refuses.

#include "rawframe/test/test.h"
#include "rawframe/view/players.h"

using namespace rawframe;

RAWFRAME_TEST(AViewIsPlacedByItsRegionAndTheWindowsSize) {
    view::PlayerViews views;
    const view::Region kRight{.left = 0.5F, .top = 0, .width = 0.5F, .height = 1};
    views.tell(1, kRight, view::Orthographic{.middle = {3, 4}, .height = 20});
    // Before the window says its size, the view has none.
    const auto kSizeless = views.orthographic(1);
    RAWFRAME_EXPECT(kSizeless.has_value() && kSizeless->size.width == 0);
    if (kSizeless.has_value()) {
        RAWFRAME_EXPECT(view::pointToWorld(kSizeless->camera, kSizeless->size, 0, 0).error() ==
                        view::Failure::NoViewSize);
    }
    views.window({.width = 1280, .height = 720});
    const auto kPlaced = views.orthographic(1);
    RAWFRAME_EXPECT(kPlaced.has_value() && kPlaced->left == 640 && kPlaced->top == 0 && kPlaced->size.width == 640 &&
                    kPlaced->size.height == 720 && kPlaced->camera.height == 20);
    // No perspective view was told; the player before has no view, nor
    // one past the last.
    RAWFRAME_EXPECT(!views.perspective(1).has_value());
    RAWFRAME_EXPECT(!views.orthographic(0).has_value() && !views.orthographic(4).has_value());
    // The scene's view and the canvas's are kept apart.
    views.tell(1, kRight, view::Perspective{.eye = {0, 2, 0}});
    views.forgetOrthographic(1);
    views.forgetPerspective(9);
    RAWFRAME_EXPECT(!views.orthographic(1).has_value() && views.perspective(1).has_value());
    views.forgetPerspective(1);
    RAWFRAME_EXPECT(!views.perspective(1).has_value());
}
