// The 3D scene's models (D284), in Slang (ADR-0026, D475), from which the
// SPIR-V and the WGSL are compiled.
//
// Vertex entry "vs": a mesh's vertex placed by its instance's model rows
// relative to the eye, then seen through the frame's view and reversed-Z
// projection, jittered. The depth prepass runs the same entry, so both
// land every vertex on the same depth; its position is marked invariant
// after Slang compiles it, which Slang cannot say (D475). Where the vertex
// is seen without the jitter, and where it was seen the frame before, go
// on for its motion (D291).
//
// Fragment entries "fs" and "fsDecaled", the latter for frames that draw
// decals, which it lays over the base color first (D339): the surface its
// material gives it (D303, ADR-0031's blob; the model's color tinting the
// base color), lit through ADR-0031's lit shading model (D299) by the sun,
// where the sun's shadow map says it reaches (D289); by the point and spot
// lights of its cluster (D290), each where its squares of the punctual
// shadows' atlas say it reaches (D292); and by the sky (brighter facing
// up, and seen in reflection), or by its picture, all around and reflected
// by roughness (D322), its reflection the reflection probes' of its
// cluster that hold the point, projected onto each probe's box (D325,
// D340); in physical units, times the exposure the device holds (the
// camera's, or its metering's, D293), so the scene target holds
// pre-exposed scene-linear light (ADR-0047). And how far the point moved
// on the screen since the frame before, for the temporal pass (D291).
//
// Fragment entries "cut", "normal", and "cutNormal", for the depth
// prepass (D310, D327): masked models cut where their opacity falls below
// their alpha cutoff, and each point's normal and roughness where a
// screen-space effect asks for them.

import material;

// The material the scene is linked with (D481, D482): the blob's for the
// containers the engine ships, or one compiled for a game.
extern struct Material : IMaterial;

struct Frame
{
    float4x4 viewProjection;
    float4 toSun;
    float4 sun;
    float4 sky;
    float4 exposure;
    // The eye's forward; each cascade's far end and texel; the cascades,
    // the shadows' distance, a cascade's side in texels (D289), and one
    // where the shadows are filtered soft (D330).
    float4 forward;
    float4 cascadeFar;
    float4 cascadeTexel;
    float4 shadow;
    float4x4 cascades[4];
    // The clusters' tiles across and down, their slices, and the lights;
    // their near end, and the slices over the log of their far over near
    // (D290).
    float4 clusterGrid;
    float4 clusterDepth;
    // The view and projection without the jitter, and the frame before's
    // taking this frame's places (D291).
    float4x4 unjittered;
    float4x4 previous;
    // The ground's luminance below the horizon (D304).
    float4 ground;
    // The sky's picture (D322): its levels, nought for none; and the
    // irradiance over π it gives, per unit of the sky's light, as nine
    // spherical harmonics' coefficients.
    float4 environment;
    float4 irradiance[9];
    // Whether the view's ambient occlusion is on (D327), its screen-space
    // reflections (D331), its contact shadows (D338), and whether it has
    // decals (D339).
    float4 occlusion;
    float4 reflections;
    float4 contact;
    float4 decals;
}

// A point or spot light (D290): its place relative to the eye and its
// range; its intensity in candela, and whether it is a spot; the way a spot
// shines; the cosines of its cone's inner and outer edges; and its first
// square of the shadows' atlas and how many, none without shadows (D292).
struct Light
{
    float4 placeRange;
    float4 intensity;
    float4 direction;
    float4 cone;
    float4 shadow;
}

// A square of the punctual shadows' atlas (D292): where it lies in the
// atlas and its side, as fractions of the atlas, and its near plane; the
// light's axes across, up, and ahead, with how wide it sees; and the
// light's place, with a texel's width a meter ahead.
struct ShadowSlot
{
    float4 rect;
    float4 right;
    float4 up;
    float4 forward;
    float4 position;
}

// The exposure the device holds (D293): its EV100 and the factor it scales
// light by, a metered camera's moved from frame to frame.
struct Exposure
{
    float4 value;
}

// The frame's reflection probes (D325, D340): each one's box's middle
// relative to the eye and its cube of the atlas, below nought for none;
// its half sides; and its light's scale.
struct Probe
{
    float4 place;
    float4 extent;
    float4 light;
}

// The frame's decals (D339): each one's box, tint, and layer of the atlas
// its texture is drawn in, below nought for none.
struct Decal
{
    float4x4 toBox;
    float4 color;
    float4 layer;
}

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] DepthTexture2D shadowMap;
[[vk::binding(2, 0)]] SamplerComparisonState shadowSampler;
// The frame's lights; each cluster's first index and count; the indices.
[[vk::binding(3, 0)]] StructuredBuffer<Light> lights;
[[vk::binding(4, 0)]] StructuredBuffer<uint4> ranges;
[[vk::binding(5, 0)]] StructuredBuffer<uint> indices;
// The punctual shadows' atlas, and its squares.
[[vk::binding(6, 0)]] DepthTexture2D lightShadowMap;
[[vk::binding(7, 0)]] StructuredBuffer<ShadowSlot> slots;
[[vk::binding(8, 0)]] StructuredBuffer<Exposure> exposure;
// The sky's picture (D322), each level its light as a rougher surface
// reflects it.
[[vk::binding(18, 0)]] TextureCube environmentTexture;
[[vk::binding(19, 0)]] SamplerState environmentSampler;
[[vk::binding(20, 0)]] StructuredBuffer<Probe> probes;
// What of the light from all around reaches each texel (D327), where the
// view's ambient occlusion is on: at half the target's size.
[[vk::binding(21, 0)]] Texture2D occlusionTexture;
// What each texel's reflection met in the picture before, pre-exposed, and
// how much of it to take (D331), where the view's screen-space reflections
// are on.
[[vk::binding(22, 0)]] Texture2D reflectionTexture;
// How much of the sun's light the contact shadows let reach each texel
// (D338), where the view's contact shadows are on.
[[vk::binding(23, 0)]] Texture2D contactTexture;
[[vk::binding(24, 0)]] StructuredBuffer<Decal> decals;
[[vk::binding(25, 0)]] Texture2DArray decalAtlas;
// The probes' pictures (D340), each cube's eight mips the light as ever
// rougher surfaces reflect it.
[[vk::binding(26, 0)]] TextureCubeArray probeAtlas;
// The decals' normal textures (D342), linear.
[[vk::binding(27, 0)]] Texture2DArray decalNormals;

// What the vertex half passes on.
struct Corner
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal;
    [[vk::location(1)]] float4 color;
    [[vk::location(2)]] float3 placed;
    [[vk::location(3)]] float3 now;
    [[vk::location(4)]] float3 before;
    // The model's material's place among the frame's materials.
    [[vk::location(5)]] nointerpolation uint material;
    [[vk::location(6)]] float2 uv;
    [[vk::location(7)]] float4 tangent;
}

// A mesh's vertex and its instance's rows.
struct Vertex
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
    [[vk::location(9)]] float4 previous0;
    [[vk::location(10)]] float4 previous1;
    [[vk::location(11)]] float4 previous2;
    [[vk::location(12)]] float material;
    // The vertex's texture coordinates, the mesh's first set (D309).
    [[vk::location(13)]] float2 uv;
    // The vertex's tangent and the bitangent's sign (D313).
    [[vk::location(14)]] float4 tangent;
}

[shader("vertex")]
Corner vs(Vertex vertex)
{
    const float4 at = float4(vertex.position, 1.0);
    const float3 placed = float3(dot(vertex.model0, at), dot(vertex.model1, at), dot(vertex.model2, at));
    Corner corner;
    corner.position = mul(frame.viewProjection, float4(placed, 1.0));
    // The normal's rows, written as GLSL's columns, so it multiplies them
    // from the left.
    corner.normal = mul(vertex.normal, float3x3(vertex.normal0, vertex.normal1, vertex.normal2));
    corner.color = vertex.color;
    corner.placed = placed;
    const float3 was = float3(dot(vertex.previous0, at), dot(vertex.previous1, at), dot(vertex.previous2, at));
    corner.now = mul(frame.unjittered, float4(placed, 1.0)).xyw;
    corner.before = mul(frame.previous, float4(was, 1.0)).xyw;
    corner.material = uint(vertex.material);
    corner.uv = vertex.uv;
    // A tangent turns with the model; a mirrored model flips its sign.
    const float3 turned = float3(dot(vertex.model0.xyz, vertex.tangent.xyz),
                                 dot(vertex.model1.xyz, vertex.tangent.xyz),
                                 dot(vertex.model2.xyz, vertex.tangent.xyz));
    const float mirror = dot(vertex.model0.xyz, cross(vertex.model1.xyz, vertex.model2.xyz)) < 0.0 ? -1.0 : 1.0;
    corner.tangent = float4(turned, vertex.tangent.w * mirror);
    return corner;
}

// Every input of a prepass entry read, though it needs only some (D418):
// Slang leaves an input never read out of the entry, and Direct3D 12 links
// stages by place, so the rest would not line up with the vertex entry's.
// A point whose inputs are not numbers is dropped; a mesh's never are.
bool unreadable(Corner corner)
{
    return isnan(corner.normal.x + corner.color.x + corner.placed.x + corner.now.x + corner.before.x +
                 float(corner.material) + corner.uv.x + corner.tangent.x);
}

// Where the material is evaluated at `corner` (D482): the coordinates'
// derivatives taken before anything branches.
Point pointOf(Corner corner)
{
    Point point;
    point.uv = corner.uv;
    point.dx = ddx(corner.uv);
    point.dy = ddy(corner.uv);
    point.normal = normalize(corner.normal);
    point.tangent = corner.tangent;
    point.color = corner.color;
    point.material = corner.material;
    return point;
}

// Whether a masked model is cut away where its material gives `evaluated`:
// where its opacity falls below its alpha cutoff (D310), a declared state
// every material's blob holds (D487).
bool cutAway(Corner corner, Evaluated evaluated)
{
    return evaluated.opacity < materials[blobOf(corner.material) + 3u].z;
}

// Each point's normal in the World's axes, turned toward the eye, and the
// roughness its material gives (D327, D487).
float4 surfaceOf(Corner corner, Evaluated evaluated)
{
    const float3 normal = normalize(corner.normal);
    return float4(dot(normal, corner.placed) > 0.0 ? -normal : normal, evaluated.roughness);
}

// The masked models in the depth prepass (D310): where the material's
// opacity falls below its alpha cutoff the model is cut away, so the depth
// the prepass leaves is where the model stands. The lit pass draws a
// masked model only where its depth equals what the prepass left, so it
// needs no cut of its own, and the lit entries never discard, which would
// cost every model its early depth test.
[shader("fragment")]
void cut(Corner corner)
{
    // Sampled before anything branches: WGSL samples a texture with
    // implicit derivatives only in uniform control flow, which a
    // short-circuit would leave.
    const bool away = cutAway(corner, Material.evaluate(pointOf(corner)));
    if (unreadable(corner) || away)
    {
        discard;
    }
}

// The models in the depth prepass when a screen-space effect asks for
// their surfaces (ADR-0051, D327), into the normal-roughness target.
[shader("fragment")]
float4 normal(Corner corner) : SV_Target0
{
    const Evaluated evaluated = Material.evaluate(pointOf(corner));
    if (unreadable(corner))
    {
        discard;
    }
    return surfaceOf(corner, evaluated);
}

// The masked models in the depth prepass when a screen-space effect asks
// for their surfaces (D310, D327): cut as "cut" cuts them, and where they
// stand, their normal and roughness as "normal" gives them.
[shader("fragment")]
float4 cutNormal(Corner corner) : SV_Target0
{
    const Evaluated evaluated = Material.evaluate(pointOf(corner));
    const bool away = cutAway(corner, evaluated);
    if (unreadable(corner) || away)
    {
        discard;
    }
    return surfaceOf(corner, evaluated);
}

static const float kPi = 3.14159265;

// A point's surface as the BRDF takes it: the diffuse color (the base
// color less what metal takes), what it reflects head on (a dielectric's
// by its index of refraction and specular color, or a metal's base color),
// and its roughness.
struct Surface
{
    float3 diffuse;
    float3 headOn;
    float roughness;
}

// Of the light arriving from `toward`, per unit of illuminance, what leaves
// toward the eye, times the cosine it arrives at: glTF's metallic-roughness
// BRDF, the diffuse color's Lambert under Schlick's Fresnel, and GGX with
// Smith's height-correlated visibility.
float3 reflected(Surface surface, float3 normal, float3 toEye, float3 toward)
{
    const float nl = max(dot(normal, toward), 0.0);
    if (nl <= 0.0)
    {
        return float3(0.0);
    }
    const float3 halfway = normalize(toEye + toward);
    const float nv = max(dot(normal, toEye), 1e-4);
    const float nh = max(dot(normal, halfway), 0.0);
    const float alpha = max(surface.roughness * surface.roughness, 1e-3);
    const float alpha2 = alpha * alpha;
    const float3 fresnel = surface.headOn + (1.0 - surface.headOn) * pow(1.0 - max(dot(toEye, halfway), 0.0), 5.0);
    const float spread = nh * nh * (alpha2 - 1.0) + 1.0;
    const float distribution = alpha2 / (kPi * spread * spread);
    const float visibility =
        0.5 / (nl * sqrt(nv * nv * (1.0 - alpha2) + alpha2) + nv * sqrt(nl * nl * (1.0 - alpha2) + alpha2));
    return ((1.0 - fresnel) * surface.diffuse / kPi + fresnel * (distribution * visibility)) * nl;
}

// The irradiance over π the sky's picture gives a surface facing `normal`,
// per unit of the sky's light: its spherical harmonics summed.
float3 irradianceAt(float3 normal)
{
    const float3 sum = frame.irradiance[0].rgb * 0.282095 +
                       (frame.irradiance[1].rgb * normal.y + frame.irradiance[2].rgb * normal.z +
                        frame.irradiance[3].rgb * normal.x) *
                           0.488603 +
                       (frame.irradiance[4].rgb * (normal.x * normal.y) +
                        frame.irradiance[5].rgb * (normal.y * normal.z) +
                        frame.irradiance[7].rgb * (normal.x * normal.z)) *
                           1.092548 +
                       frame.irradiance[6].rgb * (0.315392 * (3.0 * normal.z * normal.z - 1.0)) +
                       frame.irradiance[8].rgb * (0.546274 * (normal.x * normal.x - normal.y * normal.y));
    return max(sum, float3(0.0));
}

// Of the light a surface reflects from all around, the scale on what it
// reflects head on and the bias added: Karis's fit of the split sum's
// second half, over the roughness and the cosine to the eye.
float2 environmentBrdf(float roughness, float nv)
{
    const float4 fit = roughness * float4(-1.0, -0.0275, -0.572, 0.022) + float4(1.0, 0.0425, 1.04, -0.04);
    const float a = min(fit.x * fit.x, exp2(-9.28 * nv)) * fit.x + fit.y;
    return float2(-1.04, 1.04) * a + fit.zw;
}

// ADR-0051's middle shadow filter (D330), Castaño's optimized PCF: a five
// by five tent over the map's texels about `texel` (in texels), taken as
// nine of the hardware's blends of four, each tap's middle in the map's
// coordinates, of a map `size` texels square, and its weight, the nine
// summing to one.
struct Taps
{
    float2 at[9];
    float weight[9];
}

Taps tapsAbout(float2 texel, float2 size)
{
    const float2 base = floor(texel + 0.5);
    const float2 into = texel + 0.5 - base;
    const float3 us = float3(4.0 - 3.0 * into.x, 7.0, 1.0 + 3.0 * into.x);
    const float3 vs = float3(4.0 - 3.0 * into.y, 7.0, 1.0 + 3.0 * into.y);
    const float3 u = float3((3.0 - 2.0 * into.x) / us.x - 2.0, (3.0 + into.x) / us.y, into.x / us.z + 2.0);
    const float3 v = float3((3.0 - 2.0 * into.y) / vs.x - 2.0, (3.0 + into.y) / vs.y, into.y / vs.z + 2.0);
    Taps made;
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            made.at[row * 3 + column] = (base - 0.5 + float2(u[column], v[row])) / size;
            made.weight[row * 3 + column] = us[column] * vs[row] / 144.0;
        }
    }
    return made;
}

// A place on the golden angle's spiral of sixteen within the unit disc,
// the spiral turned by `turn` radians.
float2 spiral(int tap, float turn)
{
    const float angle = float(tap) * 2.3999632 + turn;
    return sqrt((float(tap) + 0.5) / 16.0) * float2(cos(angle), sin(angle));
}

float2 sizeOf(DepthTexture2D map)
{
    uint width;
    uint height;
    map.GetDimensions(width, height);
    return float2(float(width), float(height));
}

// ADR-0051's top shadow filter (D347), contact hardening (PCSS): the sun's
// shadow at `inMap`, `depth` in cascade `at`, as sharp where it meets its
// caster as the map lets it be, and wider with the distance from it, as
// the sun's disc makes it. Sixteen of the map's depths within eight texels
// find the casters above the point; their mean height above it in meters
// (the cascade's depth is linear in them) times the sun's width is the
// penumbra, from a texel to the search's eight; then sixteen of the
// hardware's blends of four across it. Both spirals are turned by the
// pixel, the noise the temporal pass smooths.
float hardened(float2 inMap, float depth, int at, float2 pixel)
{
    const float2 size = sizeOf(shadowMap);
    // The cascade's depth row, GLSL's third component of each column.
    const float perMeter = length(frame.cascades[at][2].xyz);
    const float turn = 6.2831853 * frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float above = 0.0;
    float casters = 0.0;
    for (int tap = 0; tap < 16; ++tap)
    {
        const float2 there = inMap + spiral(tap, turn) * (8.0 / size);
        const float nearer = shadowMap.Load(int3(int2(there * size), 0));
        if (nearer > depth)
        {
            above += nearer - depth;
            casters += 1.0;
        }
    }
    if (casters == 0.0)
    {
        return 1.0;
    }
    const float penumbra = clamp(above / (casters * perMeter) * frame.sun.w / frame.cascadeTexel[at], 1.0, 8.0);
    float lit = 0.0;
    for (int tap = 0; tap < 16; ++tap)
    {
        lit += shadowMap.SampleCmpLevelZero(shadowSampler, inMap + spiral(tap, turn) * (penumbra / size), depth);
    }
    return lit / 16.0;
}

// How much of the sun reaches `placed`: its cascade chosen by how far ahead
// it is, the point moved along its normal by a texel and a half of it (the
// normal bias), and the map compared there, nearer the sun being greater
// (reversed-Z), four texels blended (hardware 2x2 PCF). Past the shadows'
// distance the sun reaches everything, fading in over its last tenth.
float sunlit(float3 placed, float3 normal, float2 pixel)
{
    const int count = int(frame.shadow.x);
    const float ahead = dot(placed, frame.forward.xyz);
    if (count == 0 || ahead > frame.shadow.y)
    {
        return 1.0;
    }
    int at = 0;
    while (at < count - 1 && ahead > frame.cascadeFar[at])
    {
        at += 1;
    }
    const float4 clip = mul(frame.cascades[at], float4(placed + normal * (frame.cascadeTexel[at] * 1.5), 1.0));
    // Within the cascade's square, kept from its edges so the texels
    // blended are its own (half a texel, three filtered soft, nine
    // hardening at contact); the squares tile the map two by two.
    const bool soft = frame.shadow.w > 0.5;
    const bool hardening = frame.shadow.w > 1.5;
    const float halfTexel = (hardening ? 9.0 : soft ? 3.0 : 0.5) / frame.shadow.z;
    const float2 inSquare =
        clamp(float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5), float2(halfTexel), float2(1.0 - halfTexel));
    const float2 inMap = (inSquare + float2(float(at % 2), float(at / 2))) * 0.5;
    float lit = 0.0;
    if (hardening)
    {
        lit = hardened(inMap, clip.z, at, pixel);
    }
    else if (soft)
    {
        const float2 size = sizeOf(shadowMap);
        const Taps taps = tapsAbout(inMap * size, size);
        for (int tap = 0; tap < 9; ++tap)
        {
            lit += taps.weight[tap] * shadowMap.SampleCmpLevelZero(shadowSampler, taps.at[tap], clip.z);
        }
    }
    else
    {
        lit = shadowMap.SampleCmpLevelZero(shadowSampler, inMap, clip.z);
    }
    const float fade = clamp((frame.shadow.y - ahead) / (0.1 * frame.shadow.y), 0.0, 1.0);
    return lerp(1.0, lit, fade);
}

// How much of a light reaches `placed` through its square of the atlas:
// the point moved along its normal by a texel and a half there, then
// compared, nearer the light being greater (reversed-Z), four texels
// blended, or twenty-five filtered soft (D330), kept within the square.
float lightShadow(ShadowSlot slot, float3 placed, float3 normal)
{
    const float near = slot.rect.w;
    const float away = max(dot(placed - slot.position.xyz, slot.forward.xyz), near);
    const float3 from = placed + normal * (slot.position.w * away * 1.5) - slot.position.xyz;
    const float ahead = dot(from, slot.forward.xyz);
    if (ahead <= near)
    {
        return 1.0;
    }
    const float2 seen = float2(dot(from, slot.right.xyz), dot(from, slot.up.xyz)) / (ahead * slot.right.w);
    const bool soft = frame.shadow.w > 0.5;
    const float halfTexel = slot.position.w * (soft ? 1.5 : 0.25) / slot.right.w;
    const float2 inSquare =
        clamp(float2(seen.x * 0.5 + 0.5, 0.5 - seen.y * 0.5), float2(halfTexel), float2(1.0 - halfTexel));
    const float2 inAtlas = slot.rect.xy + inSquare * slot.rect.z;
    if (!soft)
    {
        return lightShadowMap.SampleCmpLevelZero(shadowSampler, inAtlas, near / ahead);
    }
    const float2 size = sizeOf(lightShadowMap);
    const Taps taps = tapsAbout(inAtlas * size, size);
    float lit = 0.0;
    for (int tap = 0; tap < 9; ++tap)
    {
        lit += taps.weight[tap] * lightShadowMap.SampleCmpLevelZero(shadowSampler, taps.at[tap], near / ahead);
    }
    return lit;
}

// The cluster `placed` falls in, by where the view puts it and how far
// ahead it is: its first index, its lights, its decals (D339), and its
// probes (D340).
uint4 clusterOf(float3 placed)
{
    const float4 clip = mul(frame.unjittered, float4(placed, 1.0));
    const float2 seen = clip.xy / clip.w;
    const uint3 grid = uint3(frame.clusterGrid.xyz);
    const uint x = min(uint(max((seen.x * 0.5 + 0.5) * frame.clusterGrid.x, 0.0)), grid.x - 1u);
    const uint y = min(uint(max((0.5 - seen.y * 0.5) * frame.clusterGrid.y, 0.0)), grid.y - 1u);
    const float ahead = dot(placed, frame.forward.xyz);
    const uint slice = ahead <= frame.clusterDepth.x
                           ? 0u
                           : min(uint(log(ahead / frame.clusterDepth.x) * frame.clusterDepth.y), grid.z - 1u);
    return ranges[(slice * grid.y + y) * grid.x + x];
}

// The illuminance the point and spot lights of the cluster `range` give
// `placed`: each light's inverse square windowed to nought at its range, a
// spot's also faded across its cone's edge.
float3 punctual(uint4 range, float3 placed, float3 normal, Surface surface, float3 toEye)
{
    float3 sum = float3(0.0);
    for (uint at = range.x; at < range.x + range.y; ++at)
    {
        const Light light = lights[indices[at]];
        const float3 toLight = light.placeRange.xyz - placed;
        const float square = dot(toLight, toLight);
        const float3 toward = toLight * rsqrt(max(square, 1e-8));
        const float reached = square / (light.placeRange.w * light.placeRange.w);
        const float window = clamp(1.0 - reached * reached, 0.0, 1.0);
        float falloff = window * window / max(square, 1e-4);
        if (light.intensity.w > 0.5)
        {
            const float cone =
                clamp((dot(-toward, light.direction.xyz) - light.cone.y) / (light.cone.x - light.cone.y), 0.0, 1.0);
            falloff *= cone * cone;
        }
        if (light.shadow.y > 0.5 && falloff > 0.0)
        {
            // A point's face by the way the surface lies from it: +X, -X,
            // +Y, -Y, +Z, -Z.
            uint slot = uint(light.shadow.x);
            if (light.shadow.y > 1.5)
            {
                const float3 axes = abs(toLight);
                slot += axes.x >= axes.y && axes.x >= axes.z ? (toLight.x <= 0.0 ? 0u : 1u)
                        : axes.y >= axes.z                   ? (toLight.y <= 0.0 ? 2u : 3u)
                                                             : (toLight.z <= 0.0 ? 4u : 5u);
            }
            falloff *= lightShadow(slots[slot], placed, normal);
        }
        sum += light.intensity.rgb * falloff * reflected(surface, normal, toEye, toward);
    }
    return sum;
}

// The surface under the decals of its cluster (D339, D342), each in the
// frame's order over those before: inside a decal's box, its texture at
// the place's spot of it, tinted, covers the color by its alpha, fading
// toward the box's front and back and on surfaces turned from it, `facing`
// the way the surface faces. As much, its normal texture's normal, laid
// along the surface the way the decal's right and up run, covers the
// normal, and a roughness it lays the roughness. Its mip is from the
// place's derivatives across the pixel, `along` and `aside`.
void decalled(uint4 range,
              float3 placed,
              float3 facing,
              float3 along,
              float3 aside,
              inout float3 color,
              inout float3 normal,
              inout float roughness)
{
    for (uint at = range.x + range.y; at < range.x + range.y + range.z; ++at)
    {
        const Decal decal = decals[indices[at]];
        const float3 box = mul(decal.toBox, float4(placed, 1.0)).xyz;
        if (decal.layer.x < 0.0 || any(abs(box) > float3(1.0)))
        {
            continue;
        }
        // The box's upper three by three, its front its third row (GLSL's
        // third component of each column), its right its first.
        const float3x3 into = float3x3(decal.toBox[0].xyz, decal.toBox[1].xyz, decal.toBox[2].xyz);
        const float3 front = normalize(decal.toBox[2].xyz);
        const float2 spot = float2(box.x * 0.5 + 0.5, 0.5 - box.y * 0.5);
        const float2 alongBox = mul(into, along).xy * float2(0.5, -0.5);
        const float2 asideBox = mul(into, aside).xy * float2(0.5, -0.5);
        const float4 texel = decalAtlas.SampleGrad(environmentSampler, float3(spot, decal.layer.x), alongBox, asideBox);
        const float covers = texel.a * decal.color.a * smoothstep(0.0, 0.3, dot(facing, front)) *
                             (1.0 - smoothstep(0.8, 1.0, abs(box.z)));
        color = lerp(color, texel.rgb * decal.color.rgb, covers);
        if (decal.layer.y >= 0.0 && covers > 0.0)
        {
            const float3 bent =
                decalNormals.SampleGrad(environmentSampler, float3(spot, decal.layer.y), alongBox, asideBox).xyz *
                    2.0 -
                1.0;
            const float3 right = decal.toBox[0].xyz;
            const float3 across = normalize(right - normal * dot(normal, right));
            const float3 up = cross(normal, across);
            normal = normalize(lerp(normal, normalize(across * bent.x + up * bent.y + normal * bent.z), covers));
        }
        if (decal.layer.z > 0.0)
        {
            roughness = lerp(roughness, decal.layer.z, covers);
        }
    }
}

// Where a reflection from `placed` along `mirrored` meets the probe's box,
// as seen from its middle, the way its picture was taken: a point outside
// the box as if on its side.
float3 projected(Probe probe, float3 placed, float3 mirrored)
{
    const float3 local = clamp(placed - probe.place.xyz, -probe.extent.xyz, probe.extent.xyz);
    const float3 far = (probe.extent.xyz - local * sign(mirrored)) / max(abs(mirrored), float3(1e-5));
    return local + mirrored * min(min(far.x, far.y), far.z);
}

struct Lit
{
    float4 color : SV_Target0;
    // Where the point is on the target less where it was, in the target's
    // coordinates, rows top first.
    float2 motion : SV_Target1;
}

Lit shaded(Corner corner, bool decaled)
{
    const float2 pixel = corner.position.xy;
    const float3 toEye = normalize(-corner.placed);
    // The coordinates' derivatives, taken before anything branches, so a
    // texture is sampled only where its material has one; and the place's,
    // for the decals' textures (D339).
    const float2 dx = ddx(corner.uv);
    const float2 dy = ddy(corner.uv);
    const float3 along = ddx(corner.placed);
    const float3 aside = ddy(corner.placed);
    // The cluster's lights, decals, and probes, where any is clustered
    // (D339, D340).
    const uint4 range = frame.clusterDepth.y > 0.0 ? clusterOf(corner.placed) : uint4(0u);
    // The surface the material gives (D482).
    Point point;
    point.uv = corner.uv;
    point.dx = dx;
    point.dy = dy;
    point.normal = normalize(corner.normal);
    point.tangent = corner.tangent;
    point.color = corner.color;
    point.material = corner.material;
    const Evaluated evaluated = Material.evaluate(point);
    float3 color = evaluated.color;
    float3 normal = evaluated.normal;
    float roughness = evaluated.roughness;
    // Entry "fsDecaled", for frames with decals: under them.
    if (decaled && frame.decals.x > 0.5)
    {
        decalled(range, corner.placed, normalize(corner.normal), along, aside, color, normal, roughness);
    }
    const float opacity = evaluated.opacity;
    const float metalness = evaluated.metalness;
    const float occlusion = evaluated.occlusion;
    const float3 glowing = evaluated.glowing;
    Lit lit;
    lit.motion = (corner.now.xy / corner.now.z - corner.before.xy / corner.before.z) * float2(0.5, -0.5);
    // Unlit (KHR_materials_unlit): its color stands in the picture as it
    // is, whatever the exposure.
    if (evaluated.unlit)
    {
        lit.color = float4(color, opacity);
        return lit;
    }
    const float reflectance = (evaluated.ior - 1.0) / (evaluated.ior + 1.0);
    Surface surface;
    surface.diffuse = color * (1.0 - metalness);
    surface.headOn = lerp(reflectance * reflectance * evaluated.specular, color, metalness);
    surface.roughness = roughness;
    const int2 texel = int2(pixel);
    // What of the sun's light the contact shadows let through (D338), for
    // an opaque model the prepass saw.
    const float contact =
        frame.contact.x > 0.5 && opacity >= 0.999 ? contactTexture.Load(int3(texel, 0)).r : 1.0;
    const float3 direct = frame.sun.rgb * sunlit(corner.placed, normal, pixel) * contact *
                              reflected(surface, normal, toEye, frame.toSun.xyz) +
                          punctual(range, corner.placed, normal, surface, toEye);
    // The sky above and the ground below (D304): their light across the
    // normal's side, diffused, and along the reflection, the Fresnel of a
    // rough surface; what the material occludes of both.
    const float nv = max(dot(normal, toEye), 0.0);
    const float3 sheen =
        surface.headOn + (max(float3(1.0 - surface.roughness), surface.headOn) - surface.headOn) * pow(1.0 - nv, 5.0);
    const float3 mirrored = reflect(-toEye, normal);
    // With the sky's picture (D322), its irradiance across the normal's
    // side.
    float3 around = lerp(frame.ground.rgb, frame.sky.rgb, 0.5 + 0.5 * normal.y);
    if (frame.environment.w > 0.5)
    {
        around = frame.sky.rgb * irradianceAt(normal);
    }
    // Along the reflection, the cluster's probes that hold the point
    // (D340), the first a point takes first: each its picture's mip for the
    // roughness (of eight), projected onto its box, weighed by the split
    // sum (D322), over what the ones before left, fading out across a
    // tenth of its size past its box.
    const float2 scaleBias = environmentBrdf(surface.roughness, nv);
    const float3 split = surface.headOn * scaleBias.x + scaleBias.y;
    float3 probed = float3(0.0);
    float left = 1.0;
    const uint firstProbe = range.x + range.y + range.z;
    for (uint each = firstProbe; each < firstProbe + range.w && left > 0.0; ++each)
    {
        const Probe probe = probes[indices[each]];
        const float3 past = (abs(corner.placed - probe.place.xyz) - probe.extent.xyz) / (0.1 * probe.extent.xyz);
        const float holds = clamp(1.0 - max(max(past.x, past.y), past.z), 0.0, 1.0);
        if (probe.place.w < 0.0 || holds <= 0.0)
        {
            continue;
        }
        const float3 toward = projected(probe, corner.placed, mirrored);
        probed += left * holds * probe.light.rgb *
                  probeAtlas.SampleLevel(environmentSampler, float4(toward, probe.place.w), surface.roughness * 7.0).rgb;
        left *= 1.0 - holds;
    }
    // What they left, the sky's picture's level for the roughness, by the
    // split sum; with none, the sky above and the ground below, by the
    // sheen.
    float3 alongReflection = split * probed;
    if (left > 0.0)
    {
        float3 beyond = sheen * lerp(frame.ground.rgb, frame.sky.rgb, 0.5 + 0.5 * mirrored.y);
        if (frame.environment.w > 0.5)
        {
            beyond = split * frame.sky.rgb *
                     environmentTexture
                         .SampleLevel(environmentSampler, mirrored, surface.roughness * (frame.environment.w - 1.0))
                         .rgb;
        }
        alongReflection += left * beyond;
    }
    // Over it, what the screen-space reflection met, as much as it found
    // (D331), for an opaque model the prepass saw.
    if (frame.reflections.x > 0.5 && opacity >= 0.999)
    {
        const float4 met = reflectionTexture.Load(int3(texel, 0));
        const float3 weight = left < 1.0 || frame.environment.w > 0.5 ? split : sheen;
        alongReflection = lerp(alongReflection, weight * met.rgb / max(exposure[0].value.y, 1e-12), met.a);
    }
    // What of it reaches the point (D327): for an opaque model, what the
    // ambient occlusion found; a translucent one the prepass never saw
    // takes all of it. An opaque model's alpha, interpolated, can fall a
    // hair short of one.
    const float reaches =
        frame.occlusion.x > 0.5 && opacity >= 0.999 ? occlusionTexture.Load(int3(texel / 2, 0)).r : 1.0;
    const float3 sky = occlusion * reaches * ((1.0 - sheen) * surface.diffuse * around + alongReflection);
    lit.color = float4((direct + sky + glowing) * exposure[0].value.y, opacity);
    return lit;
}

// Every input read whatever the material reads: an input it leaves unread
// is dropped from the entry otherwise, and D3D12 refuses a pixel shader
// whose inputs are not its vertex shader's outputs (D418, D501). Chosen
// after the shading, so nothing it samples is behind a branch, and with no
// discard, which would cost an opaque model its early depth test.
Lit shade(Corner corner, bool decaled)
{
    Lit lit = shaded(corner, decaled);
    lit.color = unreadable(corner) ? float4(0.0) : lit.color;
    return lit;
}

[shader("fragment")]
Lit fs(Corner corner)
{
    return shade(corner, false);
}

[shader("fragment")]
Lit fsDecaled(Corner corner)
{
    return shade(corner, true);
}
