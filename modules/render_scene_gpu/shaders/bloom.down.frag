// The scene's bloom (ADR-0051, D328), fragment entry "down": a level of the
// bloom's chain halved into the next by Jimenez's thirteen taps.

#version 450

layout(set = 0, binding = 0) uniform texture2D source;
layout(set = 0, binding = 1) uniform sampler blended;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

// The source's light `x` and `y` of its texels from here.
vec3 at(float x, float y)
{
    const vec2 kTexel = 1.0 / vec2(textureSize(sampler2D(source, blended), 0));
    return textureLod(sampler2D(source, blended), inUv + vec2(x, y) * kTexel, 0.0).rgb;
}

void main()
{
    const vec3 kCorners = at(-2.0, -2.0) + at(2.0, -2.0) + at(-2.0, 2.0) + at(2.0, 2.0);
    const vec3 kEdges = at(0.0, -2.0) + at(-2.0, 0.0) + at(2.0, 0.0) + at(0.0, 2.0);
    const vec3 kInner = at(-1.0, -1.0) + at(1.0, -1.0) + at(-1.0, 1.0) + at(1.0, 1.0);
    outColor = vec4(at(0.0, 0.0) * 0.125 + kCorners * 0.03125 + kEdges * 0.0625 + kInner * 0.125, 1.0);
}
