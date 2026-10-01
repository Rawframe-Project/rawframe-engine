// The UI's images (SPEC-0032, D378), fragment entry "imageFs": the image's
// part (`uv`) over its rectangle, stretched, or in nine slices whose
// corners keep their size at one picture pixel for each of the image's a
// logical pixel, shrunk together where the rectangle is too small for
// them; multiplied by its tint, premultiplied, inside every clip above it.

#version 450

struct Image
{
    vec4 rect;
    vec4 uv;
    vec4 slice;
    vec4 tint;
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

layout(set = 0, binding = 3, std430) readonly buffer Images
{
    Image images[];
}
shown;

layout(set = 0, binding = 4) uniform texture2D picture;
layout(set = 0, binding = 5) uniform sampler pictureSampler;

layout(location = 0) in vec2 inPixel;
layout(location = 1) flat in uint inImage;

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

// Where along one axis of the image, in its pixels, a point `along` the
// rectangle's `extent` falls: its first `before` and last `after` image
// pixels each `scale` picture pixels, the middle stretched.
float sliced(float along, float extent, float size, float before, float after, float scale)
{
    float first = before * scale;
    float last = after * scale;
    if (first + last <= 0.0)
    {
        return along / max(extent, 1e-4) * size;
    }
    float shrink = min(1.0, extent / (first + last));
    first *= shrink;
    last *= shrink;
    if (along < first)
    {
        return along / first * before;
    }
    if (along > extent - last)
    {
        return size - (extent - along) / max(last, 1e-4) * after;
    }
    return before + (along - first) / max(extent - first - last, 1e-4) * (size - before - after);
}

void main()
{
    vec4 rect = shown.images[inImage].rect;
    vec4 uv = shown.images[inImage].uv;
    vec4 slice = shown.images[inImage].slice;
    vec4 clip = shown.images[inImage].clip;
    vec2 size = max(clip.yz, vec2(1.0));
    // Slices top, right, bottom, left.
    vec2 texel = vec2(sliced(inPixel.x - rect.x, rect.z, size.x, slice.w, slice.y, clip.w),
                      sliced(inPixel.y - rect.y, rect.w, size.y, slice.x, slice.z, clip.w));
    vec2 at = uv.xy + texel / size * uv.zw;
    vec4 color = texture(sampler2D(picture, pictureSampler), at);
    color = vec4(color.rgb * color.a, color.a) * shown.images[inImage].tint;
    int link = int(clip.x);
    for (int depth = 0; depth < kDeepestClip && link > 0; ++depth)
    {
        float kept = coverage(distanceTo(inPixel, table.clips[link].rect, table.clips[link].radii));
        color *= table.clips[link].link.y > 0.5 ? 1.0 - kept : kept;
        link = int(table.clips[link].link.x);
    }
    outColor = color;
}
