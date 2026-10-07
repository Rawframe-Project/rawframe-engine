// The scene's bloom (ADR-0051, D328), in Slang (ADR-0026, D475): the
// scene's light quartered into the first level of a chain ("first"), each
// level halved into the next ("down"), and each doubled and added to the
// level above ("up"), so each level holds its own light and all those
// below it.

import screen;

[[vk::binding(0, 0)]] Texture2D source;
[[vk::binding(1, 0)]] SamplerState blended;

// The source's light `x` and `y` of its texels from `uv`.
float3 at(float2 uv, float x, float y)
{
    uint width;
    uint height;
    source.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(float(width), float(height));
    return source.SampleLevel(blended, uv + float2(x, y) * texel, 0.0).rgb;
}

float weightOf(float3 light)
{
    return 1.0 / (1.0 + dot(light, float3(0.2126, 0.7152, 0.0722)));
}

[shader("vertex")]
Screen vs(uint vertex : SV_VulkanVertexID)
{
    return screenOf(vertex);
}

// Jimenez's thirteen taps, spread twice as wide, as five overlapping
// boxes, each weighed by one over one plus its luma (Karis's average), so
// a lone bright texel does not flicker as it moves.
[shader("fragment")]
float4 first(Screen screen) : SV_Target0
{
    const float2 uv = screen.uv;
    const float3 a = at(uv, -4.0, -4.0);
    const float3 b = at(uv, 0.0, -4.0);
    const float3 c = at(uv, 4.0, -4.0);
    const float3 d = at(uv, -4.0, 0.0);
    const float3 e = at(uv, 0.0, 0.0);
    const float3 f = at(uv, 4.0, 0.0);
    const float3 g = at(uv, -4.0, 4.0);
    const float3 h = at(uv, 0.0, 4.0);
    const float3 i = at(uv, 4.0, 4.0);
    const float3 j = at(uv, -2.0, -2.0);
    const float3 k = at(uv, 2.0, -2.0);
    const float3 l = at(uv, -2.0, 2.0);
    const float3 m = at(uv, 2.0, 2.0);
    const float3 boxes[5] = { (j + k + l + m) * 0.25,
                              (a + b + d + e) * 0.25,
                              (b + c + e + f) * 0.25,
                              (d + e + g + h) * 0.25,
                              (e + f + h + i) * 0.25 };
    const float shares[5] = { 0.5, 0.125, 0.125, 0.125, 0.125 };
    float3 sum = float3(0.0);
    float weight = 0.0;
    for (int box = 0; box < 5; ++box)
    {
        const float weighed = shares[box] * weightOf(boxes[box]);
        sum += boxes[box] * weighed;
        weight += weighed;
    }
    return float4(sum / weight, 1.0);
}

// Jimenez's thirteen taps.
[shader("fragment")]
float4 down(Screen screen) : SV_Target0
{
    const float2 uv = screen.uv;
    const float3 corners = at(uv, -2.0, -2.0) + at(uv, 2.0, -2.0) + at(uv, -2.0, 2.0) + at(uv, 2.0, 2.0);
    const float3 edges = at(uv, 0.0, -2.0) + at(uv, -2.0, 0.0) + at(uv, 2.0, 0.0) + at(uv, 0.0, 2.0);
    const float3 inner = at(uv, -1.0, -1.0) + at(uv, 1.0, -1.0) + at(uv, -1.0, 1.0) + at(uv, 1.0, 1.0);
    return float4(at(uv, 0.0, 0.0) * 0.125 + corners * 0.03125 + edges * 0.0625 + inner * 0.125, 1.0);
}

// A three-by-three tent.
[shader("fragment")]
float4 up(Screen screen) : SV_Target0
{
    const float2 uv = screen.uv;
    const float3 corners = at(uv, -1.0, -1.0) + at(uv, 1.0, -1.0) + at(uv, -1.0, 1.0) + at(uv, 1.0, 1.0);
    const float3 edges = at(uv, 0.0, -1.0) + at(uv, -1.0, 0.0) + at(uv, 1.0, 0.0) + at(uv, 0.0, 1.0);
    return float4((at(uv, 0.0, 0.0) * 4.0 + edges * 2.0 + corners) / 16.0, 1.0);
}
