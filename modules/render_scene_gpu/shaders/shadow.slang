// The sun's shadow map (D289), in Slang (ADR-0026, D475): a caster's
// vertex placed by its instance's model rows relative to the eye, then
// seen through one cascade's square, reversed-Z (ADR-0051): depth one
// toward the sun. A masked caster (D310) casts no shadow where the
// opacity its material gives falls below its alpha cutoff, as the scene's
// cut takes it from the view (D487): the material linked as the scene's
// is, the blob's or a material's own program (D488), with its bindings.
// A caster whose material moves its vertices is moved as the scene moves
// it (D691), its normal, color, and coordinates read for that.
import material;

extern struct Material : IMaterial;

// The square's view, and the frame's eye in the World's axes and time,
// now and the frame before (D691).
struct Cascade
{
    float4x4 viewProjection;
    float4 eye;
    float4 time;
}

[[vk::binding(0, 0)]] ConstantBuffer<Cascade> cascade;

// A caster's vertex and its instance's rows.
struct Caster
{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float4 model0;
    [[vk::location(3)]] float4 model1;
    [[vk::location(4)]] float4 model2;
    [[vk::location(5)]] float3 normal0;
    [[vk::location(6)]] float3 normal1;
    [[vk::location(7)]] float3 normal2;
    [[vk::location(8)]] float4 color;
    // The model's material's place among the frame's materials, which a
    // masked caster is cut by.
    [[vk::location(12)]] float material;
    [[vk::location(13)]] float2 uv;
}

float4 placed(Caster caster)
{
    const float4 vertex = float4(caster.position, 1.0);
    const float3 there = float3(dot(caster.model0, vertex), dot(caster.model1, vertex), dot(caster.model2, vertex));
    Placed here;
    here.position = there + cascade.eye.xyz;
    here.normal = normalize(mul(caster.normal, float3x3(caster.normal0, caster.normal1, caster.normal2)));
    here.uv = caster.uv;
    here.color = caster.color;
    here.time = cascade.time.x;
    here.view = normalize(-there);
    return mul(cascade.viewProjection, float4(there + Material.offset(here), 1.0));
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
Cut vsCut(Caster caster)
{
    Cut cut;
    cut.position = placed(caster);
    cut.color = caster.color;
    cut.material = uint(caster.material);
    cut.uv = caster.uv;
    return cut;
}

[shader("fragment")]
void cut(Cut cut)
{
    // A caster carries no normal: the material's opacity is read facing
    // the sun and seen along that normal (D696), the coordinates'
    // derivatives taken before anything branches.
    Point point;
    point.uv = cut.uv;
    point.dx = ddx(cut.uv);
    point.dy = ddy(cut.uv);
    point.normal = float3(0.0, 0.0, 1.0);
    point.tangent = float4(1.0, 0.0, 0.0, 1.0);
    point.color = cut.color;
    point.material = cut.material;
    point.position = float3(0.0);
    point.time = cascade.time.x;
    point.view = float3(0.0, 0.0, 1.0);
    const Evaluated evaluated = Material.evaluate(point);
    if (evaluated.opacity < materials[blobOf(cut.material) + 3u].z)
    {
        discard;
    }
}
