// The 3D scene's models (D284), fragment entry "fs": the base color lit by
// the sun (Lambert), where the sun's shadow map says it reaches (D289), by
// the point and spot lights of its cluster (D290), and by the sky (brighter
// facing up), in physical units, times the camera's exposure, so the scene
// target holds pre-exposed scene-linear light (ADR-0047).

#version 450

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 viewProjection;
    vec4 toSun;
    vec4 sun;
    vec4 sky;
    vec4 exposure;
    // The eye's forward; each cascade's far end and texel; the cascades,
    // the shadows' distance, and a cascade's side in texels (D289).
    vec4 forward;
    vec4 cascadeFar;
    vec4 cascadeTexel;
    vec4 shadow;
    mat4 cascades[4];
    // The clusters' tiles across and down, their slices, and the lights;
    // their near end, and the slices over the log of their far over near
    // (D290).
    vec4 clusterGrid;
    vec4 clusterDepth;
}
frame;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inPlaced;

layout(set = 0, binding = 1) uniform texture2D shadowMap;
layout(set = 0, binding = 2) uniform samplerShadow shadowSampler;

// A point or spot light (D290): its place relative to the eye and its
// range; its intensity in candela, and whether it is a spot; the way a spot
// shines; and the cosines of its cone's inner and outer edges.
struct Light
{
    vec4 placeRange;
    vec4 intensity;
    vec4 direction;
    vec4 cone;
};

// The frame's lights; each cluster's first index and count; the indices.
layout(set = 0, binding = 3, std430) readonly buffer Lights
{
    Light lights[];
};
layout(set = 0, binding = 4, std430) readonly buffer Ranges
{
    uvec2 ranges[];
};
layout(set = 0, binding = 5, std430) readonly buffer Indices
{
    uint indices[];
};

layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265;

// How much of the sun reaches `placed`: its cascade chosen by how far ahead
// it is, the point moved along its normal by a texel and a half of it (the
// normal bias), and the map compared there, nearer the sun being greater
// (reversed-Z), four texels blended (hardware 2x2 PCF). Past the shadows'
// distance the sun reaches everything, fading in over its last tenth.
float sunlit(vec3 placed, vec3 normal)
{
    const int kCount = int(frame.shadow.x);
    const float kAhead = dot(placed, frame.forward.xyz);
    if (kCount == 0 || kAhead > frame.shadow.y) {
        return 1.0;
    }
    int at = 0;
    while (at < kCount - 1 && kAhead > frame.cascadeFar[at]) {
        at += 1;
    }
    const vec4 kClip = frame.cascades[at] * vec4(placed + normal * (frame.cascadeTexel[at] * 1.5), 1.0);
    // Within the cascade's square, kept half a texel from its edges so the
    // four texels blended are its own; the squares tile the map two by two.
    const float kHalfTexel = 0.5 / frame.shadow.z;
    const vec2 kInSquare = clamp(vec2(kClip.x * 0.5 + 0.5, 0.5 - kClip.y * 0.5), vec2(kHalfTexel), vec2(1.0 - kHalfTexel));
    const vec2 kInMap = (kInSquare + vec2(float(at % 2), float(at / 2))) * 0.5;
    const float kLit = textureLod(sampler2DShadow(shadowMap, shadowSampler), vec3(kInMap, kClip.z), 0.0);
    const float kFade = clamp((frame.shadow.y - kAhead) / (0.1 * frame.shadow.y), 0.0, 1.0);
    return mix(1.0, kLit, kFade);
}

// The illuminance the point and spot lights of `placed`'s cluster give it:
// the cluster found by where the view puts it and how far ahead it is;
// each light's inverse square windowed to nought at its range, a spot's
// also faded across its cone's edge.
vec3 punctual(vec3 placed, vec3 normal)
{
    if (frame.clusterGrid.w == 0.0) {
        return vec3(0.0);
    }
    const vec4 kClip = frame.viewProjection * vec4(placed, 1.0);
    const vec2 kSeen = kClip.xy / kClip.w;
    const uvec3 kGrid = uvec3(frame.clusterGrid.xyz);
    const uint kX = min(uint(max((kSeen.x * 0.5 + 0.5) * frame.clusterGrid.x, 0.0)), kGrid.x - 1u);
    const uint kY = min(uint(max((0.5 - kSeen.y * 0.5) * frame.clusterGrid.y, 0.0)), kGrid.y - 1u);
    const float kAhead = dot(placed, frame.forward.xyz);
    const uint kSlice = kAhead <= frame.clusterDepth.x
                            ? 0u
                            : min(uint(log(kAhead / frame.clusterDepth.x) * frame.clusterDepth.y), kGrid.z - 1u);
    const uvec2 kRange = ranges[(kSlice * kGrid.y + kY) * kGrid.x + kX];
    vec3 sum = vec3(0.0);
    for (uint at = kRange.x; at < kRange.x + kRange.y; ++at) {
        const Light kLight = lights[indices[at]];
        const vec3 kToLight = kLight.placeRange.xyz - placed;
        const float kSquare = dot(kToLight, kToLight);
        const vec3 kToward = kToLight * inversesqrt(max(kSquare, 1e-8));
        const float kReached = kSquare / (kLight.placeRange.w * kLight.placeRange.w);
        const float kWindow = clamp(1.0 - kReached * kReached, 0.0, 1.0);
        float falloff = kWindow * kWindow / max(kSquare, 1e-4);
        if (kLight.intensity.w > 0.5) {
            const float kCone = clamp((dot(-kToward, kLight.direction.xyz) - kLight.cone.y) /
                                          (kLight.cone.x - kLight.cone.y),
                                      0.0,
                                      1.0);
            falloff *= kCone * kCone;
        }
        sum += kLight.intensity.rgb * (falloff * max(dot(normal, kToward), 0.0));
    }
    return sum;
}

void main()
{
    const vec3 kNormal = normalize(inNormal);
    const float kFacing = max(dot(kNormal, frame.toSun.xyz), 0.0) * sunlit(inPlaced, kNormal);
    const vec3 kLight = (frame.sun.rgb * kFacing + punctual(inPlaced, kNormal)) / kPi +
                        frame.sky.rgb * (0.5 + 0.5 * kNormal.y);
    outColor = vec4(inColor.rgb * kLight * frame.exposure.x, 1.0);
}
