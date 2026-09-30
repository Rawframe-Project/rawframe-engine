#include "blocks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

/// The light an EV100 exposes to one (ADR-0047): a sensor's saturation at
/// 1.2 times two to the EV100.
float factorOf(float ev100) noexcept {
    return 1.0F / (1.2F * std::exp2(ev100));
}

} // namespace

ExposureBlock exposureOf(float ev100) noexcept {
    return {.value = {ev100, factorOf(ev100), 0, 0}};
}

PictureBlock pictureOf(const render_scene::SceneFrame& frame) noexcept {
    const render_scene::SceneGrading& kGrade = frame.grading;
    PictureBlock block;
    for (std::size_t row = 0; row < 3; ++row) {
        block.balance[row] = {kGrade.balance[row * 3], kGrade.balance[(row * 3) + 1], kGrade.balance[(row * 3) + 2], 0};
    }
    block.slope = {kGrade.slope[0], kGrade.slope[1], kGrade.slope[2], kGrade.saturation};
    block.offset = {kGrade.offset[0], kGrade.offset[1], kGrade.offset[2], kGrade.contrast};
    block.power = {kGrade.power[0], kGrade.power[1], kGrade.power[2], kGrade.enabled ? 1.0F : 0.0F};
    // What middle grey (0.18) is scaled by for each operator to come out
    // as AgX's 0.2145 (ADR-0047's normalization across operators).
    switch (frame.tonemapper) {
    case render_scene::Tonemapper::Agx:
        block.tonemapper = {0, 1, 0, 0};
        break;
    case render_scene::Tonemapper::PbrNeutral:
        block.tonemapper = {1, 1.41371F, 0, 0};
        break;
    case render_scene::Tonemapper::Linear:
        block.tonemapper = {2, 1.19149F, 0, 0};
        break;
    }
    block.display = {frame.dither ? 1.0F : 0.0F, 0, 0, 0};
    return block;
}

MeterBlock meterOf(const render_scene::SceneFrame& frame) noexcept {
    const render_scene::SceneMetering& kMetering = frame.metering;
    const render_scene::AutoExposure& kAsked = kMetering.settings;
    return {.bounds = {kAsked.minimum, kAsked.maximum, kAsked.brighten, kAsked.darken},
            .fractions = {kAsked.low, kAsked.high, kAsked.compensation, kMetering.elapsed},
            .snap = {kMetering.snap ? 1.0F : 0.0F, 0, 0, 0}};
}

FrameBlock blockOf(const render_scene::SceneFrame& frame, std::uint32_t width, std::uint32_t height) noexcept {
    FrameBlock block;
    // The projection times the view, column-major; jittered, a point ahead
    // moves right by twice the jitter over the width in clip space, and
    // down likewise, as the distance ahead divides it. A frame not
    // antialiased over time is never jittered.
    render_scene::Matrix jittered = frame.projection;
    if (frame.temporal.enabled) {
        jittered[8] -= 2 * frame.temporal.jitter[0] / static_cast<float>(std::max<std::uint32_t>(width, 1));
        jittered[9] += 2 * frame.temporal.jitter[1] / static_cast<float>(std::max<std::uint32_t>(height, 1));
    }
    for (const auto& [kProjection, kInto] :
         {std::pair{static_cast<const render_scene::Matrix*>(&jittered), &block.viewProjection},
          std::pair{&frame.projection, &block.unjittered}}) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) {
                float sum = 0;
                for (std::size_t k = 0; k < 4; ++k) {
                    sum += (*kProjection)[(k * 4) + row] * frame.view[(column * 4) + k];
                }
                (*kInto)[(column * 4) + row] = sum;
            }
        }
    }
    block.previous = frame.temporal.enabled ? frame.temporal.previousViewProjection : block.unjittered;
    const render_scene::SceneLights& kLights = frame.lights;
    block.toSun = {kLights.toSun[0], kLights.toSun[1], kLights.toSun[2], 0};
    block.sun = {kLights.sun[0], kLights.sun[1], kLights.sun[2], 0};
    block.sky = {kLights.sky[0], kLights.sky[1], kLights.sky[2], 0};
    block.ground = {kLights.ground[0], kLights.ground[1], kLights.ground[2], 0};
    block.exposure = {factorOf(frame.exposure), 0, 0, 0};
    block.occlusion = {frame.occlusion.enabled ? 1.0F : 0.0F, 0, 0, 0};
    block.forward = {frame.forward[0], frame.forward[1], frame.forward[2], 0};
    const render_scene::SceneShadows& kShadows = frame.shadows;
    for (std::size_t at = 0; at < kShadows.count; ++at) {
        block.cascadeFar[at] = kShadows.cascades[at].far;
        block.cascadeTexel[at] = kShadows.cascades[at].texel;
        block.cascades[at] = kShadows.cascades[at].viewProjection;
    }
    block.shadow = {static_cast<float>(kShadows.count),
                    kShadows.distance,
                    static_cast<float>(std::max<std::uint32_t>(kShadows.side, 1)),
                    kShadows.filter == render_scene::ShadowFilter::Soft ? 1.0F : 0.0F};
    const render_scene::SceneClusters& kClusters = frame.clusters;
    const bool kClustered =
        !frame.lights3d.empty() && kClusters.near > 0 && kClusters.far > kClusters.near &&
        kClusters.ranges.size() == std::size_t{kClusters.tilesX} * kClusters.tilesY * kClusters.slices * 2;
    block.clusterGrid = {static_cast<float>(kClusters.tilesX),
                         static_cast<float>(kClusters.tilesY),
                         static_cast<float>(kClusters.slices),
                         kClustered ? static_cast<float>(frame.lights3d.size()) : 0};
    block.clusterDepth = {kClusters.near,
                          kClustered ? static_cast<float>(kClusters.slices) / std::log(kClusters.far / kClusters.near)
                                     : 0,
                          0,
                          0};
    return block;
}

std::vector<LightBlock> lightsOf(const render_scene::SceneFrame& frame) {
    std::vector<LightBlock> made;
    for (const render_scene::SceneLight& light : frame.lights3d) {
        made.push_back(
            {.placeRange = {light.position[0], light.position[1], light.position[2], light.range},
             .intensity = {light.intensity[0], light.intensity[1], light.intensity[2], light.spot ? 1.0F : 0.0F},
             .direction = {light.direction[0], light.direction[1], light.direction[2], 0},
             .cone = {light.cosInner, light.cosOuter, 0, 0},
             .shadow = {static_cast<float>(light.shadowSlot), static_cast<float>(light.shadowSlots), 0, 0}});
    }
    if (made.empty()) {
        made.emplace_back();
    }
    return made;
}

std::vector<SlotBlock> slotsOf(const render_scene::SceneFrame& frame) {
    std::vector<SlotBlock> made;
    const render_scene::SceneLightShadows& kShadows = frame.lightShadows;
    const auto kAtlas = static_cast<float>(std::max<std::uint32_t>(kShadows.side, 1));
    for (const render_scene::ShadowSlot& slot : kShadows.slots) {
        const auto kSide = static_cast<float>(std::max<std::uint32_t>(slot.side, 1));
        made.push_back({.rect = {static_cast<float>(slot.x) / kAtlas,
                                 static_cast<float>(slot.y) / kAtlas,
                                 kSide / kAtlas,
                                 slot.near},
                        .right = {slot.right[0], slot.right[1], slot.right[2], slot.tangent},
                        .up = {slot.up[0], slot.up[1], slot.up[2], 0},
                        .forward = {slot.forward[0], slot.forward[1], slot.forward[2], 0},
                        .position = {slot.position[0], slot.position[1], slot.position[2], 2 * slot.tangent / kSide}});
    }
    if (made.empty()) {
        made.emplace_back();
    }
    return made;
}

std::vector<std::array<float, 4>> tangentsOf(const mesh::Mesh& made, std::span<const mesh::Vector3> normals) {
    using Vector = std::array<double, 3>;
    const auto kMinus = [](const Vector& left, const Vector& right) {
        return Vector{left[0] - right[0], left[1] - right[1], left[2] - right[2]};
    };
    const auto kDot = [](const Vector& left, const Vector& right) {
        return (left[0] * right[0]) + (left[1] * right[1]) + (left[2] * right[2]);
    };
    const auto kCross = [](const Vector& left, const Vector& right) {
        return Vector{(left[1] * right[2]) - (left[2] * right[1]),
                      (left[2] * right[0]) - (left[0] * right[2]),
                      (left[0] * right[1]) - (left[1] * right[0])};
    };
    const auto kWide = [](const auto& value) {
        return Vector{value[0], value[1], value[2]};
    };
    // Each face's direction of rising u and of falling v (up the image,
    // glTF's normal-map green), weighted by the face's size in both.
    std::vector<Vector> alongU(made.positions.size(), Vector{0, 0, 0});
    std::vector<Vector> upV(made.positions.size(), Vector{0, 0, 0});
    if (made.uvs.size() == made.positions.size()) {
        for (std::size_t at = 0; at + 2 < made.indices.size(); at += 3) {
            const std::uint32_t kA = made.indices[at];
            const std::uint32_t kB = made.indices[at + 1];
            const std::uint32_t kC = made.indices[at + 2];
            const Vector kE1 = kMinus(kWide(made.positions[kB]), kWide(made.positions[kA]));
            const Vector kE2 = kMinus(kWide(made.positions[kC]), kWide(made.positions[kA]));
            const double kDu1 = made.uvs[kB][0] - made.uvs[kA][0];
            const double kDv1 = made.uvs[kB][1] - made.uvs[kA][1];
            const double kDu2 = made.uvs[kC][0] - made.uvs[kA][0];
            const double kDv2 = made.uvs[kC][1] - made.uvs[kA][1];
            const double kArea = (kDu1 * kDv2) - (kDu2 * kDv1);
            if (kArea == 0 || !std::isfinite(kArea)) {
                continue;
            }
            const double kR = 1 / kArea;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double kU = ((kE1[axis] * kDv2) - (kE2[axis] * kDv1)) * kR;
                const double kV = ((kE2[axis] * kDu1) - (kE1[axis] * kDu2)) * kR;
                for (const std::uint32_t kCorner : {kA, kB, kC}) {
                    alongU[kCorner][axis] += kU;
                    upV[kCorner][axis] -= kV;
                }
            }
        }
    }
    std::vector<std::array<float, 4>> tangents(made.positions.size());
    for (std::size_t at = 0; at < made.positions.size(); ++at) {
        Vector normal = kWide(normals[at]);
        const double kLength = std::sqrt(kDot(normal, normal));
        normal = kLength > 0 ? Vector{normal[0] / kLength, normal[1] / kLength, normal[2] / kLength} : Vector{0, 1, 0};
        // Across the normal: rising u, or, with none, any direction across.
        Vector across = kMinus(alongU[at],
                               Vector{normal[0] * kDot(normal, alongU[at]),
                                      normal[1] * kDot(normal, alongU[at]),
                                      normal[2] * kDot(normal, alongU[at])});
        if (kDot(across, across) < 1e-20) {
            across = kCross(std::abs(normal[1]) < 0.9 ? Vector{0, 1, 0} : Vector{1, 0, 0}, normal);
        }
        const double kAcross = std::sqrt(kDot(across, across));
        const double kSide = kDot(kCross(normal, across), upV[at]) < 0 ? -1 : 1;
        tangents[at] = {static_cast<float>(across[0] / kAcross),
                        static_cast<float>(across[1] / kAcross),
                        static_cast<float>(across[2] / kAcross),
                        static_cast<float>(kSide)};
    }
    return tangents;
}

std::vector<float> verticesOf(const mesh::Mesh& made) {
    std::vector<mesh::Vector3> normals = made.normals;
    if (normals.size() != made.positions.size()) {
        normals.assign(made.positions.size(), mesh::Vector3{0, 0, 0});
        for (std::size_t at = 0; at + 2 < made.indices.size(); at += 3) {
            const mesh::Vector3& kA = made.positions[made.indices[at]];
            const mesh::Vector3& kB = made.positions[made.indices[at + 1]];
            const mesh::Vector3& kC = made.positions[made.indices[at + 2]];
            const mesh::Vector3 kAb = {kB[0] - kA[0], kB[1] - kA[1], kB[2] - kA[2]};
            const mesh::Vector3 kAc = {kC[0] - kA[0], kC[1] - kA[1], kC[2] - kA[2]};
            // Weighted by the face's area, as its cross product is.
            const mesh::Vector3 kFace = {(kAb[1] * kAc[2]) - (kAb[2] * kAc[1]),
                                         (kAb[2] * kAc[0]) - (kAb[0] * kAc[2]),
                                         (kAb[0] * kAc[1]) - (kAb[1] * kAc[0])};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                mesh::Vector3& normal = normals[made.indices[at + corner]];
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    normal[axis] += kFace[axis];
                }
            }
        }
    }
    const bool kMapped = made.uvs.size() == made.positions.size();
    const std::vector<std::array<float, 4>> kTangents = tangentsOf(made, normals);
    std::vector<float> vertices;
    vertices.reserve(made.positions.size() * (kVertexBytes / sizeof(float)));
    for (std::size_t at = 0; at < made.positions.size(); ++at) {
        vertices.insert(vertices.end(), made.positions[at].begin(), made.positions[at].end());
        vertices.insert(vertices.end(), normals[at].begin(), normals[at].end());
        vertices.push_back(kMapped ? made.uvs[at][0] : 0.0F);
        vertices.push_back(kMapped ? made.uvs[at][1] : 0.0F);
        vertices.insert(vertices.end(), kTangents[at].begin(), kTangents[at].end());
    }
    return vertices;
}

std::uint64_t bytesOf(const mesh::Mesh& made) noexcept {
    return (std::uint64_t{made.positions.size()} * kVertexBytes) + (std::uint64_t{made.indices.size()} * 4);
}

} // namespace rawframe::render_scene_gpu
