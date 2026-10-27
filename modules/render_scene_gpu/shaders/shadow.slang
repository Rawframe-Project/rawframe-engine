// The sun's shadow map, in Slang: a caster's
// vertex placed by its instance's model rows relative to the eye, then
// seen through one cascade's square, reversed-Z: depth one
// toward the sun. A masked caster casts no shadow where the
// opacity its material gives falls below its alpha cutoff, as the scene's
// cut takes it from the view: the material linked as the scene's
// is, the blob's or a material's own program, with its bindings.
// A caster whose material moves its vertices is moved as the scene moves
// it, its normal, color, and coordinates read for that. A caster
// that blends casts as much shadow as it covers: it is cut where a
// fixed noise over the map passes what it lets through, its opacity's
// remainder and what it transmits, which the shadows' filter spreads into
// a partial shadow.
import material;

extern struct Material : IMaterial;

// The square's view, and the frame's eye in the World's axes and time,
// now and the frame before; and the frame's view and its view and
// projection, unjittered, which a material's transform reads as the
// scene's does, so a caster moves as its model is drawn.
struct Cascade
{
    float4x4 viewProjection;
    float4 eye;
    float4 time;
    float4x4 view;
    float4x4 clip;
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

Spaces spacesOf(float4 model0, float4 model1, float4 model2)
{
    Spaces spaces;
    spaces.eye = cascade.eye.xyz;
    spaces.model0 = model0;
    spaces.model1 = model1;
    spaces.model2 = model2;
    spaces.view = cascade.view;
    spaces.clip = cascade.clip;
    return spaces;
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
    here.time = cascade.time.xy;
    here.view = normalize(-there);
    here.material = uint(caster.material);
    here.spaces = spacesOf(caster.model0, caster.model1, caster.model2);
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
void cut(Cut cut, bool front: SV_IsFrontFace)
{
    // A caster carries no normal: the material's opacity is read facing
    // the sun and seen along that normal, its front the face the
    // sun sees, its model the World's axes at the eye, the
    // coordinates' derivatives taken before anything branches.
    Point point;
    point.uv = cut.uv;
    point.dx = ddx(cut.uv);
    point.dy = ddy(cut.uv);
    point.normal = float3(0.0, 0.0, 1.0);
    point.tangent = float4(1.0, 0.0, 0.0, 1.0);
    point.color = cut.color;
    point.material = cut.material;
    point.position = float3(0.0);
    point.time = cascade.time.xy;
    point.view = float3(0.0, 0.0, 1.0);
    point.front = front;
    point.spaces = spacesOf(float4(1.0, 0.0, 0.0, 0.0), float4(0.0, 1.0, 0.0, 0.0), float4(0.0, 0.0, 1.0, 0.0));
    const Evaluated evaluated = Material.evaluate(point);
    const float4 states = materials[blobOf(cut.material) + 3u];
    float covers = 1.0;
    if ((uint(states.w) & 32u) != 0u)
    {
        covers = evaluated.opacity * (1.0 - evaluated.transmission * (1.0 - evaluated.metalness));
    }
    // Jimenez's interleaved gradient noise, fixed to the map's texels.
    const float noise = frac(52.9829189 * frac(dot(cut.position.xy, float2(0.06711056, 0.00583715))));
    // Every input read whatever the material reads, as the scene's are: an
    // input left unread is dropped from the entry, and D3D12 refuses a
    // pixel shader whose inputs are not its vertex shader's outputs.
    const bool unreadable = isnan(cut.color.x + float(cut.material) + cut.uv.x);
    if (evaluated.opacity < states.z || noise >= covers || unreadable)
    {
        discard;
    }
}
