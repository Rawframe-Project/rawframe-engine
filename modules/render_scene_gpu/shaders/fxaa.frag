// The scene's FXAA (D296, ADR-0051), fragment entry "fs": the tonemapped
// picture antialiased where its contrast marks an edge, in the manner of
// Timothy Lottes' FXAA: the four diagonal neighbours give the edge's
// direction, two and then four taps along it blend across it, and the
// wider blend is kept unless it leaves the neighbourhood's light. Luma is
// the square root of the linear light's, near what the display shows.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D picture;
layout(set = 0, binding = 1) uniform sampler filtered;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

// Contrast below the larger of these is no edge; the direction's floor
// and share of the light; the farthest reach along the edge, in texels.
const float kEdgeThreshold = 0.166;
const float kEdgeThresholdMin = 0.0833;
const float kReduceMin = 1.0 / 128.0;
const float kReduceMul = 1.0 / 8.0;
const float kSpanMax = 8.0;

vec3 at(vec2 uv)
{
    return textureLod(sampler2D(picture, filtered), uv, 0.0).rgb;
}

float lumaOf(vec3 color)
{
    return sqrt(dot(color, vec3(0.2126, 0.7152, 0.0722)));
}

void main()
{
    const vec2 kTexel = 1.0 / vec2(textureSize(picture, 0));
    const vec3 kMiddle = at(inUv);
    const float kNw = lumaOf(at(inUv + vec2(-1.0, -1.0) * kTexel));
    const float kNe = lumaOf(at(inUv + vec2(1.0, -1.0) * kTexel));
    const float kSw = lumaOf(at(inUv + vec2(-1.0, 1.0) * kTexel));
    const float kSe = lumaOf(at(inUv + vec2(1.0, 1.0) * kTexel));
    const float kM = lumaOf(kMiddle);
    const float kLowest = min(kM, min(min(kNw, kNe), min(kSw, kSe)));
    const float kHighest = max(kM, max(max(kNw, kNe), max(kSw, kSe)));
    if (kHighest - kLowest < max(kEdgeThresholdMin, kHighest * kEdgeThreshold)) {
        outColor = vec4(kMiddle, 1.0);
        return;
    }
    vec2 direction = vec2(-((kNw + kNe) - (kSw + kSe)), (kNw + kSw) - (kNe + kSe));
    const float kReduce = max((kNw + kNe + kSw + kSe) * 0.25 * kReduceMul, kReduceMin);
    const float kScale = 1.0 / (min(abs(direction.x), abs(direction.y)) + kReduce);
    direction = clamp(direction * kScale, vec2(-kSpanMax), vec2(kSpanMax)) * kTexel;
    const vec3 kNarrow = 0.5 * (at(inUv + direction * (1.0 / 3.0 - 0.5)) + at(inUv + direction * (2.0 / 3.0 - 0.5)));
    const vec3 kWide = kNarrow * 0.5 + 0.25 * (at(inUv - direction * 0.5) + at(inUv + direction * 0.5));
    const float kWideLuma = lumaOf(kWide);
    outColor = vec4(kWideLuma < kLowest || kWideLuma > kHighest ? kNarrow : kWide, 1.0);
}
