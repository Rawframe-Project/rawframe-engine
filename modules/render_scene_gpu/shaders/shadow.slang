// The sun's shadow map (D289), in Slang (ADR-0026, D475): a caster's
// vertex placed by its instance's model rows relative to the eye, then
// seen through one cascade's square, reversed-Z (ADR-0051): depth one
// toward the sun. A masked caster (D310) casts no shadow where the
// opacity its material gives falls below its alpha cutoff, as the scene's
// cut takes it from the view (D487): the material linked as the scene's
// is, the blob's or a material's own program (D488), with its bindings.
import material;

extern struct Material : IMaterial;

struct Cascade
{
    float4x4 viewProjection;
}

[[vk::binding(0, 0)]] ConstantBuffer<Cascade> cascade;

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
    // A caster carries no normal: the material's opacity is read facing
    // the sun, the coordinates' derivatives taken before anything branches.
    Point point;
    point.uv = cut.uv;
    point.dx = ddx(cut.uv);
    point.dy = ddy(cut.uv);
    point.normal = float3(0.0, 0.0, 1.0);
    point.tangent = float4(1.0, 0.0, 0.0, 1.0);
    point.color = cut.color;
    point.material = cut.material;
    const Evaluated evaluated = Material.evaluate(point);
    if (evaluated.opacity < materials[blobOf(cut.material) + 3u].z)
    {
        discard;
    }
}
