#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::scene_bake {

/// Contributes the World-scoped participant `rawframe.scene_bake.probes`
/// (D326), for a Tool-role process only: with `bake.directory` set, once
/// `bake.after` frames were presented it bakes every reflection probe the
/// scene extracted, drawing the scene six ways from its box's middle at
/// `bake.exposure` (EV100, 10 unless set) through
/// `rawframe.render_scene_gpu.captures`, and writes each probe's picture,
/// `bake.width` texels across (512 unless set), as the Radiance file its
/// environment texture names, under the directory. It logs each probe
/// baked and a summary when the World stops.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::scene_bake
