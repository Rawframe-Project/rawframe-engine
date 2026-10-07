// The scene's temporal anti-aliasing (D291, ADR-0051), in Slang (ADR-0026,
// D475): the frame's pre-exposed light blended with the picture before,
// read where the texel's motion says it was and kept within the colors
// around the texel now (in YCoCg), so what was not there before does not
// ghost. A tenth of each frame is kept, the brighter weighted less, so a
// lone bright texel does not flicker. Without a picture before, or where
// the texel was off it, the frame alone.

import screen;

// Whether the picture before may be reused.
struct Temporal
{
    float4 state;
}

[[vk::binding(0, 0)]] Texture2D scene;
[[vk::binding(1, 0)]] Texture2D history;
[[vk::binding(2, 0)]] Texture2D motion;
[[vk::binding(3, 0)]] SamplerState blended;
[[vk::binding(4, 0)]] ConstantBuffer<Temporal> temporal;

float3 ycocgOf(float3 color)
{
    return float3(0.25 * color.r + 0.5 * color.g + 0.25 * color.b,
                  0.5 * color.r - 0.5 * color.b,
                  -0.25 * color.r + 0.5 * color.g - 0.25 * color.b);
}

float3 rgbOf(float3 color)
{
    return float3(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z);
}

float lumaOf(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[shader("vertex")]
Screen vs(uint vertex : SV_VulkanVertexID)
{
    return screenOf(vertex);
}

[shader("fragment")]
float4 fs(Screen screen) : SV_Target0
{
    uint width;
    uint height;
    scene.GetDimensions(width, height);
    const int2 size = int2(int(width), int(height));
    const int2 texel = min(int2(screen.uv * float2(size)), size - 1);
    const float3 now = scene.Load(int3(texel, 0)).rgb;
    float3 lowest = ycocgOf(now);
    float3 highest = lowest;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float3 around = ycocgOf(scene.Load(int3(clamp(texel + int2(x, y), int2(0), size - 1), 0)).rgb);
            lowest = min(lowest, around);
            highest = max(highest, around);
        }
    }
    const float2 was = (float2(texel) + 0.5) / float2(size) - motion.Load(int3(texel, 0)).xy;
    const float3 before = history.SampleLevel(blended, was, 0.0).rgb;
    if (temporal.state.x == 0.0 || any(was < float2(0.0)) || any(was > float2(1.0)))
    {
        return float4(now, 1.0);
    }
    const float3 kept = rgbOf(clamp(ycocgOf(before), lowest, highest));
    const float nowWeight = 0.1 / (1.0 + lumaOf(now));
    const float keptWeight = 0.9 / (1.0 + lumaOf(kept));
    return float4((now * nowWeight + kept * keptWeight) / (nowWeight + keptWeight), 1.0);
}
