// The scene's ambient occlusion (ADR-0051, D327), in Slang (ADR-0026,
// D475), found at half the target's size, each texel for the point at its
// top left, then blurred. Both read only where their fragment is, so the
// vertex half passes nothing on (D418).

import screen;

// The jittered view's inverse, from clip space to the eye-relative World,
// and the view itself; the radius, the intensity, and the near plane.
struct Occlusion
{
    float4x4 toPoint;
    float4x4 viewProjection;
    float4 settings;
}

[[vk::binding(0, 0)]] DepthTexture2D depth;
[[vk::binding(1, 0)]] Texture2D surfaces;
[[vk::binding(2, 0)]] ConstantBuffer<Occlusion> occlusion;
[[vk::binding(3, 0)]] Texture2D occluded;

// Points of the hemisphere about +Z within one, denser near the middle.
static const float3 kKernel[12] = { float3(0.2020, 0.0000, 0.0381),
                                    float3(-0.1584, 0.1451, 0.0569),
                                    float3(0.0207, -0.2353, 0.0818),
                                    float3(0.1613, 0.2103, 0.1150),
                                    float3(-0.2948, -0.0521, 0.1589),
                                    float3(0.2842, -0.1808, 0.2158),
                                    float3(-0.0971, 0.3612, 0.2883),
                                    float3(-0.1874, -0.3609, 0.3785),
                                    float3(0.4024, 0.1470, 0.4889),
                                    float3(-0.3968, 0.1638, 0.6218),
                                    float3(0.1658, -0.3544, 0.7795),
                                    float3(0.0789, 0.2517, 0.9646) };

// A turn for each texel of a four-by-four tile, in the order of a Bayer
// matrix, so the blur after takes all sixteen together.
static const uint kBayer[16] = { 0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u };

// The eye-relative point the prepass left at `texel`, whose depth is
// `depthThere`, in a depth of `size`.
float3 pointAt(int2 texel, float depthThere, float2 size)
{
    const float2 middle = (float2(texel) + 0.5) / size;
    const float4 seen = mul(occlusion.toPoint, float4(middle.x * 2.0 - 1.0, 1.0 - middle.y * 2.0, depthThere, 1.0));
    return seen.xyz / seen.w;
}

[shader("vertex")]
Bare vs(uint vertex : SV_VulkanVertexID)
{
    return bareOf(vertex);
}

// For each point the depth prepass left, twelve points of the hemisphere
// above it within the radius, turned about its normal by one of sixteen
// angles in each four-by-four tile, each find the surface the prepass left
// where they are seen. A surface found above the point's own plane hides
// it as much as it rises toward the normal, less the farther it is, and
// not at all past the radius (the Alchemy form): a flat floor seen
// grazing hides nothing of itself. What is not hidden, taken the intensity
// times, is the light from all around that reaches the point. The sky,
// where nothing lies, is not occluded.
[shader("fragment")]
float occlude(Bare bare) : SV_Target0
{
    const int2 halved = int2(bare.position.xy);
    const int2 at = halved * 2;
    const float here = depth.Load(int3(at, 0));
    if (here <= 0.0)
    {
        return 1.0;
    }
    uint width;
    uint height;
    depth.GetDimensions(width, height);
    const float2 size = float2(float(width), float(height));
    const float radius = occlusion.settings.x;
    const float near = occlusion.settings.z;
    const float3 place = pointAt(at, here, size);
    const float3 normal = normalize(surfaces.Load(int3(at, 0)).xyz);
    const float turn = float(kBayer[uint(halved.y & 3) * 4u + uint(halved.x & 3)]) * (6.28318531 / 16.0);
    const float3 helper = abs(normal.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 across = normalize(cross(helper, normal));
    const float3 up = cross(normal, across);
    const float3 tangent = cos(turn) * across + sin(turn) * up;
    const float3 bitangent = cross(normal, tangent);
    float hidden = 0.0;
    for (int taken = 0; taken < 12; ++taken)
    {
        const float3 offset = kKernel[taken];
        const float3 probe = place + (tangent * offset.x + bitangent * offset.y + normal * offset.z) * radius;
        const float4 clip = mul(occlusion.viewProjection, float4(probe, 1.0));
        if (clip.w <= near)
        {
            continue;
        }
        const float2 on = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
        if (any(on < float2(0.0)) || any(on >= float2(1.0)))
        {
            continue;
        }
        // The surface seen at that texel's middle.
        const int2 texel = int2(on * size);
        const float there = depth.Load(int3(texel, 0));
        if (there <= 0.0)
        {
            continue;
        }
        const float3 toward = pointAt(texel, there, size) - place;
        const float far = length(toward);
        if (far > 1e-4 && far < radius)
        {
            const float fade = 1.0 - (far * far) / (radius * radius);
            hidden += max(dot(normal, toward / far) - 0.1, 0.0) * fade;
        }
    }
    // Half the samples rising straight up, near, hide it all.
    return clamp(1.0 - occlusion.settings.y * hidden / 6.0, 0.0, 1.0);
}

// The four-by-four tile about each texel averaged, so its sixteen turns
// count together, leaving out texels whose point is farther from the eye
// than a tenth again, so the occlusion does not bleed across an edge.
[shader("fragment")]
float blur(Bare bare) : SV_Target0
{
    const int2 at = int2(bare.position.xy);
    uint width;
    uint height;
    occluded.GetDimensions(width, height);
    const int2 last = int2(int(width), int(height)) - 1;
    const float here = depth.Load(int3(at * 2, 0));
    float sum = 0.0;
    float weight = 0.0;
    for (int y = -2; y < 2; ++y)
    {
        for (int x = -2; x < 2; ++x)
        {
            const int2 there = clamp(at + int2(x, y), int2(0), last);
            const float thereDepth = depth.Load(int3(there * 2, 0));
            // Depths, as the near plane over the distance ahead: close in
            // distance where their ratio is.
            if (abs(thereDepth - here) <= 0.1 * here)
            {
                sum += occluded.Load(int3(there, 0)).r;
                weight += 1.0;
            }
        }
    }
    return weight > 0.0 ? sum / weight : occluded.Load(int3(at, 0)).r;
}
