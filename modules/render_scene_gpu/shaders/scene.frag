// The 3D scene's models (D284), fragment entry "fs": the base color lit,
// through ADR-0031's lit shading model (D299), by the sun, where the sun's
// shadow map says it reaches (D289); by
// the point and spot lights of its cluster (D290), each where its squares
// of the punctual shadows' atlas say it reaches (D292); and by the sky
// (brighter facing up, and seen in reflection); in physical units, times the exposure the device
// holds (the camera's, or its metering's, D293), so the scene target holds
// pre-exposed scene-linear light (ADR-0047). And how far the point moved
// on the screen since the frame before, for the temporal pass (D291).

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
    // The view and projection without the jitter, and the frame before's
    // taking this frame's places (D291).
    mat4 unjittered;
    mat4 previous;
}
frame;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inPlaced;
layout(location = 3) in vec3 inNow;
layout(location = 4) in vec3 inBefore;

layout(set = 0, binding = 1) uniform texture2D shadowMap;
layout(set = 0, binding = 2) uniform samplerShadow shadowSampler;

// A point or spot light (D290): its place relative to the eye and its
// range; its intensity in candela, and whether it is a spot; the way a spot
// shines; the cosines of its cone's inner and outer edges; and its first
// square of the shadows' atlas and how many, none without shadows (D292).
struct Light
{
    vec4 placeRange;
    vec4 intensity;
    vec4 direction;
    vec4 cone;
    vec4 shadow;
};

// A square of the punctual shadows' atlas (D292): where it lies in the
// atlas and its side, as fractions of the atlas, and its near plane; the
// light's axes across, up, and ahead, with how wide it sees; and the
// light's place, with a texel's width a meter ahead.
struct ShadowSlot
{
    vec4 rect;
    vec4 right;
    vec4 up;
    vec4 forward;
    vec4 position;
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

// The exposure the device holds (D293): its EV100 and the factor it
// scales light by, a metered camera's moved from frame to frame.
layout(set = 0, binding = 8, std430) readonly buffer Exposure
{
    vec4 value;
}
exposure;

// The punctual shadows' atlas, and its squares.
layout(set = 0, binding = 6) uniform texture2D lightShadowMap;
layout(set = 0, binding = 7, std430) readonly buffer Slots
{
    ShadowSlot slots[];
};

layout(location = 0) out vec4 outColor;
// Where the point is on the target less where it was, in the target's
// coordinates, rows top first.
layout(location = 1) out vec2 outMotion;

const float kPi = 3.14159265;

// The surface every model has until materials give models their own
// (ADR-0031, D299): OpenPBR's defaults, a dielectric of index 1.5, which
// reflects four hundredths head on, with a specular roughness of 0.3.
const float kRoughness = 0.3;
const vec3 kHeadOn = vec3(0.04);

// Of the light arriving from `toward`, per unit of illuminance, what leaves
// toward the eye, times the cosine it arrives at: glTF's metallic-roughness
// BRDF, the base color's Lambert diffuse under Schlick's Fresnel, and GGX
// with Smith's height-correlated visibility.
vec3 reflected(vec3 base, vec3 normal, vec3 toEye, vec3 toward)
{
    const float kNl = max(dot(normal, toward), 0.0);
    if (kNl <= 0.0) {
        return vec3(0.0);
    }
    const vec3 kHalf = normalize(toEye + toward);
    const float kNv = max(dot(normal, toEye), 1e-4);
    const float kNh = max(dot(normal, kHalf), 0.0);
    const float kAlpha = kRoughness * kRoughness;
    const float kAlpha2 = kAlpha * kAlpha;
    const vec3 kFresnel = kHeadOn + (1.0 - kHeadOn) * pow(1.0 - max(dot(toEye, kHalf), 0.0), 5.0);
    const float kSpread = kNh * kNh * (kAlpha2 - 1.0) + 1.0;
    const float kDistribution = kAlpha2 / (kPi * kSpread * kSpread);
    const float kVisibility = 0.5 / (kNl * sqrt(kNv * kNv * (1.0 - kAlpha2) + kAlpha2) +
                                     kNv * sqrt(kNl * kNl * (1.0 - kAlpha2) + kAlpha2));
    return ((1.0 - kFresnel) * base / kPi + kFresnel * (kDistribution * kVisibility)) * kNl;
}

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

// How much of a light reaches `placed` through its square of the atlas:
// the point moved along its normal by a texel and a half there, then
// compared, nearer the light being greater (reversed-Z), four texels
// blended, kept within the square.
float lightShadow(ShadowSlot slot, vec3 placed, vec3 normal)
{
    const float kNear = slot.rect.w;
    const float kAway = max(dot(placed - slot.position.xyz, slot.forward.xyz), kNear);
    const vec3 kFrom = placed + normal * (slot.position.w * kAway * 1.5) - slot.position.xyz;
    const float kAhead = dot(kFrom, slot.forward.xyz);
    if (kAhead <= kNear) {
        return 1.0;
    }
    const vec2 kSeen = vec2(dot(kFrom, slot.right.xyz), dot(kFrom, slot.up.xyz)) / (kAhead * slot.right.w);
    const float kHalfTexel = slot.position.w * 0.25 / slot.right.w;
    const vec2 kInSquare = clamp(vec2(kSeen.x * 0.5 + 0.5, 0.5 - kSeen.y * 0.5), vec2(kHalfTexel), vec2(1.0 - kHalfTexel));
    const vec2 kInAtlas = slot.rect.xy + kInSquare * slot.rect.z;
    return textureLod(sampler2DShadow(lightShadowMap, shadowSampler), vec3(kInAtlas, kNear / kAhead), 0.0);
}

// The illuminance the point and spot lights of `placed`'s cluster give it:
// the cluster found by where the view puts it and how far ahead it is;
// each light's inverse square windowed to nought at its range, a spot's
// also faded across its cone's edge.
vec3 punctual(vec3 placed, vec3 normal, vec3 base, vec3 toEye)
{
    if (frame.clusterGrid.w == 0.0) {
        return vec3(0.0);
    }
    const vec4 kClip = frame.unjittered * vec4(placed, 1.0);
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
        if (kLight.shadow.y > 0.5 && falloff > 0.0) {
            // A point's face by the way the surface lies from it: +X, -X,
            // +Y, -Y, +Z, -Z.
            uint slot = uint(kLight.shadow.x);
            if (kLight.shadow.y > 1.5) {
                const vec3 kAxes = abs(kToLight);
                slot += kAxes.x >= kAxes.y && kAxes.x >= kAxes.z ? (kToLight.x <= 0.0 ? 0u : 1u)
                        : kAxes.y >= kAxes.z                     ? (kToLight.y <= 0.0 ? 2u : 3u)
                                                                 : (kToLight.z <= 0.0 ? 4u : 5u);
            }
            falloff *= lightShadow(slots[slot], placed, normal);
        }
        sum += kLight.intensity.rgb * falloff * reflected(base, normal, toEye, kToward);
    }
    return sum;
}

void main()
{
    const vec3 kNormal = normalize(inNormal);
    const vec3 kToEye = normalize(-inPlaced);
    const vec3 kBase = inColor.rgb;
    const vec3 kDirect = frame.sun.rgb * sunlit(inPlaced, kNormal) * reflected(kBase, kNormal, kToEye, frame.toSun.xyz) +
                         punctual(inPlaced, kNormal, kBase, kToEye);
    // The sky: its light from above the normal, diffused, and from along
    // the reflection, the Fresnel of a rough surface (reflection probes
    // replace this, ADR-0051).
    const float kNv = max(dot(kNormal, kToEye), 0.0);
    const vec3 kSheen = kHeadOn + (max(vec3(1.0 - kRoughness), kHeadOn) - kHeadOn) * pow(1.0 - kNv, 5.0);
    const vec3 kMirrored = reflect(-kToEye, kNormal);
    const vec3 kSky = frame.sky.rgb * ((1.0 - kSheen) * kBase * (0.5 + 0.5 * kNormal.y) + kSheen * (0.5 + 0.5 * kMirrored.y));
    outColor = vec4((kDirect + kSky) * exposure.value.y, 1.0);
    outMotion = (inNow.xy / inNow.z - inBefore.xy / inBefore.z) * vec2(0.5, -0.5);
}
