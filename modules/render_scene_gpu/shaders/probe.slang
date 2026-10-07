// The reflection probes' atlas (ADR-0051, D340), in Slang (ADR-0026,
// D475): one face of a probe's picture drawn into one mip of its cube in
// the atlas by one triangle covering it, the face and the mip the draw's
// first instance names (the face times eight, plus the mip). Each mip is
// the light as an ever rougher surface reflects it, as the picture's
// levels are (D321): mip m of the atlas's eight from the picture's level m
// sevenths of the way to its last.

import screen;

[[vk::binding(0, 0)]] TextureCube source;
[[vk::binding(1, 0)]] SamplerState blended;

struct Face
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] nointerpolation uint at;
}

[shader("vertex")]
Face vs(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    const Screen screen = screenOf(vertex);
    Face face;
    face.position = screen.position;
    face.uv = screen.uv;
    face.at = instance;
    return face;
}

[shader("fragment")]
float4 fill(Face face) : SV_Target0
{
    const uint side = face.at / 8u;
    // The direction through the texel, s rightward and t downward, as
    // Vulkan addresses a cube.
    const float s = face.uv.x * 2.0 - 1.0;
    const float t = face.uv.y * 2.0 - 1.0;
    float3 toward = float3(-s, -t, -1.0);
    if (side == 0u)
    {
        toward = float3(1.0, -t, -s);
    }
    else if (side == 1u)
    {
        toward = float3(-1.0, -t, s);
    }
    else if (side == 2u)
    {
        toward = float3(s, 1.0, t);
    }
    else if (side == 3u)
    {
        toward = float3(s, -1.0, -t);
    }
    else if (side == 4u)
    {
        toward = float3(s, -t, 1.0);
    }
    uint width;
    uint height;
    uint levels;
    source.GetDimensions(0, width, height, levels);
    return source.SampleLevel(blended, toward, float(face.at % 8u) / 7.0 * float(levels - 1u));
}
