// The UI's boxes (SPEC-0032, D375), fragment entry "fs": a rounded box by
// its signed distance, its edge smoothed over a pixel, its border inside
// it in the color of the side nearest, over its fill, inside its clip and
// each clip that clip is inside (or outside one, inverted, D377);
// premultiplied, in linear light.

#version 450

struct Box
{
    vec4 rect;
    vec4 radii;
    vec4 fill;
    vec4 widths;
    vec4 borders[4];
    // Its clip's index in the clips, nought for none.
    vec4 clip;
};

// A clip: its rounded rectangle, its radii, and its parent's index and
// whether it is inverted.
struct Clip
{
    vec4 rect;
    vec4 radii;
    vec4 link;
};

layout(set = 0, binding = 1, std430) readonly buffer Boxes
{
    Box boxes[];
}
list;

layout(set = 0, binding = 2, std430) readonly buffer Clips
{
    Clip clips[];
}
table;

// The deepest chain of clips followed; the list's builder keeps parents
// before their children, so a chain ends.
const int kDeepestClip = 64;

layout(location = 0) in vec2 inPixel;
layout(location = 1) flat in uint inBox;

layout(location = 0) out vec4 outColor;

// The distance from a rounded rectangle (x, y, width, height; radii top
// left, then clockwise), negative inside.
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
    // Read in place: a copy of the whole box does not link (its array).
    vec4 rect = list.boxes[inBox].rect;
    vec4 radii = list.boxes[inBox].radii;
    vec4 widths = list.boxes[inBox].widths;
    float outer = coverage(distanceTo(inPixel, rect, radii));
    // Widths top, right, bottom, left.
    vec4 inner = vec4(rect.x + widths.w,
                      rect.y + widths.x,
                      rect.z - widths.w - widths.y,
                      rect.w - widths.x - widths.z);
    vec4 innerRadii = max(radii - vec4(max(widths.w, widths.x),
                                           max(widths.x, widths.y),
                                           max(widths.y, widths.z),
                                           max(widths.z, widths.w)),
                          vec4(0.0));
    float filled = inner.z > 0.0 && inner.w > 0.0 ? coverage(distanceTo(inPixel, inner, innerRadii)) : 0.0;
    // The side nearest, as a share of its width.
    vec4 reach = vec4(inPixel.y - rect.y,
                      rect.x + rect.z - inPixel.x,
                      rect.y + rect.w - inPixel.y,
                      inPixel.x - rect.x) /
                 max(widths, vec4(1e-4));
    int side = 0;
    for (int at = 1; at < 4; ++at)
    {
        if (reach[at] < reach[side])
        {
            side = at;
        }
    }
    vec4 color = mix(list.boxes[inBox].borders[side], list.boxes[inBox].fill, filled) * outer;
    int clip = int(list.boxes[inBox].clip.x);
    for (int depth = 0; depth < kDeepestClip && clip > 0; ++depth)
    {
        float kept = coverage(distanceTo(inPixel, table.clips[clip].rect, table.clips[clip].radii));
        color *= table.clips[clip].link.y > 0.5 ? 1.0 - kept : kept;
        clip = int(table.clips[clip].link.x);
    }
    outColor = color;
}
