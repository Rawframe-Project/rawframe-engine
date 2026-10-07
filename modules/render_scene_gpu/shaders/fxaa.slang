// The scene's FXAA (D296, ADR-0051), in Slang (ADR-0026, D475): the
// tonemapped picture antialiased where its contrast marks an edge, in the
// manner of Timothy Lottes' FXAA: the four diagonal neighbours give the
// edge's direction, two and then four taps along it blend across it, and
// the wider blend is kept unless it leaves the neighbourhood's light. Luma
// is the square root of the linear light's, near what the display shows.

import screen;

[[vk::binding(0, 0)]] Texture2D picture;
[[vk::binding(1, 0)]] SamplerState filtered;

// Contrast below the larger of these is no edge; the direction's floor
// and share of the light; the farthest reach along the edge, in texels.
static const float kEdgeThreshold = 0.166;
static const float kEdgeThresholdMin = 0.0833;
static const float kReduceMin = 1.0 / 128.0;
static const float kReduceMul = 1.0 / 8.0;
static const float kSpanMax = 8.0;

float3 at(float2 uv)
{
    return picture.SampleLevel(filtered, uv, 0.0).rgb;
}

float lumaOf(float3 color)
{
    return sqrt(dot(color, float3(0.2126, 0.7152, 0.0722)));
}

[shader("vertex")]
Screen vs(uint vertex : SV_VulkanVertexID)
{
    return screenOf(vertex);
}

[shader("fragment")]
float4 fs(Screen screen) : SV_Target0
{
    const float2 uv = screen.uv;
    uint width;
    uint height;
    picture.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(float(width), float(height));
    const float3 middle = at(uv);
    const float nw = lumaOf(at(uv + float2(-1.0, -1.0) * texel));
    const float ne = lumaOf(at(uv + float2(1.0, -1.0) * texel));
    const float sw = lumaOf(at(uv + float2(-1.0, 1.0) * texel));
    const float se = lumaOf(at(uv + float2(1.0, 1.0) * texel));
    const float m = lumaOf(middle);
    const float lowest = min(m, min(min(nw, ne), min(sw, se)));
    const float highest = max(m, max(max(nw, ne), max(sw, se)));
    if (highest - lowest < max(kEdgeThresholdMin, highest * kEdgeThreshold))
    {
        return float4(middle, 1.0);
    }
    float2 direction = float2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    const float reduce = max((nw + ne + sw + se) * 0.25 * kReduceMul, kReduceMin);
    const float scale = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * scale, float2(-kSpanMax), float2(kSpanMax)) * texel;
    const float3 narrow = 0.5 * (at(uv + direction * (1.0 / 3.0 - 0.5)) + at(uv + direction * (2.0 / 3.0 - 0.5)));
    const float3 wide = narrow * 0.5 + 0.25 * (at(uv - direction * 0.5) + at(uv + direction * 0.5));
    const float wideLuma = lumaOf(wide);
    return float4(wideLuma < lowest || wideLuma > highest ? narrow : wide, 1.0);
}
