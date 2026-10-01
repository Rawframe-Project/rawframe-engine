// The UI's shadows (SPEC-0032, D381), fragment entry "shadowFs", as CSS's
// box-shadow: the box offset and grown by the spread (shrunk, inset), its
// corners with it, blurred by a Gaussian of half the blur's deviation (a
// closed form across, four samples down, after Evan Wallace's rounded-box
// shadows), outside the box, or inside it inset; premultiplied, inside
// every clip above it.

#version 450

struct Shadow
{
    vec4 rect;
    vec4 radii;
    vec4 color;
    vec4 shape;
    vec4 flags;
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

layout(set = 0, binding = 6, std430) readonly buffer Shadows
{
    Shadow shadows[];
}
thrown;

layout(location = 0) in vec2 inPixel;
layout(location = 1) flat in uint inShadow;

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

vec2 errorFunction(vec2 x)
{
    vec2 sign = sign(x);
    vec2 magnitude = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (magnitude * magnitude)) * magnitude) * magnitude;
    x *= x;
    return sign - sign / (x * x);
}

float gaussian(float x, float sigma)
{
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (2.5066283 * sigma);
}

// How much of a row `y` from the box's center, `halfSize` with corners of
// `corner`, a blur of `sigma` spreads to `x` from the center.
float across(float x, float y, float sigma, float corner, vec2 halfSize)
{
    float delta = min(halfSize.y - corner - abs(y), 0.0);
    float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec2 integral = 0.5 + 0.5 * errorFunction((x + vec2(-curved, curved)) * (0.70710678 / sigma));
    return integral.y - integral.x;
}

// The blurred box's cover of `pixel`: nought to one.
float blurred(vec2 pixel, vec4 rect, vec4 radii, float sigma)
{
    if (sigma < 0.25)
    {
        return coverage(distanceTo(pixel, rect, radii));
    }
    vec2 halfSize = rect.zw * 0.5;
    vec2 at = pixel - (rect.xy + halfSize);
    float corner = at.x < 0.0 ? (at.y < 0.0 ? radii.x : radii.w) : (at.y < 0.0 ? radii.y : radii.z);
    corner = min(corner, min(halfSize.x, halfSize.y));
    float low = at.y - halfSize.y;
    float high = at.y + halfSize.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);
    float step = (end - start) / 4.0;
    float y = start + step * 0.5;
    float value = 0.0;
    for (int row = 0; row < 4; ++row)
    {
        value += across(at.x, at.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

void main()
{
    vec4 rect = thrown.shadows[inShadow].rect;
    vec4 radii = thrown.shadows[inShadow].radii;
    vec4 shape = thrown.shadows[inShadow].shape;
    vec4 flags = thrown.shadows[inShadow].flags;
    float sigma = shape.z * 0.5;
    float inside = coverage(distanceTo(inPixel, rect, radii));
    float value;
    if (flags.x < 0.5)
    {
        vec4 grown = vec4(rect.xy + shape.xy - vec2(shape.w), rect.zw + vec2(2.0 * shape.w));
        value = blurred(inPixel, grown, max(radii + vec4(shape.w), vec4(0.0)), sigma) * (1.0 - inside);
    }
    else
    {
        vec4 shrunk = vec4(rect.xy + shape.xy + vec2(shape.w), max(rect.zw - vec2(2.0 * shape.w), vec2(0.0)));
        value = inside * (1.0 - blurred(inPixel, shrunk, max(radii - vec4(shape.w), vec4(0.0)), sigma));
    }
    vec4 color = thrown.shadows[inShadow].color * value;
    int link = int(flags.y);
    for (int depth = 0; depth < kDeepestClip && link > 0; ++depth)
    {
        float kept = coverage(distanceTo(inPixel, table.clips[link].rect, table.clips[link].radii));
        color *= table.clips[link].link.y > 0.5 ? 1.0 - kept : kept;
        link = int(table.clips[link].link.x);
    }
    outColor = color;
}
