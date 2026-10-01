// The UI's boxes (SPEC-0032, D375), fragment entry "fs": a rounded box by
// its signed distance, its edge smoothed over a pixel, its border inside
// it in the color of the side nearest, over its fill and the gradient over
// that (D382), inside its clip and each clip that clip is inside (or
// outside one, inverted, D377); premultiplied, in linear light.

#version 450

struct Box
{
    vec4 rect;
    vec4 radii;
    vec4 fill;
    vec4 widths;
    vec4 borders[4];
    // Its clip's index in the clips, and its gradient's in the gradients,
    // nought for none.
    vec4 clip;
};

// A gradient: its kind (1 linear, 2 radial), its stops, and its angle; the
// stops' colors, linear and premultiplied; and their positions.
struct Gradient
{
    vec4 head;
    vec4 colors[4];
    vec4 positions;
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

layout(set = 0, binding = 7, std430) readonly buffer Gradients
{
    Gradient gradients[];
}
ramps;

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

vec3 cubeRoot(vec3 value)
{
    return sign(value) * pow(abs(value), vec3(1.0 / 3.0));
}

// Linear sRGB to Oklab and back (Björn Ottosson's matrices).
vec3 toOklab(vec3 color)
{
    vec3 lms = cubeRoot(mat3(0.4122214708, 0.2119034982, 0.0883024619,
                             0.5363325363, 0.6806995451, 0.2817188376,
                             0.0514459929, 0.1073969566, 0.6299787005) * color);
    return mat3(0.2104542553, 1.9779984951, 0.0259040371,
                0.7936177850, -2.4285922050, 0.7827717662,
                -0.0040720468, 0.4505937099, -0.8086757660) * lms;
}

vec3 fromOklab(vec3 lab)
{
    vec3 lms = mat3(1.0, 1.0, 1.0,
                    0.3963377774, -0.1055613458, -0.0894841775,
                    0.2158037573, -0.0638541728, -1.2914855480) * lab;
    return mat3(4.0767416621, -1.2684380046, -0.0041960863,
                -3.3077115913, 2.6097574011, -0.7034186147,
                0.2309699292, -0.3413193965, 1.7076147010) * (lms * lms * lms);
}

// A premultiplied linear color as premultiplied Oklab.
vec4 premultipliedOklab(vec4 color)
{
    if (color.a <= 0.0)
    {
        return vec4(0.0);
    }
    return vec4(toOklab(color.rgb / color.a) * color.a, color.a);
}

// Gradient `index` at `pixel` of the box `rect`: premultiplied, linear.
vec4 ramp(uint index, vec2 pixel, vec4 rect)
{
    vec4 head = ramps.gradients[index].head;
    vec2 halfSize = rect.zw * 0.5;
    vec2 at = pixel - (rect.xy + halfSize);
    float along;
    if (head.x < 1.5)
    {
        float angle = radians(head.z);
        vec2 toward = vec2(sin(angle), -cos(angle));
        float length = abs(rect.z * toward.x) + abs(rect.w * toward.y);
        along = dot(at, toward) / max(length, 1e-4) + 0.5;
    }
    else
    {
        along = length(at / max(halfSize * 1.41421356, vec2(1e-4)));
    }
    int stops = int(head.y);
    vec4 positions = ramps.gradients[index].positions;
    vec4 lab = premultipliedOklab(ramps.gradients[index].colors[0]);
    for (int stop = 1; stop < stops; ++stop)
    {
        if (along > positions[stop - 1])
        {
            float span = max(positions[stop] - positions[stop - 1], 1e-6);
            float share = clamp((along - positions[stop - 1]) / span, 0.0, 1.0);
            lab = mix(premultipliedOklab(ramps.gradients[index].colors[stop - 1]),
                      premultipliedOklab(ramps.gradients[index].colors[stop]),
                      share);
        }
    }
    if (lab.a <= 0.0)
    {
        return vec4(0.0);
    }
    return vec4(max(fromOklab(lab.rgb / lab.a), vec3(0.0)) * lab.a, lab.a);
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
    vec4 fill = list.boxes[inBox].fill;
    uint gradient = uint(list.boxes[inBox].clip.y);
    if (gradient > 0u)
    {
        vec4 over = ramp(gradient, inPixel, rect);
        fill = over + fill * (1.0 - over.a);
    }
    vec4 color = mix(list.boxes[inBox].borders[side], fill, filled) * outer;
    int clip = int(list.boxes[inBox].clip.x);
    for (int depth = 0; depth < kDeepestClip && clip > 0; ++depth)
    {
        float kept = coverage(distanceTo(inPixel, table.clips[clip].rect, table.clips[clip].radii));
        color *= table.clips[clip].link.y > 0.5 ? 1.0 - kept : kept;
        clip = int(table.clips[clip].link.x);
    }
    outColor = color;
}
