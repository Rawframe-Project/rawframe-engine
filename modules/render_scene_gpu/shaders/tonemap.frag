// The scene's picture (D284), fragment entry "fs": the pre-exposed
// scene-linear light of the texel under it, mixed with the bloom's spread
// light as the camera asks (D328), graded as the camera asks
// (D294, ADR-0051: white balance, ASC CDL, saturation, contrast about
// middle grey), then mapped for display by the camera's tonemapper (D295,
// ADR-0047's closed set: AgX, the default; Khronos PBR Neutral; linear),
// each keeping middle grey where AgX puts it, in linear light for the
// sRGB picture to encode.
// AgX as Troy Sobotka made it, fitted by Benjamin Wrensch.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D scene;

// The grade: the white balance's rows; the slope, with the saturation;
// the offset, with the contrast; the power, with whether to grade at all.
// Then the tonemapper's number, and what it scales light by first.
layout(set = 0, binding = 1, std140) uniform Grade
{
    vec4 balance[3];
    vec4 slope;
    vec4 offset;
    vec4 power;
    vec4 tonemapper;
    // The bloom's share, and one over its chain's levels (D328).
    vec4 bloom;
}
grade;

// The bloom's spread light, summed over its chain's levels (D328), at a
// quarter of the target's sides.
layout(set = 0, binding = 2) uniform texture2D bloom;
layout(set = 0, binding = 3) uniform sampler blended;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

vec3 contrast(vec3 x)
{
    const vec3 kX2 = x * x;
    const vec3 kX4 = kX2 * kX2;
    return 15.5 * kX4 * kX2 - 40.14 * kX4 * x + 31.96 * kX4 - 6.868 * kX2 * x + 0.4298 * kX2 + 0.1191 * x - 0.00232;
}

vec3 graded(vec3 color)
{
    if (grade.power.w < 0.5) {
        return color;
    }
    color = vec3(dot(grade.balance[0].xyz, color), dot(grade.balance[1].xyz, color), dot(grade.balance[2].xyz, color));
    color = pow(max(color * grade.slope.xyz + grade.offset.xyz, vec3(0.0)), grade.power.xyz);
    const float kLuma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = max(vec3(kLuma) + (color - vec3(kLuma)) * grade.slope.w, vec3(0.0));
    return 0.18 * pow(color / 0.18, vec3(grade.offset.w));
}

// Khronos PBR Neutral, as Khronos specifies it.
vec3 neutral(vec3 color)
{
    const float kStart = 0.8 - 0.04;
    const float kDesaturation = 0.15;
    const float kLeast = min(color.r, min(color.g, color.b));
    const float kOffset = kLeast < 0.08 ? kLeast - 6.25 * kLeast * kLeast : 0.04;
    color -= kOffset;
    const float kPeak = max(color.r, max(color.g, color.b));
    if (kPeak < kStart) {
        return color;
    }
    const float kRoom = 1.0 - kStart;
    const float kNewPeak = 1.0 - kRoom * kRoom / (kPeak + kRoom - kStart);
    color *= kNewPeak / kPeak;
    const float kGrey = 1.0 - 1.0 / (kDesaturation * (kPeak - kNewPeak) + 1.0);
    return mix(color, vec3(kNewPeak), kGrey);
}

void main()
{
    const mat3 kInset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992,
                             0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 kOutset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368,
                              1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433,
                              1.15107367264116);
    const float kLowest = -12.47393;
    const float kHighest = 4.026069;
    const ivec2 kSize = textureSize(scene, 0);
    const ivec2 kTexel = min(ivec2(inUv * vec2(kSize)), kSize - 1);
    vec3 seen = texelFetch(scene, kTexel, 0).rgb;
    if (grade.bloom.x > 0.0) {
        seen = mix(seen, textureLod(sampler2D(bloom, blended), inUv, 0.0).rgb * grade.bloom.y, grade.bloom.x);
    }
    const vec3 kLight = graded(seen) * grade.tonemapper.y;
    if (grade.tonemapper.x > 1.5) {
        outColor = vec4(clamp(kLight, 0.0, 1.0), 1.0);
        return;
    }
    if (grade.tonemapper.x > 0.5) {
        outColor = vec4(clamp(neutral(max(kLight, vec3(0.0))), 0.0, 1.0), 1.0);
        return;
    }
    vec3 color = kInset * max(kLight, vec3(1e-10));
    color = (clamp(log2(color), kLowest, kHighest) - kLowest) / (kHighest - kLowest);
    color = kOutset * contrast(color);
    outColor = vec4(pow(max(color, vec3(0.0)), vec3(2.2)), 1.0);
}
