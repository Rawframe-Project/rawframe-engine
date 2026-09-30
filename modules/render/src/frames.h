#pragma once

#include "rawframe/composition/registrar.h"

namespace rawframe::render {

/// Contributes `rawframe.render.frame` (frames.cpp); registrar.h's
/// `registerParticipants` calls it.
void registerFrames(composition::ParticipantRegistrar& registrar) noexcept;

} // namespace rawframe::render
