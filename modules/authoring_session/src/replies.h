#pragma once

// The records a session answers with about an open scene (D407): what an
// apply or a step did, and what is selected and where it is looked from.

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/document/json.h"

#include <cstddef>

namespace rawframe::authoring_session {

/// The outcome document `apply` writes, from a session's scene.
document::Value outcomeOf(
    const authoring::AuthoredScene& scene, bool written, bool reopened, document::Value results, std::size_t skipped);

/// The entities a scene has selected, by their ids.
document::Value selectionOf(const authoring::AuthoredScene& scene);

/// Where a scene is looked at from, null until told (D432).
document::Value viewOf(const authoring::AuthoredScene& scene);

} // namespace rawframe::authoring_session
