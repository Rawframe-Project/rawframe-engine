// The 3D scene's models (D284), fragment entry "fs": the surface its
// material gives it (D303, ADR-0031's blob; the model's color tinting the
// base color), lit through ADR-0031's lit shading model (D299) by the sun, where the sun's
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
    // The ground's luminance below the horizon (D304).
    vec4 ground;
}
frame;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inPlaced;
layout(location = 3) in vec3 inNow;
layout(location = 4) in vec3 inBefore;
// The model's material's place among the frame's materials.
layout(location = 5) flat in uint inMaterial;
layout(location = 6) in vec2 inUv;

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
// Every material's blob (D303), eight vectors each: the base color and
// metalness; the specular color times its weight, and the roughness; the
// emission in nits, and the index of refraction; the opacity, the
// occlusion, the alpha cutoff, and its flags: one when unlit, two when its
// base texture's color multiplies the base color, four when its alpha
// multiplies the opacity; the base, packed, and emission textures' scale
// and offset (D311); the packed texture's channels for the metalness, the
// roughness, and the occlusion, and whether the emission texture's color
// multiplies the emission (D312).
// The texture the draw's material samples, and how (D309): white for one
// sampling none.
layout(set = 0, binding = 9, std430) readonly buffer Materials
{
    vec4 materials[];
};

layout(set = 0, binding = 10) uniform texture2D baseTexture;
layout(set = 0, binding = 11) uniform sampler baseSampler;
// The packed and the emission textures (D312).
layout(set = 0, binding = 12) uniform texture2D packedTexture;
layout(set = 0, binding = 13) uniform sampler packedSampler;
layout(set = 0, binding = 14) uniform texture2D emissionTexture;
layout(set = 0, binding = 15) uniform sampler emissionSampler;

// A texture's channel a number is read from: one to four, red to alpha;
// nought for none, which reads one.
float channelOf(vec4 texel, float channel)
{
    return channel < 0.5 ? 1.0 : channel < 1.5 ? texel.r : channel < 2.5 ? texel.g : channel < 3.5 ? texel.b : texel.a;
}

layout(set = 0, binding = 7, std430) readonly buffer Slots
{
    ShadowSlot slots[];
};

layout(location = 0) out vec4 outColor;
// Where the point is on the target less where it was, in the target's
// coordinates, rows top first.
layout(location = 1) out vec2 outMotion;

const float kPi = 3.14159265;

// A point's surface as the BRDF takes it: the diffuse color (the base
// color less what metal takes), what it reflects head on (a dielectric's
// by its index of refraction and specular color, or a metal's base color),
// and its roughness.
struct Surface {
    vec3 diffuse;
    vec3 headOn;
    float roughness;
};

// Of the light arriving from `toward`, per unit of illuminance, what leaves
// toward the eye, times the cosine it arrives at: glTF's metallic-roughness
// BRDF, the diffuse color's Lambert under Schlick's Fresnel, and GGX with
// Smith's height-correlated visibility.
vec3 reflected(Surface surface, vec3 normal, vec3 toEye, vec3 toward)
{
    const float kNl = max(dot(normal, toward), 0.0);
    if (kNl <= 0.0) {
        return vec3(0.0);
    }
    const vec3 kHalf = normalize(toEye + toward);
    const float kNv = max(dot(normal, toEye), 1e-4);
    const float kNh = max(dot(normal, kHalf), 0.0);
    const float kAlpha = max(surface.roughness * surface.roughness, 1e-3);
    const float kAlpha2 = kAlpha * kAlpha;
    const vec3 kFresnel = surface.headOn + (1.0 - surface.headOn) * pow(1.0 - max(dot(toEye, kHalf), 0.0), 5.0);
    const float kSpread = kNh * kNh * (kAlpha2 - 1.0) + 1.0;
    const float kDistribution = kAlpha2 / (kPi * kSpread * kSpread);
    const float kVisibility = 0.5 / (kNl * sqrt(kNv * kNv * (1.0 - kAlpha2) + kAlpha2) +
                                     kNv * sqrt(kNl * kNl * (1.0 - kAlpha2) + kAlpha2));
    return ((1.0 - kFresnel) * surface.diffuse / kPi + kFresnel * (kDistribution * kVisibility)) * kNl;
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
vec3 punctual(vec3 placed, vec3 normal, Surface surface, vec3 toEye)
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
        sum += kLight.intensity.rgb * falloff * reflected(surface, normal, toEye, kToward);
    }
    return sum;
}

void main()
{
    const vec3 kNormal = normalize(inNormal);
    const vec3 kToEye = normalize(-inPlaced);
    const uint kAt = min(inMaterial, uint(materials.length()) / 8u - 1u) * 8u;
    const vec4 kBase = materials[kAt];
    const vec4 kSpecular = materials[kAt + 1u];
    const vec4 kEmission = materials[kAt + 2u];
    const vec4 kRest = materials[kAt + 3u];
    // Where each texture is sampled (D311), and what the packed and the
    // emission textures feed (D312).
    const vec4 kBaseMap = materials[kAt + 4u];
    const vec4 kPackedMap = materials[kAt + 5u];
    const vec4 kEmissionMap = materials[kAt + 6u];
    const vec4 kChannels = materials[kAt + 7u];
    const uint kFlags = uint(kRest.w);
    // The coordinates' derivatives, taken before anything branches, so a
    // texture is sampled only where its material has one.
    const vec2 kDx = dFdx(inUv);
    const vec2 kDy = dFdy(inUv);
    vec4 sampled = vec4(1.0);
    if ((kFlags & 6u) != 0u) {
        sampled = textureGrad(sampler2D(baseTexture, baseSampler), inUv * kBaseMap.xy + kBaseMap.zw,
                              kDx * kBaseMap.xy, kDy * kBaseMap.xy);
    }
    vec4 packed = vec4(1.0);
    if (kChannels.x + kChannels.y + kChannels.z > 0.5) {
        packed = textureGrad(sampler2D(packedTexture, packedSampler), inUv * kPackedMap.xy + kPackedMap.zw,
                             kDx * kPackedMap.xy, kDy * kPackedMap.xy);
    }
    vec3 glow = vec3(1.0);
    if (kChannels.w > 0.5) {
        glow = textureGrad(sampler2D(emissionTexture, emissionSampler), inUv * kEmissionMap.xy + kEmissionMap.zw,
                           kDx * kEmissionMap.xy, kDy * kEmissionMap.xy).rgb;
    }
    const vec3 kColor = inColor.rgb * kBase.rgb * ((kFlags & 2u) != 0u ? sampled.rgb : vec3(1.0));
    // How much of what is behind it a translucent model hides (D305): its
    // material's opacity times its color's alpha.
    const float kOpacity = kRest.x * inColor.a * ((kFlags & 4u) != 0u ? sampled.a : 1.0);
    const float kMetalness = kBase.w * channelOf(packed, kChannels.x);
    const float kRoughness = kSpecular.w * channelOf(packed, kChannels.y);
    const float kOcclusion = kRest.y * channelOf(packed, kChannels.z);
    const vec3 kGlow = kEmission.rgb * glow;
    outMotion = (inNow.xy / inNow.z - inBefore.xy / inBefore.z) * vec2(0.5, -0.5);
    // Unlit (KHR_materials_unlit): its color stands in the picture as it
    // is, whatever the exposure.
    if ((kFlags & 1u) != 0u) {
        outColor = vec4(kColor, kOpacity);
        return;
    }
    const float kReflectance = (kEmission.w - 1.0) / (kEmission.w + 1.0);
    const Surface kSurface = Surface(kColor * (1.0 - kMetalness),
                                     mix(kReflectance * kReflectance * kSpecular.rgb, kColor, kMetalness),
                                     kRoughness);
    const vec3 kDirect = frame.sun.rgb * sunlit(inPlaced, kNormal) * reflected(kSurface, kNormal, kToEye, frame.toSun.xyz) +
                         punctual(inPlaced, kNormal, kSurface, kToEye);
    // The sky above and the ground below (D304): their light across the
    // normal's side, diffused, and along the reflection, the Fresnel of a
    // rough surface (reflection probes replace this, ADR-0051); what the
    // material occludes of both.
    const float kNv = max(dot(kNormal, kToEye), 0.0);
    const vec3 kSheen = kSurface.headOn + (max(vec3(1.0 - kSurface.roughness), kSurface.headOn) - kSurface.headOn) *
                                              pow(1.0 - kNv, 5.0);
    const vec3 kMirrored = reflect(-kToEye, kNormal);
    const vec3 kAround = mix(frame.ground.rgb, frame.sky.rgb, 0.5 + 0.5 * kNormal.y);
    const vec3 kAlong = mix(frame.ground.rgb, frame.sky.rgb, 0.5 + 0.5 * kMirrored.y);
    const vec3 kSky = kOcclusion * ((1.0 - kSheen) * kSurface.diffuse * kAround + kSheen * kAlong);
    outColor = vec4((kDirect + kSky + kGlow) * exposure.value.y, kOpacity);
}
