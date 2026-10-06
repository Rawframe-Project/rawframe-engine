// The scene's motion blur (D334), fragment entry "neighbor": the longest
// blur among each tile and the eight around it (McGuire et al. 2012's
// NeighborMax), what any point of the tile can be reached by.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D scene;
layout(set = 0, binding = 1) uniform texture2D motion;
layout(set = 0, binding = 2) uniform texture2D depth;
layout(set = 0, binding = 3) uniform texture2D tiles;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec2 outLongest;

void main()
{
    // Read for the interface alone (D418): Direct3D 12 links stages by place.
    const float kInterface = inUv.x;
    const ivec2 kSize = textureSize(tiles, 0);
    const ivec2 kAt = ivec2(gl_FragCoord.xy);
    vec2 longest = vec2(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec2 kBlur = texelFetch(tiles, clamp(kAt + ivec2(x, y), ivec2(0), kSize - 1), 0).xy;
            if (dot(kBlur, kBlur) > dot(longest, longest)) {
                longest = kBlur;
            }
        }
    }
    outLongest = longest;
}
