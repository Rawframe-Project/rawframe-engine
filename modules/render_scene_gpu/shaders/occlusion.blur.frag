// The scene's ambient occlusion blurred (D327), fragment entry "blur": the
// four-by-four tile about each texel averaged, so its sixteen turns count
// together, leaving out texels whose point is farther from the eye than a
// tenth again, so the occlusion does not bleed across an edge. At half the
// target's size, as it was found.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D depth;
layout(set = 0, binding = 3) uniform texture2D occluded;

layout(set = 0, binding = 2, std140) uniform Occlusion
{
    mat4 toPoint;
    mat4 viewProjection;
    vec4 settings;
}
occlusion;

layout(location = 0) in vec2 inUv;

layout(location = 0) out float outOcclusion;

void main()
{
    // Read for the interface alone (D418): Direct3D 12 links stages by place.
    const float kInterface = inUv.x;
    const ivec2 kAt = ivec2(gl_FragCoord.xy);
    const ivec2 kLast = textureSize(occluded, 0) - 1;
    const float kDepth = texelFetch(depth, kAt * 2, 0).r;
    float sum = 0.0;
    float weight = 0.0;
    for (int y = -2; y < 2; ++y) {
        for (int x = -2; x < 2; ++x) {
            const ivec2 kThere = clamp(kAt + ivec2(x, y), ivec2(0), kLast);
            const float kThereDepth = texelFetch(depth, kThere * 2, 0).r;
            // Depths, as the near plane over the distance ahead: close in
            // distance where their ratio is.
            if (abs(kThereDepth - kDepth) <= 0.1 * kDepth) {
                sum += texelFetch(occluded, kThere, 0).r;
                weight += 1.0;
            }
        }
    }
    outOcclusion = weight > 0.0 ? sum / weight : texelFetch(occluded, kAt, 0).r;
}
