// The scene's temporal anti-aliasing (D291, ADR-0051), fragment entry "fs":
// the frame's pre-exposed light blended with the picture before, read
// where the texel's motion says it was and kept within the colors around
// the texel now (in YCoCg), so what was not there before does not ghost. A
// tenth of each frame is kept, the brighter weighted less, so a lone
// bright texel does not flicker. Without a picture before, or where the
// texel was off it, the frame alone.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D scene;
layout(set = 0, binding = 1) uniform texture2D history;
layout(set = 0, binding = 2) uniform texture2D motion;
layout(set = 0, binding = 3) uniform sampler blended;

// Whether the picture before may be reused.
layout(set = 0, binding = 4, std140) uniform Temporal
{
    vec4 state;
}
temporal;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

vec3 ycocgOf(vec3 color)
{
    return vec3(0.25 * color.r + 0.5 * color.g + 0.25 * color.b,
                0.5 * color.r - 0.5 * color.b,
                -0.25 * color.r + 0.5 * color.g - 0.25 * color.b);
}

vec3 rgbOf(vec3 color)
{
    return vec3(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z);
}

float lumaOf(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    const ivec2 kSize = textureSize(scene, 0);
    const ivec2 kTexel = min(ivec2(inUv * vec2(kSize)), kSize - 1);
    const vec3 kNow = texelFetch(scene, kTexel, 0).rgb;
    vec3 lowest = ycocgOf(kNow);
    vec3 highest = lowest;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec3 kAround = ycocgOf(texelFetch(scene, clamp(kTexel + ivec2(x, y), ivec2(0), kSize - 1), 0).rgb);
            lowest = min(lowest, kAround);
            highest = max(highest, kAround);
        }
    }
    const vec2 kWas = (vec2(kTexel) + 0.5) / vec2(kSize) - texelFetch(motion, kTexel, 0).xy;
    const vec3 kBefore = textureLod(sampler2D(history, blended), kWas, 0.0).rgb;
    if (temporal.state.x == 0.0 || any(lessThan(kWas, vec2(0.0))) || any(greaterThan(kWas, vec2(1.0)))) {
        outColor = vec4(kNow, 1.0);
        return;
    }
    const vec3 kKept = rgbOf(clamp(ycocgOf(kBefore), lowest, highest));
    const float kNowWeight = 0.1 / (1.0 + lumaOf(kNow));
    const float kKeptWeight = 0.9 / (1.0 + lumaOf(kKept));
    outColor = vec4((kNow * kNowWeight + kKept * kKeptWeight) / (kNowWeight + kKeptWeight), 1.0);
}
