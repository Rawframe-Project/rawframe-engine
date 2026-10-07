// A frame's picture shown on a window's surface (D280), in Slang (ADR-0026,
// D475), from which the SPIR-V and the WGSL are compiled.

[[vk::binding(0, 0)]] Texture2D picture;
[[vk::binding(1, 0)]] SamplerState pictureSampler;

struct Corner
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
}

// One triangle covering the target, with the picture's coordinates, rows
// top first.
[shader("vertex")]
Corner vs(uint vertex : SV_VulkanVertexID)
{
    const float2 at = float2(float((vertex << 1) & 2), float(vertex & 2));
    Corner corner;
    corner.uv = at;
    corner.position = float4(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0, 0.0, 1.0);
    return corner;
}

// The picture's bytes as they are, read through its linear view, so its
// sRGB encoding reaches the surface unchanged.
[shader("fragment")]
float4 fs(Corner corner) : SV_Target0
{
    return picture.Sample(pictureSampler, corner.uv);
}
