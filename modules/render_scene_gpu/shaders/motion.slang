// The scene's motion blur (ADR-0051, D334), in Slang (ADR-0026, D475),
// after McGuire et al. 2012: the longest blur in each tile, then among
// each tile and its neighbours, then the reconstruction filter. A point's
// blur is half its motion over the frame's time the shutter is open, in
// pixels, either way from it. Each entry reads only where its fragment is,
// so the vertex half passes nothing on (D418).

import screen;

// Half the shutter's share of the frame, a tile's side and the longest
// blur in pixels, and the near plane.
struct Blur
{
    float4 settings;
}

[[vk::binding(0, 0)]] Texture2D scene;
[[vk::binding(1, 0)]] Texture2D motion;
[[vk::binding(2, 0)]] DepthTexture2D depth;
[[vk::binding(3, 0)]] Texture2D tiles;
[[vk::binding(4, 0)]] ConstantBuffer<Blur> blur;

static const int kTaps = 15;

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

// The longest blur within each tile of the target, a tile as many pixels
// across as the longest blur's reach (TileMax).
[shader("fragment")]
float2 tile(Bare bare) : SV_Target0
{
    const int side = int(blur.settings.y);
    const int2 size = sizeOf(motion);
    const int2 first = int2(bare.position.xy) * side;
    float2 longest = float2(0.0);
    for (int y = 0; y < side; ++y)
    {
        for (int x = 0; x < side; ++x)
        {
            const int2 at = min(first + int2(x, y), size - 1);
            const float2 spread = motion.Load(int3(at, 0)).xy * float2(size) * blur.settings.x;
            if (dot(spread, spread) > dot(longest, longest))
            {
                longest = spread;
            }
        }
    }
    const float reach = length(longest);
    return reach > blur.settings.y ? longest * (blur.settings.y / reach) : longest;
}

// The longest blur among each tile and the eight around it
// (NeighborMax), what any point of the tile can be reached by.
[shader("fragment")]
float2 neighbor(Bare bare) : SV_Target0
{
    const int2 size = sizeOf(tiles);
    const int2 at = int2(bare.position.xy);
    float2 longest = float2(0.0);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 spread = tiles.Load(int3(clamp(at + int2(x, y), int2(0), size - 1), 0)).xy;
            if (dot(spread, spread) > dot(longest, longest))
            {
                longest = spread;
            }
        }
    }
    return longest;
}

// A point's blur in pixels, at most the longest.
float2 blurAt(int2 at, float2 size)
{
    const float2 spread = motion.Load(int3(at, 0)).xy * size * blur.settings.x;
    const float reach = length(spread);
    return reach > blur.settings.y ? spread * (blur.settings.y / reach) : spread;
}

// How far ahead a point is, in meters (reversed-Z, infinite far).
float aheadAt(int2 at)
{
    return blur.settings.z / max(depth.Load(int3(at, 0)), 1e-7);
}

// Whether `nearer` stands in front of `farther`, softly over a few
// hundredths of the distance.
float inFront(float nearer, float farther)
{
    return clamp(1.0 - (nearer - farther) / (0.02 * min(nearer, farther)), 0.0, 1.0);
}

float cone(float apart, float reach)
{
    return clamp(1.0 - apart / reach, 0.0, 1.0);
}

float cylinder(float apart, float reach)
{
    return 1.0 - smoothstep(0.95 * reach, 1.05 * reach, apart);
}

// The reconstruction filter. Fifteen taps along the longest blur that
// reaches the point's tile, jittered per pixel, each weighed by whether it
// blurs over the point: a tap in front, by its own blur's reach; a tap
// behind, by the point's (what the point's blur uncovers); and taps both
// moving alike, by both. What stands in front stays sharp over what moves
// behind it, and what moves in front smears over what stands behind.
[shader("fragment")]
float4 gather(Bare bare) : SV_Target0
{
    const int2 at = int2(bare.position.xy);
    const float4 here = scene.Load(int3(at, 0));
    const int side = int(blur.settings.y);
    const float2 longest = tiles.Load(int3(at / side, 0)).xy;
    if (dot(longest, longest) <= 0.25)
    {
        return here;
    }
    const int2 size = sizeOf(scene);
    const float2 sizeF = float2(size);
    const float reach = max(length(blurAt(at, sizeF)), 0.5);
    const float ahead = aheadAt(at);
    // Interleaved gradient noise (Jimenez 2014): the taps' offset, less
    // than one tap's spacing, differing from pixel to pixel.
    const float jitter = frac(52.9829189 * frac(dot(bare.position.xy, float2(0.06711056, 0.00583715)))) - 0.5;
    float weight = 1.0 / reach;
    float4 sum = here * weight;
    for (int tap = 0; tap < kTaps; ++tap)
    {
        if (tap == kTaps / 2)
        {
            continue;
        }
        const float along = lerp(-1.0, 1.0, (float(tap) + jitter + 1.0) / float(kTaps + 1));
        const int2 there = clamp(int2(floor(bare.position.xy + longest * along)), int2(0), size - 1);
        const float apart = length(longest * along);
        const float thereAhead = aheadAt(there);
        const float thereReach = max(length(blurAt(there, sizeF)), 0.5);
        const float taken = inFront(thereAhead, ahead) * cone(apart, thereReach) +
                            inFront(ahead, thereAhead) * cone(apart, reach) +
                            cylinder(apart, thereReach) * cylinder(apart, reach) * 2.0;
        weight += taken;
        sum += scene.Load(int3(there, 0)) * taken;
    }
    return sum / weight;
}
