// The scene's ambient occlusion (ADR-0051, D327), fragment entry "occlude":
// for each point the depth prepass left, twelve points of the hemisphere
// above it within the radius, turned about its normal by one of sixteen
// angles in each four-by-four tile, each find the surface the prepass left
// where they are seen. A surface found above the point's own plane hides
// it as much as it rises toward the normal, less the farther it is, and
// not at all past the radius (the Alchemy form): a flat floor seen
// grazing hides nothing of itself. What is not hidden, taken the
// intensity times, is the light from all around that reaches the point.
// The sky, where nothing lies, is not occluded. It is found at half the
// target's size, each texel for the point at its top left.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D depth;
layout(set = 0, binding = 1) uniform texture2D surfaces;

// The jittered view's inverse, from clip space to the eye-relative World,
// and the view itself; the radius, the intensity, and the near plane.
layout(set = 0, binding = 2, std140) uniform Occlusion
{
    mat4 toPoint;
    mat4 viewProjection;
    vec4 settings;
}
occlusion;

layout(location = 0) in vec2 inUv;

layout(location = 0) out float outOcclusion;

// Points of the hemisphere about +Z within one, denser near the middle.
const vec3 kKernel[12] = vec3[](
    vec3(0.2020, 0.0000, 0.0381),
    vec3(-0.1584, 0.1451, 0.0569),
    vec3(0.0207, -0.2353, 0.0818),
    vec3(0.1613, 0.2103, 0.1150),
    vec3(-0.2948, -0.0521, 0.1589),
    vec3(0.2842, -0.1808, 0.2158),
    vec3(-0.0971, 0.3612, 0.2883),
    vec3(-0.1874, -0.3609, 0.3785),
    vec3(0.4024, 0.1470, 0.4889),
    vec3(-0.3968, 0.1638, 0.6218),
    vec3(0.1658, -0.3544, 0.7795),
    vec3(0.0789, 0.2517, 0.9646));

void main()
{
    // Read for the interface alone (D418): Direct3D 12 links stages by place.
    const float kInterface = inUv.x;
    const ivec2 kHalf = ivec2(gl_FragCoord.xy);
    const ivec2 kAt = kHalf * 2;
    const float kDepth = texelFetch(depth, kAt, 0).r;
    if (kDepth <= 0.0) {
        outOcclusion = 1.0;
        return;
    }
    const vec2 kSize = vec2(textureSize(depth, 0));
    const float kRadius = occlusion.settings.x;
    const float kNear = occlusion.settings.z;
    const vec2 kMiddleHere = (vec2(kAt) + 0.5) / kSize;
    const vec4 kSeen =
        occlusion.toPoint * vec4(kMiddleHere.x * 2.0 - 1.0, 1.0 - kMiddleHere.y * 2.0, kDepth, 1.0);
    const vec3 kPoint = kSeen.xyz / kSeen.w;
    const vec3 kNormal = normalize(texelFetch(surfaces, kAt, 0).xyz);
    // A turn for each texel of a four-by-four tile, in the order of a
    // Bayer matrix, so the blur after takes all sixteen together.
    const uint kBayer[16] = uint[](0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);
    const float kTurn = float(kBayer[uint(kHalf.y & 3) * 4u + uint(kHalf.x & 3)]) * (6.28318531 / 16.0);
    const vec3 kHelper = abs(kNormal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    const vec3 kAcross = normalize(cross(kHelper, kNormal));
    const vec3 kUp = cross(kNormal, kAcross);
    const vec3 kTangent = cos(kTurn) * kAcross + sin(kTurn) * kUp;
    const vec3 kBitangent = cross(kNormal, kTangent);
    float hidden = 0.0;
    for (int at = 0; at < 12; ++at) {
        const vec3 kOffset = kKernel[at];
        const vec3 kSample = kPoint + (kTangent * kOffset.x + kBitangent * kOffset.y + kNormal * kOffset.z) * kRadius;
        const vec4 kClip = occlusion.viewProjection * vec4(kSample, 1.0);
        if (kClip.w <= kNear) {
            continue;
        }
        const vec2 kOn = vec2(kClip.x / kClip.w * 0.5 + 0.5, 0.5 - kClip.y / kClip.w * 0.5);
        if (any(lessThan(kOn, vec2(0.0))) || any(greaterThanEqual(kOn, vec2(1.0)))) {
            continue;
        }
        // The surface seen at that texel's middle.
        const ivec2 kTexel = ivec2(kOn * kSize);
        const float kThere = texelFetch(depth, kTexel, 0).r;
        if (kThere <= 0.0) {
            continue;
        }
        const vec2 kMiddle = (vec2(kTexel) + 0.5) / kSize;
        const vec4 kFound = occlusion.toPoint * vec4(kMiddle.x * 2.0 - 1.0, 1.0 - kMiddle.y * 2.0, kThere, 1.0);
        const vec3 kToward = kFound.xyz / kFound.w - kPoint;
        const float kFar = length(kToward);
        if (kFar > 1e-4 && kFar < kRadius) {
            const float kFade = 1.0 - (kFar * kFar) / (kRadius * kRadius);
            hidden += max(dot(kNormal, kToward / kFar) - 0.1, 0.0) * kFade;
        }
    }
    // Half the samples rising straight up, near, hide it all.
    outOcclusion = clamp(1.0 - occlusion.settings.y * hidden / 6.0, 0.0, 1.0);
}
