#pragma once

// The components of `rawframe.model` as C++ reads them (D283 onward): the
// records a game's Kest program lays out, checked against its layout when
// its scene loads, which the scene's CPU half (scene.h) copies out of a
// World.

#include <cstdint>

namespace rawframe::render_scene {

/// `rawframe.model.Model` as C++ reads it; checked against the program's
/// layout when a game's scene loads.
struct Model {
    std::uint64_t mesh = 0;
    float scaleX = 1;
    float scaleY = 1;
    float scaleZ = 1;
    std::uint32_t color = 0xFFFFFFFF;
    std::uint64_t material = 0;
};

/// `rawframe.model.Camera` as C++ reads it.
struct Camera {
    float offsetX = 0;
    float offsetY = 0;
    float offsetZ = 0;
    float yaw = 0;
    float pitch = 0;
    float fovY = 0;
    float near = 0;
    float exposure = 0;
    std::uint32_t tonemapper = 0;
};

/// `rawframe.model.View` as C++ reads it (ADR-0052, D361): the render
/// texture its entity's camera draws into, its order among the views
/// naming it, and the update it asks for, where the texture is drawn on
/// demand.
struct View {
    std::uint64_t target = 0;
    std::int32_t order = 0;
    std::uint32_t request = 0;
};

/// ADR-0047's closed tonemapper set, as a camera names it (D295): AgX, the
/// default; Khronos PBR Neutral; and linear, for measuring.
enum class Tonemapper : std::uint8_t {
    Agx = 0,
    PbrNeutral = 1,
    Linear = 2
};

/// `rawframe.model.AmbientOcclusion` as C++ reads it (D327).
struct AmbientOcclusion {
    float radius = 0;
    float intensity = 0;
};

/// `rawframe.model.Bloom` as C++ reads it (D328).
struct Bloom {
    float intensity = 0;
};

/// `rawframe.model.ScreenSpaceReflections` as C++ reads it (D331).
struct ScreenSpaceReflections {
    float distance = 0;
};

/// `rawframe.model.MotionBlur` as C++ reads it (D334).
struct MotionBlur {
    float shutter = 0;
};

/// `rawframe.model.DepthOfField` as C++ reads it (D336).
struct DepthOfField {
    float focus = 0;
    float aperture = 0;
};

/// `rawframe.model.ContactShadows` as C++ reads it (D338).
struct ContactShadows {
    float length = 0;
};

/// `rawframe.model.Grading` as C++ reads it.
struct Grading {
    float slopeR = 1;
    float slopeG = 1;
    float slopeB = 1;
    float offsetR = 0;
    float offsetG = 0;
    float offsetB = 0;
    float powerR = 1;
    float powerG = 1;
    float powerB = 1;
    float saturation = 1;
    float contrast = 1;
    float temperature = 0;
    float tint = 0;
    /// A grading table, a game texture; nought for none (D344).
    std::uint64_t table = 0;
};

/// `rawframe.model.AutoExposure` as C++ reads it.
struct AutoExposure {
    float minimum = 0;
    float maximum = 0;
    float brighten = 0;
    float darken = 0;
    float compensation = 0;
    float low = 0;
    float high = 0;
    /// How much more the middle of the picture counts (D345).
    float centered = 0;
};

/// `rawframe.model.Sun` as C++ reads it.
struct Sun {
    float directionX = 0;
    float directionY = -1;
    float directionZ = 0;
    float illuminance = 0;
    std::uint32_t color = 0xFFFFFFFF;
    /// The angle its disc spans, in radians (D347).
    float angle = 0;
};

/// `rawframe.model.Sky` as C++ reads it.
struct Sky {
    float luminance = 0;
    std::uint32_t color = 0xFFFFFFFF;
    std::uint32_t ground = 0;
    std::uint64_t environment = 0;
};

/// `rawframe.model.Decal` as C++ reads it (D339).
struct Decal {
    float halfX = 0;
    float halfY = 0;
    float halfZ = 0;
    std::uint32_t color = 0xFFFFFFFF;
    std::uint64_t texture = 0;
    /// The texture bending the normals it covers, nought for none; the
    /// roughness it lays, nought for the surface's (D342).
    std::uint64_t normal = 0;
    float roughness = 0;
};

/// `rawframe.model.PostProcess` as C++ reads it (D349).
struct PostProcess {
    std::uint64_t material = 0;
    float weight = 0;
};

/// `rawframe.model.ReflectionProbe` as C++ reads it (D325).
struct ReflectionProbe {
    float halfX = 0;
    float halfY = 0;
    float halfZ = 0;
    float intensity = 0;
    std::uint32_t priority = 0;
    std::uint64_t environment = 0;
};

/// `rawframe.model.PointLight` as C++ reads it.
struct PointLight {
    float lumens = 0;
    float range = 0;
    std::uint32_t color = 0xFFFFFFFF;
    /// Whether it casts shadows, within the view's budget (D292).
    bool shadows = false;
};

/// `rawframe.model.SpotLight` as C++ reads it.
struct SpotLight {
    float lumens = 0;
    float range = 0;
    float inner = 0;
    float outer = 0;
    std::uint32_t color = 0xFFFFFFFF;
    bool shadows = false;
};

} // namespace rawframe::render_scene
