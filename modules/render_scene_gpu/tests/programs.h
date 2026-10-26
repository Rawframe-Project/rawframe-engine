#pragma once

// A program material for the scene's tests, from containers
// tools/gen_shaders.py built as the cook builds a game's.

#include "rawframe/material/material.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::scene_fixture {

/// A program material of `shading`, the scene's and the shadow's container
/// the build's driver reads in each place, which is the one the renderer
/// takes.
inline std::shared_ptr<const material::ProgramMaterial>
programOf(std::span<const std::uint8_t> scene, std::span<const std::uint8_t> shadow, material::Shading shading) {
    material::ProgramMaterial made{.shading = shading};
    const auto kBytes = [](std::span<const std::uint8_t> embedded) {
        std::vector<std::byte> copied(embedded.size());
        for (std::size_t at = 0; at < embedded.size(); ++at) {
            copied[at] = static_cast<std::byte>(embedded[at]);
        }
        return copied;
    };
    made.containers.fill(kBytes(scene));
    made.shadows.fill(kBytes(shadow));
    return std::make_shared<const material::ProgramMaterial>(std::move(made));
}

/// The checkerboard's program material, unlit.

} // namespace rawframe::scene_fixture
