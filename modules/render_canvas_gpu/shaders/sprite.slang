// The canvas's sprites (D278, D356, D475), in Slang (ADR-0026), from which
// the SPIR-V and the WGSL are compiled.
//
// Vertex entry "vs": a quad's corner as the queue stage made it, in clip
// space, where y runs up; its place in the texture; and its color,
// 0xRRGGBBAA, whose bytes arrive in memory order, alpha first.
//
// Fragment entry "fs": the sprite's texel, tinted, times its material's
// color, the material's texture sampled across the sprite's region where
// it has one; and its material's emission added within the sprite's
// shape. Written premultiplied, as its material's blend asks of its
// pipeline: over what is behind, or added to it; or, multiplying, what is
// behind is scaled by its color as far as it covers, the emission left
// out.

// The material (D355): its color, and what multiplies its texture's; its
// emission, and whether it multiplies (its fourth); what multiplies its
// texture's color in its emission; and its texture's scale and offset.
struct Material
{
    float4 color;
    float4 colorTexture;
    float4 emission;
    float4 emissionTexture;
    float4 map;
}

[[vk::binding(0, 0)]] Texture2D spriteTexture;
[[vk::binding(1, 0)]] SamplerState spriteSampler;
[[vk::binding(2, 0)]] ConstantBuffer<Material> material;
[[vk::binding(3, 0)]] Texture2D materialTexture;
[[vk::binding(4, 0)]] SamplerState materialSampler;

struct Corner
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] float4 color;
}

// The color's channels are written in sRGB (ADR-0047): blending happens in
// linear light, so they are decoded here.
float3 linearOf(float3 encoded)
{
    return select(encoded < float3(0.04045), encoded / 12.92, pow((encoded + 0.055) / 1.055, float3(2.4)));
}

[shader("vertex")]
Corner vs([[vk::location(0)]] float2 position, [[vk::location(1)]] float2 uv, [[vk::location(2)]] float4 color)
{
    Corner corner;
    // Maul RHI's clip space runs up on every driver, as WebGPU's does.
    corner.position = float4(position, 0.0, 1.0);
    corner.uv = uv;
    const float4 ordered = color.wzyx;
    corner.color = float4(linearOf(ordered.rgb), ordered.a);
    return corner;
}

[shader("fragment")]
float4 fs(Corner corner) : SV_Target0
{
    const float4 sprite = spriteTexture.Sample(spriteSampler, corner.uv) * corner.color;
    const float4 sampled = materialTexture.Sample(materialSampler, corner.uv * material.map.xy + material.map.zw);
    const float4 tinted = sprite * (material.color + material.colorTexture * sampled);
    if (material.emission.w > 0.5)
    {
        return float4(lerp(float3(1.0), tinted.rgb, tinted.a), 0.0);
    }
    const float3 glow = (material.emission.rgb + material.emissionTexture.rgb * sampled.rgb) * sprite.a;
    return float4(tinted.rgb * tinted.a + glow, tinted.a);
}
