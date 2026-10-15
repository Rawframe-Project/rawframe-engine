#pragma once

// What an editor's preview draws over its scene, kind by kind (D798): each
// body's collider and each light's reach as lines, so an author sees what
// the models do not show.

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/scene.h"

#include <vector>

namespace rawframe::render_scene {

/// `body`'s collider where `pose` puts it, appended to `lines`: a box's
/// twelve edges; a sphere's three great circles; a capsule's or a
/// cylinder's two end rings and four sides; nothing for a mesh. Green, a
/// sensor's cyan.
void addColliderLines(const physics3d::Body3D& body, const physics3d::Pose3D& pose, std::vector<SceneLine>& lines);

/// `light`'s reach, appended to `lines`: three circles of its range about
/// it, and a spot's way to its range and the ring its cone reaches there.
/// Yellow.
void addLightLines(const LightInstance& light, std::vector<SceneLine>& lines);

} // namespace rawframe::render_scene
