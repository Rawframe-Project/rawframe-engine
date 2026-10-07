// The camera's metering (D293, ADR-0051), in Slang (ADR-0026, D475): the
// frame's light counted into a histogram, then the exposure the device
// holds moved toward what it meters.

// The exposure's EV100 and the factor it scales light by.
struct Exposure
{
    float4 value;
}

// The bounds and rates: minimum, maximum, brighten, darken; and the
// fractions left out, the compensation, and the seconds since the frame
// before: low, high, compensation, elapsed; and whether to go at once, and
// the middle's weight.
struct Meter
{
    float4 bounds;
    float4 fractions;
    float4 snap;
}

[[vk::binding(0, 0)]] Texture2D scene;
[[vk::binding(1, 0)]] RWStructuredBuffer<Exposure> exposure;
[[vk::binding(2, 0)]] RWStructuredBuffer<Atomic<uint>> bins;
[[vk::binding(3, 0)]] ConstantBuffer<Meter> meter;

// Atomic, which WGSL asks of what is added to atomically.
groupshared Atomic<uint> counted[128];

static const float kLowest = -8.0;
static const float kSpan = 32.0;

// Every fourth texel each way of the frame's pre-exposed light, its
// luminance taken back to candela per square meter by the exposure it was
// drawn with, counted into 128 bins: the first for no light, the rest
// spanning log2 luminance from -8 to 24, the last also taking light the
// target could not hold. A texel counts more nearer the middle as the
// camera's metering is centered (D345): up to sixteen times a corner's.
// Each workgroup counts its texels in shared bins, then adds them to the
// frame's.
[shader("compute")]
[numthreads(16, 16, 1)]
void histogram(uint3 invocation : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    if (index < 128u)
    {
        // An exchange, not a store: SPIRV-Cross crosses an atomic store
        // to shared memory into HLSL that does not compile.
        counted[index].exchange(0u);
    }
    GroupMemoryBarrierWithGroupSync();
    uint width;
    uint height;
    scene.GetDimensions(width, height);
    const int2 size = int2(int(width), int(height));
    const int2 texel = int2(invocation.xy) * 4;
    if (texel.x < size.x && texel.y < size.y)
    {
        const float3 color = scene.Load(int3(texel, 0)).rgb;
        const float luminance = dot(color, float3(0.2126, 0.7152, 0.0722)) / max(exposure[0].value.y, 1e-30);
        // Light past what the target holds, or not a number at all, is the
        // brightest there is.
        uint bin = 127u;
        if (luminance <= 0.0)
        {
            bin = 0u;
        }
        else if (luminance <= 3.0e38)
        {
            bin = uint(clamp((log2(luminance) - kLowest) / kSpan * 127.0 + 1.0, 1.0, 127.0));
        }
        const float outward = length((float2(texel) + 0.5) / float2(size) * 2.0 - 1.0) * 0.70710678;
        counted[bin].add(1u + uint(15.0 * meter.snap.y * (1.0 - smoothstep(0.0, 1.0, outward)) + 0.5));
    }
    GroupMemoryBarrierWithGroupSync();
    if (index < 128u)
    {
        const uint inGroup = counted[index].load();
        if (inGroup > 0u)
        {
            bins[index].add(inGroup);
        }
    }
}

// The histogram's lit bins, less the darkest `low` and the brightest past
// `high` of their texels, averaged in log2 luminance into an EV100 (a
// sensor of ISO 100, K 12.5) less the compensation, kept within the
// bounds; the exposure the device holds moved toward it by at most the
// brightening or darkening rate times the seconds since the frame before,
// or set to it at once when the metering starts or the eye cuts away.
[shader("compute")]
[numthreads(1, 1, 1)]
void adapt()
{
    float total = 0.0;
    for (uint bin = 1u; bin < 128u; ++bin)
    {
        total += float(bins[bin].load());
    }
    if (total == 0.0)
    {
        return;
    }
    const float low = meter.fractions.x * total;
    const float high = meter.fractions.y * total;
    float seen = 0.0;
    float sum = 0.0;
    float weight = 0.0;
    for (uint bin = 1u; bin < 128u; ++bin)
    {
        const float count = float(bins[bin].load());
        const float from = max(seen, low);
        const float to = min(seen + count, high);
        if (to > from)
        {
            sum += (to - from) * (kLowest + (float(bin) - 0.5) / 127.0 * kSpan);
            weight += to - from;
        }
        seen += count;
    }
    if (weight == 0.0)
    {
        return;
    }
    const float target = clamp(sum / weight + log2(100.0 / 12.5) - meter.fractions.z, meter.bounds.x, meter.bounds.y);
    const float now = exposure[0].value.x;
    const float change = target - now;
    const float moved = meter.snap.x > 0.5 ? change
                        : change < 0.0       ? max(change, -meter.bounds.z * meter.fractions.w)
                                           : min(change, meter.bounds.w * meter.fractions.w);
    const float ev = clamp(now + moved, meter.bounds.x, meter.bounds.y);
    exposure[0].value = float4(ev, 1.0 / (1.2 * exp2(ev)), 0.0, 0.0);
}
