// The sun's shadow map (D289), in Slang (ADR-0026, D475): a caster's
// vertex placed by its instance's model rows relative to the eye, then
// seen through one cascade's square, reversed-Z (ADR-0051): depth one
// toward the sun. A masked caster (D310) casts no shadow where its
// material's opacity falls below its alpha cutoff, as the scene's cut
// takes it from the view.

struct Cascade
{
    float4x4 viewProjection;
}

[[vk::binding(0, 0)]] ConstantBuffer<Cascade> cascade;
// Every material's blob (D303), nine vectors each; the fourth holds the
// opacity, the occlusion, the alpha cutoff, and the flags, the fifth the
// base texture's scale and offset (D311).
[[vk::binding(1, 0)]] StructuredBuffer<float4> materials;
[[vk::binding(2, 0)]] Texture2D baseTexture;
[[vk::binding(3, 0)]] SamplerState baseSampler;

// A caster's vertex and its instance's model rows.
struct Caster
{
    [[vk::location(0)]] float3 position;
    [[vk::location(2)]] float4 model0;
    [[vk::location(3)]] float4 model1;
    [[vk::location(4)]] float4 model2;
}

float4 placed(Caster caster)
{
    const float4 vertex = float4(caster.position, 1.0);
    const float3 there = float3(dot(caster.model0, vertex), dot(caster.model1, vertex), dot(caster.model2, vertex));
    return mul(cascade.viewProjection, float4(there, 1.0));
}

struct Bare
{
    float4 position : SV_Position;
}

[shader("vertex")]
Bare vs(Caster caster)
{
    Bare bare;
    bare.position = placed(caster);
    return bare;
}

// What the cut needs of a masked caster: its color, its material's place,
// and its texture coordinates.
struct Cut
{
    float4 position : SV_Position;
    [[vk::location(1)]] float4 color;
    [[vk::location(5)]] nointerpolation uint material;
    [[vk::location(6)]] float2 uv;
}

[shader("vertex")]
Cut vsCut(Caster caster,
          [[vk::location(8)]] float4 color,
          [[vk::location(12)]] float material,
          [[vk::location(13)]] float2 uv)
{
    Cut cut;
    cut.position = placed(caster);
    cut.color = color;
    cut.material = uint(material);
    cut.uv = uv;
    return cut;
}

[shader("fragment")]
void cut(Cut cut)
{
    uint count;
    uint stride;
    materials.GetDimensions(count, stride);
    const uint at = min(cut.material, count / 9u - 1u) * 9u;
    const float4 rest = materials[at + 3u];
    // Where its texture is sampled: the coordinates scaled and moved (D311).
    const float4 mapped = materials[at + 4u];
    const float4 sampled = baseTexture.Sample(baseSampler, cut.uv * mapped.xy + mapped.zw);
    const float opacity = rest.x * cut.color.a * ((uint(rest.w) & 4u) != 0u ? sampled.a : 1.0);
    if (opacity < rest.z)
    {
        discard;
    }
}
