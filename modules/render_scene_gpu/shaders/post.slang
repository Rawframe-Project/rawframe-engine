// A post process (D348, D350), in Slang (ADR-0026, D475): the picture
// coming in at the texel under it, and the post process's texture where
// its map puts the texel, folded as its material's form, per channel the
// constant, the picture times its factor, the texture times its, and the
// two multiplied times theirs; its alpha the constant's and the texture's.
// The color, never below nought, is laid over the picture by that alpha,
// and the result over the picture again by the camera's weight. A post
// process sampling no texture is given white, which its form multiplies by
// nought.

import screen;

// The form's constant, picture, texture, and both; the texture's scale and
// offset; and the weight.
struct Process
{
    float4 fixedPart;
    float4 scenePart;
    float4 texturePart;
    float4 bothPart;
    float4 map;
    float4 weight;
}

[[vk::binding(0, 0)]] Texture2D picture;
[[vk::binding(1, 0)]] ConstantBuffer<Process> process;
[[vk::binding(2, 0)]] Texture2D sampled;
[[vk::binding(3, 0)]] SamplerState sampling;

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
    picture.GetDimensions(width, height);
    const int2 size = int2(int(width), int(height));
    const int2 texel = min(int2(screen.uv * float2(size)), size - 1);
    const float3 coming = picture.Load(int3(texel, 0)).rgb;
    const float4 textured = sampled.SampleLevel(sampling, screen.uv * process.map.xy + process.map.zw, 0.0);
    const float3 color = process.fixedPart.rgb + process.scenePart.rgb * coming + process.texturePart.rgb * textured.rgb +
                         process.bothPart.rgb * coming * textured.rgb;
    const float alpha = clamp(process.fixedPart.a + process.texturePart.a * textured.a, 0.0, 1.0);
    return float4(lerp(coming, max(color, float3(0.0)), alpha * process.weight.x), 1.0);
}
