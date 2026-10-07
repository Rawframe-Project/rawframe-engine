// Lines a scene shows (D464): each a ribbon of two points about the eye,
// on the material after the frame's own, the unsound left out, and none
// once none are shown.

#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"
#include "scene_rig.h"

#include <array>

using namespace rawframe;
using namespace rawframe::render_scene;
using namespace rawframe::render_scene::test_rig;

RAWFRAME_TEST(LinesShownAreRibbonsAboutTheEye) {
    Rig rig;
    // Two lines (D464), and one that is not sound; far out, as an eye there
    // sees them.
    const std::array<SceneLine, 3> kLines = {
        SceneLine{.from = {1e7, 1, -1e7 - 2}, .to = {1e7 + 1, 1, -1e7 - 2}, .color = {1, 0, 0, 1}, .width = 0.05F},
        SceneLine{.from = {1e7, 1, -1e7 - 2}, .to = {1e7, 2, -1e7 - 2}, .color = {0, 1, 0, 1}},
        SceneLine{.from = {1e7, 1, -1e7}, .to = {1e7, 1, -1e7}, .width = 0}};
    rig.scene->show(kLines);
    const SceneFrame& kFrame = rig.frame({.eye = {1e7, 1, -1e7}});
    const auto& kRibbons = kFrame.particles.ribbons;
    const auto& kPoints = kFrame.particles.ribbonPoints;
    RAWFRAME_EXPECT(kRibbons.size() == 2 && kPoints.size() == 4);
    if (kRibbons.size() != 2 || kPoints.size() != 4) {
        return;
    }
    // On the material after the frame's own, two points each.
    RAWFRAME_EXPECT(kRibbons[0].material == kFrame.materials.size() && kRibbons[0].count == 2 &&
                    kRibbons[1].first == 2);
    RAWFRAME_EXPECT(near(kPoints[0].place[0], 0) && near(kPoints[0].place[2], -2) && near(kPoints[1].place[0], 1) &&
                    near(kPoints[3].place[1], 1));
    RAWFRAME_EXPECT(near(kPoints[0].width, 0.05F) && near(kPoints[1].color[0], 1) && near(kPoints[2].color[1], 1));
    // Shown none, none drawn.
    rig.scene->show({});
    RAWFRAME_EXPECT(rig.frame({.eye = {1e7, 1, -1e7}}).particles.ribbons.empty());
}
