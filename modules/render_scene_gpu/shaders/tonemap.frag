// The scene's picture (D284), fragment entry "fs": the pre-exposed
// scene-linear light of the texel under it, graded as the camera asks
// (D294, ADR-0051: white balance, ASC CDL, saturation, contrast about
// middle grey), then mapped for display by AgX (ADR-0047's default), in
// linear light for the sRGB picture to encode.
// AgX as Troy Sobotka made it, fitted by Benjamin Wrensch.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D scene;

// The grade: the white balance's rows; the slope, with the saturation;
// the offset, with the contrast; the power, with whether to grade at all.
layout(set = 0, binding = 1, std140) uniform Grade
{
    vec4 balance[3];
    vec4 slope;
    vec4 offset;
    vec4 power;
}
grade;

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
    vec3 color = kInset * max(graded(texelFetch(scene, kTexel, 0).rgb), vec3(1e-10));
    color = (clamp(log2(color), kLowest, kHighest) - kLowest) / (kHighest - kLowest);
    color = kOutset * contrast(color);
    outColor = vec4(pow(max(color, vec3(0.0)), vec3(2.2)), 1.0);
}
