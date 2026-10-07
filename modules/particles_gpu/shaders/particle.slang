// The particles and ribbons (ADR-0053, D352 to D354, D357), in Slang
// (ADR-0026, D475), from which the SPIR-V and the WGSL are compiled.
//
// Vertex entry "vs": one square facing the eye for each slot of an
// emitter's ring, drawn as its instance, where its age puts it. Vertex
// entry "ribbon": a trail's or a beam's points joined by quads facing the
// eye. Fragment entry "fs", the fragments of both.

// The view (D353, D357): its projection of the World relative to the eye;
// the eye's right and up in the World's axes, and the particle clock now
// and its period; and the near plane, and one where the view is flat (a
// canvas's), facing straight down its forward.
struct View
{
    float4x4 viewProjection;
    float4 right;
    float4 up;
    float4 lens;
}

// The emitter, as the spawn reads it (D352, D353).
struct Emitter
{
    float4 anchor;
    float4 origin;
    float4 direction;
    float4 motion;
    float4 acceleration;
    float4 sizes;
    float4 colorStart;
    float4 colorEnd;
    uint4 ring;
    uint4 more;
    float4 inherited;
}

struct Particle
{
    float4 start;
    float4 velocity;
    float4 extra;
}

// The ribbon: its first point among the frame's, and how many.
struct Ribbon
{
    uint4 range;
}

// A point: where it is relative to the eye and its width; its color; and
// its texture's coordinate along the ribbon.
struct Point
{
    float4 placeWidth;
    float4 color;
    float4 along;
}

// The material (D357): its color, and what multiplies its texture's; its
// emission, and what multiplies its emission texture's color; each
// texture's scale and offset; and its flags: one where its texture's
// alpha shapes it, two where it multiplies what is behind, four where it
// is drawn over everything, nothing hiding it (D464).
struct Material
{
    float4 color;
    float4 colorTexture;
    float4 emission;
    float4 emissionTexture;
    float4 baseMap;
    float4 emissionMap;
    uint4 flags;
}

// The device's exposure (D293): its second the multiplier on light.
struct Exposure
{
    float4 value;
}

[[vk::binding(0, 0)]] ConstantBuffer<View> view;
// Slots 1 and 2 are a particle's emitter and pool for "vs", and a ribbon's
// range and points for "ribbon": each pipeline holds one of the two.
[[vk::binding(1, 0)]] ConstantBuffer<Emitter> emitter;
[[vk::binding(1, 0)]] ConstantBuffer<Ribbon> drawnRibbon;
[[vk::binding(2, 0)]] StructuredBuffer<Particle> particles;
[[vk::binding(2, 0)]] StructuredBuffer<Point> points;
[[vk::binding(3, 0)]] StructuredBuffer<Exposure> exposure;
[[vk::binding(4, 0)]] ConstantBuffer<Material> material;
[[vk::binding(5, 0)]] DepthTexture2D depth;
[[vk::binding(6, 0)]] Texture2D baseTexture;
[[vk::binding(7, 0)]] SamplerState baseSampler;
[[vk::binding(8, 0)]] Texture2D emissionTexture;
[[vk::binding(9, 0)]] SamplerState emissionSampler;

struct Corner
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] float4 color;
    // How deep the particle fades into what is behind it: half its size.
    [[vk::location(2)]] float soft;
    // How far from its middle toward its edges, a particle's both ways, a
    // ribbon's across it.
    [[vk::location(3)]] float2 shape;
}

static const float kPeriod = 4096.0;

// The quads a streak is cut into (D360).
static const uint kStreakSegments = 8u;

// Corners of a quad, two triangles: (0,0), (1,0), (0,1), (0,1), (1,0),
// (1,1).
static const uint kCorners[6] = { 0u, 1u, 2u, 2u, 1u, 3u };

// Where x'' = a - k x' takes a particle from rest at its start by `age`:
// in closed form with drag, uniformly accelerated without.
float3 movedAt(Particle particle, float age)
{
    const float drag = emitter.motion.z;
    const float3 acceleration = emitter.acceleration.xyz;
    if (drag > 1e-4)
    {
        const float slowed = (1.0 - exp(-drag * age)) / drag;
        return particle.velocity.xyz * slowed + acceleration * (age - slowed) / drag;
    }
    return particle.velocity.xyz * age + 0.5 * acceleration * age * age;
}

// A particle where its age puts it: from where it started, its velocity
// slowed by its drag and turned by its acceleration in closed form, its
// size and color between its birth's and its death's, and its flipbook's
// cell by its age (D359); or, for an emitter with a streak, a ribbon
// through where it was over its last moments (D360); none for a slot whose
// particle is dead or was never born.
[shader("vertex")]
Corner vs(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
    const Particle particle = particles[instance];
    // A remainder toward minus infinity, as GLSL's mod (fmod truncates).
    const float since = view.right.w - particle.start.w + kPeriod;
    const float age = since - kPeriod * floor(since / kPeriod);
    const float life = particle.velocity.w;
    Corner corner;
    if (life <= 0.0 || age >= life)
    {
        corner.uv = float2(0.0);
        corner.color = float4(0.0);
        corner.soft = 1.0;
        corner.shape = float2(0.0);
        corner.position = float4(2.0, 2.0, 2.0, 1.0);
        return corner;
    }
    const float through = age / life;
    const float size = lerp(emitter.sizes.x, emitter.sizes.y, through) * particle.extra.x;
    const float3 start = emitter.anchor.xyz + particle.start.xyz;
    const uint at = kCorners[vertex % 6];
    corner.color = lerp(emitter.colorStart, emitter.colorEnd, through);
    corner.soft = max(0.5 * size, 1e-4);
    const float streak = emitter.inherited.w;
    if (streak > 0.0)
    {
        // A streak (D360): its segments through where it was over its last
        // `streak` seconds (none before its birth), facing the eye, its
        // size across at its head narrowing to nothing.
        const uint joint = vertex / 6u + (at >> 1u);
        const float step = streak / float(kStreakSegments);
        const float back = step * float(joint);
        const float3 here = start + movedAt(particle, max(age - back, 0.0));
        const float3 runs =
            movedAt(particle, max(age - back + step, 0.0)) - movedAt(particle, max(age - back - step, 0.0));
        const float3 toEye = view.lens.y > 0.5 ? cross(view.right.xyz, view.up.xyz) : -here;
        float3 across = cross(runs, toEye);
        across = dot(across, across) > 1e-12 ? normalize(across) : view.up.xyz;
        const float side = float(at & 1u) - 0.5;
        const float width = size * (1.0 - float(joint) / float(kStreakSegments));
        corner.uv = float2(float(joint) / float(kStreakSegments), float(at & 1u));
        corner.shape = float2(0.0, side * 2.0);
        corner.position = mul(view.viewProjection, float4(here + across * (side * width), 1.0));
        return corner;
    }
    const float2 square = float2(float(at & 1u), float(at >> 1u));
    const float3 placed = start + movedAt(particle, age) +
                          (view.right.xyz * (square.x - 0.5) + view.up.xyz * (square.y - 0.5)) * size;
    // Its flipbook's cell, stepped over its life (D359), left to right and
    // top to bottom; the whole texture where it has none.
    const uint2 cells = uint2(emitter.more.w & 0xFFFFu, emitter.more.w >> 16u);
    const uint frames = max(cells.x * cells.y, 1u);
    const uint frame = min(uint(through * float(frames)), frames - 1u);
    const float2 cell = float2(float(frame % max(cells.x, 1u)), float(frame / max(cells.x, 1u)));
    corner.uv = (cell + float2(square.x, 1.0 - square.y)) / float2(max(cells, uint2(1u)));
    corner.shape = square * 2.0 - 1.0;
    corner.position = mul(view.viewProjection, float4(placed, 1.0));
    return corner;
}

// The ribbons (D354): two triangles between each point and the next, each
// point's edges across the way the ribbon runs there (from the point
// before it to the one after) and the way to the eye, half its width each
// side.
[shader("vertex")]
Corner ribbon(uint vertex : SV_VulkanVertexID)
{
    // Corners of a segment: its first point's two edges, then its next's.
    const uint at = kCorners[vertex % 6];
    const uint first = drawnRibbon.range.x;
    const uint last = drawnRibbon.range.x + drawnRibbon.range.y - 1u;
    const uint index = first + vertex / 6u + (at >> 1u);
    const Point node = points[index];
    const float3 runs = points[min(index + 1u, last)].placeWidth.xyz - points[max(index, first + 1u) - 1u].placeWidth.xyz;
    // The way to the eye: back along a flat view's forward, else from the
    // point to the eye.
    const float3 toEye = view.lens.y > 0.5 ? cross(view.right.xyz, view.up.xyz) : -node.placeWidth.xyz;
    float3 across = cross(runs, toEye);
    across = dot(across, across) > 1e-12 ? normalize(across) : view.right.xyz;
    const float side = float(at & 1u) - 0.5;
    Corner corner;
    corner.uv = float2(node.along.x, float(at & 1u));
    corner.color = node.color;
    corner.soft = max(0.5 * node.placeWidth.w, 1e-4);
    corner.shape = float2(0.0, side * 2.0);
    corner.position = mul(view.viewProjection, float4(node.placeWidth.xyz + across * (side * node.placeWidth.w), 1.0));
    return corner;
}

// Unlit, a particle's or a ribbon's color times its material's color, a
// constant plus a constant times its texture's, standing as it is whatever
// the exposure, as an unlit model's does; soft at its edges (a particle a
// disc, a ribbon along its sides) where the texture's alpha does not shape
// it; its material's emission added, a constant plus a constant times its
// emission texture's, in physical units times the exposure the device
// holds (D293), one on a canvas. Hidden behind the depth it is given, and
// faded where it meets it across half its size, so it never cuts a hard
// line through the surface behind; a canvas gives a depth of nought, the
// farthest, and nothing is hidden or faded. Written premultiplied, so its
// emission adds even where it covers nothing; or, for a material that
// multiplies, what multiplies what is behind.
[shader("fragment")]
float4 fs(Corner corner) : SV_Target0
{
    const float4 sampled = baseTexture.Sample(baseSampler, corner.uv * material.baseMap.xy + material.baseMap.zw);
    const float3 glowing =
        emissionTexture.Sample(emissionSampler, corner.uv * material.emissionMap.xy + material.emissionMap.zw).rgb;
    // Behind the depth it is given (reversed-Z: nearer is greater), hidden,
    // once its textures are sampled; nearer, faded in over half its size.
    // The sky is never in front. Its texel, held to the depth's sides: a
    // canvas's is one texel. A material drawn over everything (flag four,
    // a line's, D464) is neither hidden nor faded: the depth is taken as
    // the farthest.
    const bool over = (material.flags.x & 4u) != 0u;
    uint width;
    uint height;
    depth.GetDimensions(width, height);
    const int2 texel = min(int2(corner.position.xy), int2(int(width), int(height)) - 1);
    const float behind = over ? 0.0 : depth.Load(int3(texel, 0));
    if (corner.position.z < behind)
    {
        discard;
    }
    const float fade =
        behind > 0.0 ? clamp((view.lens.x / behind - view.lens.x / corner.position.z) / corner.soft, 0.0, 1.0) : 1.0;
    const bool shaped = (material.flags.x & 1u) != 0u;
    const float4 tinted = material.color + material.colorTexture * sampled;
    const float disc = shaped ? sampled.a : 1.0 - smoothstep(0.5, 1.0, length(corner.shape));
    const float3 color = corner.color.rgb * tinted.rgb;
    const float opacity = clamp(tinted.a * corner.color.a * (shaped ? 1.0 : disc) * fade, 0.0, 1.0);
    if ((material.flags.x & 2u) != 0u)
    {
        return float4(color * opacity + float3(1.0 - opacity), opacity);
    }
    // Its emission in physical units, over the coverage alone: a material
    // of no opacity only adds its light.
    const float3 glow = (material.emission.rgb + material.emissionTexture.rgb * glowing) * corner.color.a * disc * fade *
                        exposure[0].value.y;
    return float4(color * opacity + glow, opacity);
}
