// The scene's motion blur (D334), fragment entry "gather": McGuire et al.
// 2012's reconstruction filter. Fifteen taps along the longest blur that
// reaches the point's tile, jittered per pixel, each weighed by whether it
// blurs over the point: a tap in front, by its own blur's reach; a tap
// behind, by the point's (what the point's blur uncovers); and taps both
// moving alike, by both. What stands in front stays sharp over what moves
// behind it, and what moves in front smears over what stands behind.

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

layout(location = 0) out vec4 outColor;

const int kTaps = 15;

// A point's blur in pixels, at most the longest.
vec2 blurAt(ivec2 at, vec2 size)
{
    const vec2 kBlur = texelFetch(motion, at, 0).xy * size * blur.settings.x;
    const float kLength = length(kBlur);
    return kLength > blur.settings.y ? kBlur * (blur.settings.y / kLength) : kBlur;
}

// How far ahead a point is, in meters (reversed-Z, infinite far).
float aheadAt(ivec2 at)
{
    return blur.settings.z / max(texelFetch(depth, at, 0).r, 1e-7);
}

// Whether `near` stands in front of `far`, softly over a few hundredths
// of the distance.
float inFront(float near, float far)
{
    return clamp(1.0 - (near - far) / (0.02 * min(near, far)), 0.0, 1.0);
}

float cone(float apart, float reach)
{
    return clamp(1.0 - apart / reach, 0.0, 1.0);
}

float cylinder(float apart, float reach)
{
    return 1.0 - smoothstep(0.95 * reach, 1.05 * reach, apart);
}

void main()
{
    const ivec2 kAt = ivec2(gl_FragCoord.xy);
    const vec4 kHere = texelFetch(scene, kAt, 0);
    const int kSide = int(blur.settings.y);
    const vec2 kLongest = texelFetch(tiles, kAt / kSide, 0).xy;
    if (dot(kLongest, kLongest) <= 0.25) {
        outColor = kHere;
        return;
    }
    const ivec2 kSize = textureSize(scene, 0);
    const vec2 kSizeF = vec2(kSize);
    const float kReach = max(length(blurAt(kAt, kSizeF)), 0.5);
    const float kAhead = aheadAt(kAt);
    // Interleaved gradient noise (Jimenez 2014): the taps' offset, less
    // than one tap's spacing, differing from pixel to pixel.
    const float kJitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5;
    float weight = 1.0 / kReach;
    vec4 sum = kHere * weight;
    for (int tap = 0; tap < kTaps; ++tap) {
        if (tap == kTaps / 2) {
            continue;
        }
        const float kAlong = mix(-1.0, 1.0, (float(tap) + kJitter + 1.0) / float(kTaps + 1));
        const ivec2 kThere = clamp(ivec2(floor(gl_FragCoord.xy + kLongest * kAlong)), ivec2(0), kSize - 1);
        const float kApart = length(kLongest * kAlong);
        const float kThereAhead = aheadAt(kThere);
        const float kThereReach = max(length(blurAt(kThere, kSizeF)), 0.5);
        const float kTaken = inFront(kThereAhead, kAhead) * cone(kApart, kThereReach) +
                             inFront(kAhead, kThereAhead) * cone(kApart, kReach) +
                             cylinder(kApart, kThereReach) * cylinder(kApart, kReach) * 2.0;
        weight += kTaken;
        sum += texelFetch(scene, kThere, 0) * kTaken;
    }
    outColor = sum / weight;
}
