#pragma once

// What the scene's GPU tests share (D322): the device opened, a view as
// the queue stage makes it, boxes, and a frame drawn and read back.

#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::scene_fixture {

/// Whether a device must answer: skipping is failing (the check sets it).
inline bool required() {
    const char* value = std::getenv("RAWFRAME_REQUIRE_GPU");
    return value != nullptr && value[0] != '\0';
}

/// The one device, opened on any adapter, software too; none where no
/// adapter answers.
inline std::unique_ptr<render::Device> opened() {
    auto device = render::Device::request({.allowSoftware = true});
    if (!device.has_value()) {
        return nullptr;
    }
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = (*device)->open();
        if (!kOpen.has_value()) {
            RAWFRAME_EXPECT(!required());
            std::puts("skip: no adapter");
            return nullptr;
        }
        if (*kOpen) {
            return std::move(*device);
        }
    }
    return nullptr;
}

/// The target's side, in pixels.
inline constexpr std::uint32_t kSide = 64;

/// A view from the origin along -Z, a square field a quarter turn high, as
/// the queue stage would make it, with the engine's default light; not
/// antialiased over time unless asked, so a frame is drawn alone.
inline render_scene::SceneFrame looking(bool temporal = false) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    render_scene::SceneFrame made = scene->queue({.fovY = 1.5707964F, .near = 0.1F, .exposure = 15, .aspect = 1});
    made.temporal.enabled = temporal;
    return made;
}

/// A box of half sides `half` at `z` meters ahead, in sRGB `color`.
inline render_scene::SceneDraw
box(float z, float half, std::array<float, 4> color, std::uint64_t mesh = render_scene::kBox) {
    render_scene::SceneDraw draw{.mesh = mesh, .color = color};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        draw.model[(axis * 4) + axis] = half;
        draw.normal[(axis * 4) + axis] = 1 / half;
    }
    draw.model[14] = -z;
    draw.model[15] = 1;
    return draw;
}

/// A pixel's red, green, and blue.
inline std::array<int, 3> at(const std::vector<std::byte>& pixels, std::uint32_t x, std::uint32_t y) {
    const std::size_t kAt = (std::size_t{y} * kSide + x) * 4;
    return {std::to_integer<int>(pixels[kAt]),
            std::to_integer<int>(pixels[kAt + 1]),
            std::to_integer<int>(pixels[kAt + 2])};
}

/// One frame drawn and read back through `render`'s framer (D285); the
/// pipelines are made as the device answers, a few frames at most.
inline std::optional<std::vector<std::byte>> drawn(render::Framer& framer,
                                                   render_scene_gpu::SceneRenderer& renderer,
                                                   const render_scene::SceneFrame& frame,
                                                   const render_scene_gpu::MeshSource& meshes,
                                                   const render_scene_gpu::TextureSource& textures = {}) {
    const std::array<render::FrameRecorder*, 1> kRecorders = {&renderer};
    const std::uint64_t kBefore = renderer.statistics().frames;
    for (int attempt = 0; attempt < 1000 && renderer.statistics().frames == kBefore; ++attempt) {
        renderer.prepare(&frame, meshes, textures);
        RAWFRAME_EXPECT(framer.finish(5'000'000'000).has_value());
        const auto kMade = framer.make(kRecorders, {.width = kSide, .height = kSide, .readBack = true});
        RAWFRAME_EXPECT(kMade.has_value());
        if (!kMade.has_value()) {
            std::printf("%s\n", std::string{kMade.error().description()}.c_str());
            for (const auto& each : kMade.error().context()) {
                std::printf("  %s: %s\n", std::string{each.key}.c_str(), std::string{each.value}.c_str());
            }
            return std::nullopt;
        }
    }
    RAWFRAME_EXPECT(framer.finish(5'000'000'000).has_value());
    return framer.pixels();
}

/// One frame drawn and read back once the renderer draws what `counted`
/// counts: an effect's pipelines are asked for when a frame first wants
/// it, and the frames before the device answers go without it (D337).
inline std::optional<std::vector<std::byte>> drawnWith(render::Framer& framer,
                                                       render_scene_gpu::SceneRenderer& renderer,
                                                       const render_scene::SceneFrame& frame,
                                                       const render_scene_gpu::MeshSource& meshes,
                                                       std::uint64_t render_scene_gpu::RendererStatistics::* counted,
                                                       const render_scene_gpu::TextureSource& textures = {}) {
    const std::uint64_t kBefore = renderer.statistics().*counted;
    std::optional<std::vector<std::byte>> pixels;
    for (int attempt = 0; attempt < 1000 && renderer.statistics().*counted == kBefore; ++attempt) {
        pixels = drawn(framer, renderer, frame, meshes, textures);
    }
    return pixels;
}

} // namespace rawframe::scene_fixture
