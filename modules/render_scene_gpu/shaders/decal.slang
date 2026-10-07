// The decals' atlas (ADR-0051, D339), in Slang (ADR-0026, D475): a decal's
// texture drawn into one mip of its layer by one triangle covering it,
// sampled at the texture's own mip nearest that size (the triangle's
// coordinates step one target texel a pixel), filtered between its mips.

import screen;

[[vk::binding(0, 0)]] Texture2D source;
[[vk::binding(1, 0)]] SamplerState blended;

[shader("vertex")]
Screen vs(uint vertex : SV_VulkanVertexID)
{
    return screenOf(vertex);
}

[shader("fragment")]
float4 fill(Screen screen) : SV_Target0
{
    return source.Sample(blended, screen.uv);
}
