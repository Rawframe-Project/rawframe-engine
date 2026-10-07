// The scene's depth of field (ADR-0051, D336), in Slang (ADR-0026, D475):
// the light halved beside its circles of confusion, Gustafsson's
// single-pass bokeh at that size, and each full-size point blended toward
// it. Each entry reads only where its fragment is, so the vertex half
// passes nothing on (D418).

import screen;

// The circle's radius in pixels for a point infinitely far, the focus
// and the near plane in meters, and the longest radius in pixels.
struct Lens
{
    float4 settings;
}

[[vk::binding(0, 0)]] Texture2D scene;
[[vk::binding(1, 0)]] DepthTexture2D depth;
[[vk::binding(2, 0)]] Texture2D halved;
[[vk::binding(3, 0)]] SamplerState blended;
[[vk::binding(4, 0)]] ConstantBuffer<Lens> lens;
// The halved light, for each half-size texel's circle.
[[vk::binding(5, 0)]] Texture2D circles;

static const float kGoldenAngle = 2.39996323;

int2 sizeOf(Texture2D texture)
{
    uint width;
    uint height;
    texture.GetDimensions(width, height);
    return int2(int(width), int(height));
}

[shader("vertex")]
Bare vs(uint vertex : SV_VulkanVertexID)
{
    return bareOf(vertex);
}

// The light at half the target's size, each texel the mean of the four it
// covers, beside its circle of confusion's radius in half-size pixels,
// signed (below nought nearer than the focus), taken at the nearest of the
// four so what stands in front keeps its blur at its edges.
[shader("fragment")]
float4 prefilter(Bare bare) : SV_Target0
{
    const int2 size = sizeOf(scene);
    const int2 first = int2(bare.position.xy) * 2;
    float3 light = float3(0.0);
    float nearest = 0.0;
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            const int2 at = min(first + int2(x, y), size - 1);
            light += scene.Load(int3(at, 0)).rgb;
            nearest = max(nearest, depth.Load(int3(at, 0)));
        }
    }
    const float ahead = lens.settings.z / max(nearest, 1e-7);
    const float radius = lens.settings.x * (ahead - lens.settings.y) / ahead;
    return float4(light * 0.25, clamp(radius, -lens.settings.w, lens.settings.w) * 0.5);
}

// Taps on a golden-angle spiral out to the longest radius, each taken
// where its own circle reaches the point; one behind the point spreads no
// more than twice the point's circle, so a sharp subject does not take in
// the blurred background. Beside the light, how far the blur that reaches
// the point spreads: its own circle, or a nearer one's over it.
[shader("fragment")]
float4 bokeh(Bare bare) : SV_Target0
{
    const float2 size = float2(sizeOf(halved));
    const float4 here = halved.Load(int3(int2(bare.position.xy), 0));
    const float own = abs(here.a);
    const float longest = lens.settings.w * 0.5;
    float3 light = here.rgb;
    float taps = 1.0;
    float reach = own;
    float radius = 1.0;
    float angle = 0.0;
    for (int tap = 0; tap < 512 && radius < longest; ++tap)
    {
        const float2 at = (bare.position.xy + float2(cos(angle), sin(angle)) * radius) / size;
        const float4 there = halved.SampleLevel(blended, at, 0.0);
        const bool behind = there.a > here.a;
        const float circle = behind ? min(abs(there.a), own * 2.0) : abs(there.a);
        const float taken = smoothstep(radius - 0.5, radius + 0.5, circle);
        light += lerp(light / taps, there.rgb, taken);
        taps += 1.0;
        if (!behind)
        {
            reach = max(reach, taken * circle);
        }
        radius += 1.0 / radius;
        angle += kGoldenAngle;
    }
    return float4(light / taps, reach);
}

// Each point of the full-size light, blended toward the half-size bokeh as
// far as its own circle or a nearer one's over it reaches past a pixel.
// The bokeh is taken from the four half-size texels about the point, each
// weighed by its nearness and by how alike its circle is to the point's,
// so a blurred background does not take in a sharp subject's edge.
[shader("fragment")]
float4 combine(Bare bare) : SV_Target0
{
    const int2 at = int2(bare.position.xy);
    const float4 sharp = scene.Load(int3(at, 0));
    const float ahead = lens.settings.z / max(depth.Load(int3(at, 0)), 1e-7);
    const float circle =
        clamp(lens.settings.x * (ahead - lens.settings.y) / ahead, -lens.settings.w, lens.settings.w) * 0.5;
    const int2 size = sizeOf(halved);
    const float2 across = bare.position.xy * 0.5 - 0.5;
    const int2 first = int2(floor(across));
    const float2 fraction = across - float2(first);
    float4 blurred = float4(0.0);
    float weight = 0.0;
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            const int2 texel = clamp(first + int2(x, y), int2(0), size - 1);
            const float2 nearness = lerp(1.0 - fraction, fraction, float2(float(x), float(y)));
            const float alike = 1.0 / (0.05 + abs(circles.Load(int3(texel, 0)).a - circle));
            const float weighed = nearness.x * nearness.y * alike + 1e-6;
            blurred += halved.Load(int3(texel, 0)) * weighed;
            weight += weighed;
        }
    }
    blurred /= weight;
    const float blend = smoothstep(0.5, 1.5, max(abs(circle) * 2.0, blurred.a * 2.0));
    return float4(lerp(sharp.rgb, blurred.rgb, blend), sharp.a);
}
