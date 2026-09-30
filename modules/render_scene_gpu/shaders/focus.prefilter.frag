// The scene's depth of field (ADR-0051, D336), fragment entry "prefilter":
// the light at half the target's size, each texel the mean of the four it
// covers, beside its circle of confusion's radius in half-size pixels,
// signed (below nought nearer than the focus), taken at the nearest of
// the four so what stands in front keeps its blur at its edges.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D scene;
layout(set = 0, binding = 1) uniform texture2D depth;

// The circle's radius in pixels for a point infinitely far, the focus
// and the near plane in meters, and the longest radius in pixels.
layout(set = 0, binding = 4, std140) uniform Lens
{
    vec4 settings;
}
lens;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outHalf;

void main()
{
    const ivec2 kSize = textureSize(scene, 0);
    const ivec2 kFirst = ivec2(gl_FragCoord.xy) * 2;
    vec3 light = vec3(0.0);
    float nearest = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const ivec2 kAt = min(kFirst + ivec2(x, y), kSize - 1);
            light += texelFetch(scene, kAt, 0).rgb;
            nearest = max(nearest, texelFetch(depth, kAt, 0).r);
        }
    }
    const float kAhead = lens.settings.z / max(nearest, 1e-7);
    const float kRadius = lens.settings.x * (kAhead - lens.settings.y) / kAhead;
    outHalf = vec4(light * 0.25, clamp(kRadius, -lens.settings.w, lens.settings.w) * 0.5);
}
