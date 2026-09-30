// The scene's bloom (ADR-0051, D328), fragment entry "up": a level of the
// bloom's chain doubled by a three-by-three tent and added to the level
// above, so each level holds its own light and all those below it.

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
    const vec3 kCorners = at(-1.0, -1.0) + at(1.0, -1.0) + at(-1.0, 1.0) + at(1.0, 1.0);
    const vec3 kEdges = at(0.0, -1.0) + at(-1.0, 0.0) + at(1.0, 0.0) + at(0.0, 1.0);
    outColor = vec4((at(0.0, 0.0) * 4.0 + kEdges * 2.0 + kCorners) / 16.0, 1.0);
}
