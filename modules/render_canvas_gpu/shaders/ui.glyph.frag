// The UI's glyphs (SPEC-0032, D398), fragment entry "glyphFs": the glyph's
// coverage, read texel for pixel from the atlas (its image lies on the
// picture's pixels), times its run's color, premultiplied, inside every
// clip above it. Coverage is linear in the area covered and blended in
// linear light as it is.

#version 450

struct Glyph
{
    vec4 rect;
    vec4 atlas;
    vec4 color;
    vec4 clip;
};

struct Clip
{
    vec4 rect;
    vec4 radii;
    vec4 link;
};

layout(set = 0, binding = 2, std430) readonly buffer Clips
{
    Clip clips[];
}
table;

layout(set = 0, binding = 8, std430) readonly buffer Glyphs
{
    Glyph glyphs[];
}
shown;

layout(set = 0, binding = 4) uniform texture2D picture;
layout(set = 0, binding = 5) uniform sampler pictureSampler;

layout(location = 0) in vec2 inPixel;
layout(location = 1) flat in uint inGlyph;

layout(location = 0) out vec4 outColor;

const int kDeepestClip = 64;

float distanceTo(vec2 pixel, vec4 rect, vec4 radii)
{
    vec2 halfSize = rect.zw * 0.5;
    vec2 at = pixel - (rect.xy + halfSize);
    float radius = at.x < 0.0 ? (at.y < 0.0 ? radii.x : radii.w) : (at.y < 0.0 ? radii.y : radii.z);
    vec2 past = abs(at) - halfSize + vec2(radius);
    return min(max(past.x, past.y), 0.0) + length(max(past, vec2(0.0))) - radius;
}

float coverage(float distance)
{
    return clamp(0.5 - distance, 0.0, 1.0);
}

void main()
{
    vec4 rect = shown.glyphs[inGlyph].rect;
    vec4 atlas = shown.glyphs[inGlyph].atlas;
    vec2 within = clamp(floor(inPixel - rect.xy), vec2(0.0), max(atlas.zw - 1.0, vec2(0.0)));
    float covered = texelFetch(sampler2D(picture, pictureSampler), ivec2(atlas.xy + within), 0).r;
    vec4 color = shown.glyphs[inGlyph].color * covered;
    int link = int(shown.glyphs[inGlyph].clip.x);
    for (int depth = 0; depth < kDeepestClip && link > 0; ++depth)
    {
        float kept = coverage(distanceTo(inPixel, table.clips[link].rect, table.clips[link].radii));
        color *= table.clips[link].link.y > 0.5 ? 1.0 - kept : kept;
        link = int(table.clips[link].link.x);
    }
    outColor = color;
}
