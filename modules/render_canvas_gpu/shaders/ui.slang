// The UI's drawing (SPEC-0032), in Slang (ADR-0026, D475), from which the
// SPIR-V and the WGSL are compiled: boxes (D375), images (D378), shadows
// (D381), and glyphs (D398), each one quad an instance, in the picture's
// pixels from its top left, y down, inside every clip above it (D377);
// premultiplied, in linear light.
//
// An instance is numbered as Vulkan and WebGPU number it, its draw's first
// instance included (SV_VulkanInstanceID): each draw starts at its run's
// first record.

// The picture's width and height in pixels.
struct View
{
    float4 size;
}

struct Box
{
    float4 rect;
    float4 radii;
    float4 fill;
    float4 widths;
    float4 borders[4];
    // Its clip's index in the clips, and its gradient's in the gradients,
    // nought for none.
    float4 clip;
}

// A clip: its rounded rectangle, its radii, and its parent's index and
// whether it is inverted.
struct Clip
{
    float4 rect;
    float4 radii;
    float4 link;
}

struct Image
{
    float4 rect;
    float4 uv;
    float4 slice;
    float4 tint;
    // Its clip's index, its texture's width and height in its pixels, and
    // the picture's pixels a logical one.
    float4 clip;
}

struct Shadow
{
    float4 rect;
    float4 radii;
    float4 color;
    // Its offset, its blur, and its spread.
    float4 shape;
    // Whether it is inset, and its clip's index.
    float4 flags;
}

// A gradient: its kind (1 linear, 2 radial), its stops, and its angle; the
// stops' colors, linear and premultiplied; and their positions.
struct Gradient
{
    float4 head;
    float4 colors[4];
    float4 positions;
}

struct Glyph
{
    float4 rect;
    float4 atlas;
    float4 color;
    float4 clip;
}

[[vk::binding(0, 0)]] ConstantBuffer<View> view;
[[vk::binding(1, 0)]] StructuredBuffer<Box> boxes;
[[vk::binding(2, 0)]] StructuredBuffer<Clip> clips;
[[vk::binding(3, 0)]] StructuredBuffer<Image> images;
[[vk::binding(4, 0)]] Texture2D picture;
[[vk::binding(5, 0)]] SamplerState pictureSampler;
[[vk::binding(6, 0)]] StructuredBuffer<Shadow> shadows;
[[vk::binding(7, 0)]] StructuredBuffer<Gradient> gradients;
[[vk::binding(8, 0)]] StructuredBuffer<Glyph> glyphs;

// The deepest chain of clips followed; the list's builder keeps parents
// before their children, so a chain ends.
static const int kDeepestClip = 64;

// What every entry passes on: the pixel, and the record's index.
struct Corner
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 pixel;
    [[vk::location(1)]] nointerpolation uint record;
}

static const float2 kCorners[6] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1) };

// The corner `vertex` of a quad over `rect`, for record `record`.
Corner cornerOf(float4 rect, uint vertex, uint record)
{
    Corner corner;
    const float2 pixel = rect.xy + kCorners[vertex] * rect.zw;
    // Maul RHI's clip space runs up on every driver, as WebGPU's does.
    corner.position = float4(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    corner.pixel = pixel;
    corner.record = record;
    return corner;
}

// The distance from a rounded rectangle (x, y, width, height; radii top
// left, then clockwise), negative inside.
float distanceTo(float2 pixel, float4 rect, float4 radii)
{
    const float2 halfSize = rect.zw * 0.5;
    const float2 at = pixel - (rect.xy + halfSize);
    const float radius = at.x < 0.0 ? (at.y < 0.0 ? radii.x : radii.w) : (at.y < 0.0 ? radii.y : radii.z);
    const float2 past = abs(at) - halfSize + float2(radius);
    return min(max(past.x, past.y), 0.0) + length(max(past, float2(0.0))) - radius;
}

float coverage(float distance)
{
    return clamp(0.5 - distance, 0.0, 1.0);
}

// `color` inside the clip `link` and each clip that clip is inside, or
// outside one inverted.
float4 clipped(float4 color, float2 pixel, int link)
{
    for (int depth = 0; depth < kDeepestClip && link > 0; ++depth)
    {
        const float kept = coverage(distanceTo(pixel, clips[link].rect, clips[link].radii));
        color *= clips[link].link.y > 0.5 ? 1.0 - kept : kept;
        link = int(clips[link].link.x);
    }
    return color;
}

[shader("vertex")]
Corner vs(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    // A pixel past its border box on every side, so its edge is smoothed.
    const float4 rect = boxes[instance].rect;
    return cornerOf(float4(rect.xy - float2(1.0), rect.zw + float2(2.0)), vertex, instance);
}

float3 cubeRoot(float3 value)
{
    return sign(value) * pow(abs(value), float3(1.0 / 3.0));
}

// Linear sRGB to Oklab and back (Björn Ottosson's matrices). Each matrix
// is written a column to a row, as GLSL wrote it, so a vector multiplies
// it from the left.
float3 toOklab(float3 color)
{
    const float3 lms = cubeRoot(mul(color, float3x3(0.4122214708, 0.2119034982, 0.0883024619,
                                                     0.5363325363, 0.6806995451, 0.2817188376,
                                                     0.0514459929, 0.1073969566, 0.6299787005)));
    return mul(lms, float3x3(0.2104542553, 1.9779984951, 0.0259040371,
                             0.7936177850, -2.4285922050, 0.7827717662,
                             -0.0040720468, 0.4505937099, -0.8086757660));
}

float3 fromOklab(float3 lab)
{
    const float3 lms = mul(lab, float3x3(1.0, 1.0, 1.0,
                                         0.3963377774, -0.1055613458, -0.0894841775,
                                         0.2158037573, -0.0638541728, -1.2914855480));
    return mul(lms * lms * lms, float3x3(4.0767416621, -1.2684380046, -0.0041960863,
                                         -3.3077115913, 2.6097574011, -0.7034186147,
                                         0.2309699292, -0.3413193965, 1.7076147010));
}

// A premultiplied linear color as premultiplied Oklab.
float4 premultipliedOklab(float4 color)
{
    if (color.a <= 0.0)
    {
        return float4(0.0);
    }
    return float4(toOklab(color.rgb / color.a) * color.a, color.a);
}

// Gradient `index` at `pixel` of the box `rect` (D382): premultiplied,
// linear, its stops mixed in Oklab.
float4 ramp(uint index, float2 pixel, float4 rect)
{
    const float4 head = gradients[index].head;
    const float2 halfSize = rect.zw * 0.5;
    const float2 at = pixel - (rect.xy + halfSize);
    float along;
    if (head.x < 1.5)
    {
        const float angle = radians(head.z);
        const float2 toward = float2(sin(angle), -cos(angle));
        const float span = abs(rect.z * toward.x) + abs(rect.w * toward.y);
        along = dot(at, toward) / max(span, 1e-4) + 0.5;
    }
    else
    {
        along = length(at / max(halfSize * 1.41421356, float2(1e-4)));
    }
    const int stops = int(head.y);
    const float4 positions = gradients[index].positions;
    float4 lab = premultipliedOklab(gradients[index].colors[0]);
    for (int stop = 1; stop < stops; ++stop)
    {
        if (along > positions[stop - 1])
        {
            const float span = max(positions[stop] - positions[stop - 1], 1e-6);
            const float share = clamp((along - positions[stop - 1]) / span, 0.0, 1.0);
            lab = lerp(premultipliedOklab(gradients[index].colors[stop - 1]),
                       premultipliedOklab(gradients[index].colors[stop]),
                       share);
        }
    }
    if (lab.a <= 0.0)
    {
        return float4(0.0);
    }
    return float4(max(fromOklab(lab.rgb / lab.a), float3(0.0)) * lab.a, lab.a);
}

// A rounded box by its signed distance, its edge smoothed over a pixel,
// its border inside it in the color of the side nearest, over its fill and
// the gradient over that.
[shader("fragment")]
float4 fs(Corner corner) : SV_Target0
{
    const float2 pixel = corner.pixel;
    const uint box = corner.record;
    const float4 rect = boxes[box].rect;
    const float4 radii = boxes[box].radii;
    const float4 widths = boxes[box].widths;
    const float outer = coverage(distanceTo(pixel, rect, radii));
    // Widths top, right, bottom, left.
    const float4 inner = float4(rect.x + widths.w,
                                rect.y + widths.x,
                                rect.z - widths.w - widths.y,
                                rect.w - widths.x - widths.z);
    const float4 innerRadii = max(radii - float4(max(widths.w, widths.x),
                                                 max(widths.x, widths.y),
                                                 max(widths.y, widths.z),
                                                 max(widths.z, widths.w)),
                                  float4(0.0));
    const float filled = inner.z > 0.0 && inner.w > 0.0 ? coverage(distanceTo(pixel, inner, innerRadii)) : 0.0;
    // The side nearest, as a share of its width.
    const float4 reach = float4(pixel.y - rect.y,
                                rect.x + rect.z - pixel.x,
                                rect.y + rect.w - pixel.y,
                                pixel.x - rect.x) /
                         max(widths, float4(1e-4));
    int side = 0;
    for (int at = 1; at < 4; ++at)
    {
        if (reach[at] < reach[side])
        {
            side = at;
        }
    }
    float4 fill = boxes[box].fill;
    const uint gradient = uint(boxes[box].clip.y);
    if (gradient > 0u)
    {
        const float4 over = ramp(gradient, pixel, rect);
        fill = over + fill * (1.0 - over.a);
    }
    const float4 color = lerp(boxes[box].borders[side], fill, filled) * outer;
    return clipped(color, pixel, int(boxes[box].clip.x));
}

[shader("vertex")]
Corner imageVs(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    return cornerOf(images[instance].rect, vertex, instance);
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
    const float shrink = min(1.0, extent / (first + last));
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

// The image's part (`uv`) over its rectangle, stretched, or in nine slices
// whose corners keep their size at one picture pixel for each of the
// image's a logical pixel, shrunk together where the rectangle is too
// small for them; multiplied by its tint.
[shader("fragment")]
float4 imageFs(Corner corner) : SV_Target0
{
    const float2 pixel = corner.pixel;
    const uint image = corner.record;
    const float4 rect = images[image].rect;
    const float4 uv = images[image].uv;
    const float4 slice = images[image].slice;
    const float4 clip = images[image].clip;
    const float2 size = max(clip.yz, float2(1.0));
    // Slices top, right, bottom, left.
    const float2 texel = float2(sliced(pixel.x - rect.x, rect.z, size.x, slice.w, slice.y, clip.w),
                                sliced(pixel.y - rect.y, rect.w, size.y, slice.x, slice.z, clip.w));
    const float4 sampled = picture.Sample(pictureSampler, uv.xy + texel / size * uv.zw);
    const float4 color = float4(sampled.rgb * sampled.a, sampled.a) * images[image].tint;
    return clipped(color, pixel, int(clip.x));
}

// Over what a shadow can darken: an outer shadow's box offset, grown by
// its spread and three deviations of its blur, an inset one's box.
[shader("vertex")]
Corner shadowVs(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    float4 rect = shadows[instance].rect;
    const float4 shape = shadows[instance].shape;
    if (shadows[instance].flags.x < 0.5)
    {
        const float reach = max(shape.w, 0.0) + 1.5 * shape.z + 1.0;
        rect = float4(rect.xy + shape.xy - float2(reach), rect.zw + float2(2.0 * reach));
    }
    return cornerOf(rect, vertex, instance);
}

float2 errorFunction(float2 x)
{
    const float2 signs = sign(x);
    const float2 magnitude = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (magnitude * magnitude)) * magnitude) * magnitude;
    x *= x;
    return signs - signs / (x * x);
}

float gaussian(float x, float sigma)
{
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (2.5066283 * sigma);
}

// How much of a row `y` from the box's center, `halfSize` with corners of
// `corner`, a blur of `sigma` spreads to `x` from the center.
float across(float x, float y, float sigma, float corner, float2 halfSize)
{
    const float delta = min(halfSize.y - corner - abs(y), 0.0);
    const float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    const float2 integral = 0.5 + 0.5 * errorFunction((x + float2(-curved, curved)) * (0.70710678 / sigma));
    return integral.y - integral.x;
}

// The blurred box's cover of `pixel`: nought to one.
float blurred(float2 pixel, float4 rect, float4 radii, float sigma)
{
    if (sigma < 0.25)
    {
        return coverage(distanceTo(pixel, rect, radii));
    }
    const float2 halfSize = rect.zw * 0.5;
    const float2 at = pixel - (rect.xy + halfSize);
    float corner = at.x < 0.0 ? (at.y < 0.0 ? radii.x : radii.w) : (at.y < 0.0 ? radii.y : radii.z);
    corner = min(corner, min(halfSize.x, halfSize.y));
    const float low = at.y - halfSize.y;
    const float high = at.y + halfSize.y;
    const float start = clamp(-3.0 * sigma, low, high);
    const float end = clamp(3.0 * sigma, low, high);
    const float step = (end - start) / 4.0;
    float y = start + step * 0.5;
    float value = 0.0;
    for (int row = 0; row < 4; ++row)
    {
        value += across(at.x, at.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

// As CSS's box-shadow: the box offset and grown by the spread (shrunk,
// inset), its corners with it, blurred by a Gaussian of half the blur's
// deviation (a closed form across, four samples down, after Evan
// Wallace's rounded-box shadows), outside the box, or inside it inset.
[shader("fragment")]
float4 shadowFs(Corner corner) : SV_Target0
{
    const float2 pixel = corner.pixel;
    const uint shadow = corner.record;
    const float4 rect = shadows[shadow].rect;
    const float4 radii = shadows[shadow].radii;
    const float4 shape = shadows[shadow].shape;
    const float4 flags = shadows[shadow].flags;
    const float sigma = shape.z * 0.5;
    const float inside = coverage(distanceTo(pixel, rect, radii));
    float value;
    if (flags.x < 0.5)
    {
        const float4 grown = float4(rect.xy + shape.xy - float2(shape.w), rect.zw + float2(2.0 * shape.w));
        value = blurred(pixel, grown, max(radii + float4(shape.w), float4(0.0)), sigma) * (1.0 - inside);
    }
    else
    {
        const float4 shrunk =
            float4(rect.xy + shape.xy + float2(shape.w), max(rect.zw - float2(2.0 * shape.w), float2(0.0)));
        value = inside * (1.0 - blurred(pixel, shrunk, max(radii - float4(shape.w), float4(0.0)), sigma));
    }
    return clipped(shadows[shadow].color * value, pixel, int(flags.y));
}

[shader("vertex")]
Corner glyphVs(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    return cornerOf(glyphs[instance].rect, vertex, instance);
}

// The glyph's coverage, read texel for pixel from the atlas (its image
// lies on the picture's pixels), times its run's color. Coverage is linear
// in the area covered and blended in linear light as it is.
[shader("fragment")]
float4 glyphFs(Corner corner) : SV_Target0
{
    const float2 pixel = corner.pixel;
    const uint glyph = corner.record;
    const float4 rect = glyphs[glyph].rect;
    const float4 atlas = glyphs[glyph].atlas;
    const float2 within = clamp(floor(pixel - rect.xy), float2(0.0), max(atlas.zw - 1.0, float2(0.0)));
    const float covered = picture.Load(int3(int2(atlas.xy + within), 0)).r;
    return clipped(glyphs[glyph].color * covered, pixel, int(glyphs[glyph].clip.x));
}
