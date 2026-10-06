// The scene's motion blur (ADR-0051, D334), fragment entry "tile": the
// longest blur within each tile of the target, a tile as many pixels
// across as the longest blur's reach (McGuire et al. 2012's TileMax). A
// point's blur is half its motion over the frame's time the shutter is
// open, in pixels, either way from it.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D scene;
layout(set = 0, binding = 1) uniform texture2D motion;
layout(set = 0, binding = 2) uniform texture2D depth;
layout(set = 0, binding = 3) uniform texture2D tiles;

// Half the shutter's share of the frame, a tile's side and the longest
// blur in pixels, and the near plane.
layout(set = 0, binding = 4, std140) uniform Blur
{
    vec4 settings;
}
blur;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec2 outLongest;

void main()
{
    // Read for the interface alone (D418): Direct3D 12 links stages by place.
    const float kInterface = inUv.x;
    const int kSide = int(blur.settings.y);
    const ivec2 kSize = textureSize(motion, 0);
    const ivec2 kFirst = ivec2(gl_FragCoord.xy) * kSide;
    vec2 longest = vec2(0.0);
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const ivec2 kAt = min(kFirst + ivec2(x, y), kSize - 1);
            const vec2 kBlur = texelFetch(motion, kAt, 0).xy * vec2(kSize) * blur.settings.x;
            if (dot(kBlur, kBlur) > dot(longest, longest)) {
                longest = kBlur;
            }
        }
    }
    const float kLength = length(longest);
    outLongest = kLength > blur.settings.y ? longest * (blur.settings.y / kLength) : longest;
}
