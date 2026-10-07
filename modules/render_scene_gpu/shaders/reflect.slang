// The scene's screen-space reflections (ADR-0051, D331), in Slang
// (ADR-0026, D475): from each point the prepass left smooth enough, its
// reflection is followed through the World in steps, each seen from the
// eye, until it passes behind what the prepass left there by less than a
// step and a half; the meeting is narrowed by halving four times, and what
// the view saw there the frame before, where that place was then, is what
// the point reflects. Its weight fades as the surface roughens, toward the
// target's edges, toward the far end of the distance, and for reflections
// turning back toward the eye, which the view cannot have seen. It reads
// only where its fragment is, so the vertex half passes nothing on (D418).

import screen;

// The jittered view's inverse, from clip space to the eye-relative World;
// the view; the frame before's view taking this frame's places; and the
// distance followed and the near plane.
struct Reflections
{
    float4x4 toPoint;
    float4x4 viewProjection;
    float4x4 previous;
    float4 settings;
}

[[vk::binding(0, 0)]] DepthTexture2D depth;
[[vk::binding(1, 0)]] Texture2D surfaces;
[[vk::binding(2, 0)]] Texture2D before;
[[vk::binding(3, 0)]] SamplerState blended;
[[vk::binding(4, 0)]] ConstantBuffer<Reflections> reflections;

static const int kSteps = 32;

// Where a place is on the target, rows top first, and how far ahead.
float3 seenAt(float4x4 view, float3 place)
{
    const float4 clip = mul(view, float4(place, 1.0));
    return float3(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5, clip.w);
}

bool outside(float2 on)
{
    return any(on < float2(0.0)) || any(on >= float2(1.0));
}

[shader("vertex")]
Bare vs(uint vertex : SV_VulkanVertexID)
{
    return bareOf(vertex);
}

[shader("fragment")]
float4 march(Bare bare) : SV_Target0
{
    const int2 at = int2(bare.position.xy);
    const float here = depth.Load(int3(at, 0));
    const float4 surface = surfaces.Load(int3(at, 0));
    const float smooth = 1.0 - smoothstep(0.2, 0.4, surface.w);
    if (here <= 0.0 || smooth <= 0.0)
    {
        return float4(0.0);
    }
    uint width;
    uint height;
    depth.GetDimensions(width, height);
    const float2 size = float2(float(width), float(height));
    const float distance = reflections.settings.x;
    const float near = reflections.settings.y;
    const float2 middle = (float2(at) + 0.5) / size;
    const float4 seen = mul(reflections.toPoint, float4(middle.x * 2.0 - 1.0, 1.0 - middle.y * 2.0, here, 1.0));
    const float3 place = seen.xyz / seen.w;
    const float3 normal = normalize(surface.xyz);
    const float3 toward = reflect(normalize(place), normal);
    // Turning back toward the eye, it meets what the view cannot see.
    const float away = smoothstep(-0.2, 0.2, dot(toward, normalize(place)));
    if (away <= 0.0)
    {
        return float4(0.0);
    }
    const float step = distance / float(kSteps);
    const float thickness = max(step * 1.5, 0.05);
    // From a little off the surface, so it does not meet itself.
    const float3 from = place + normal * 0.02;
    float nearer = 0.0;
    float farther = -1.0;
    for (int taken = 1; taken <= kSteps; ++taken)
    {
        const float along = step * float(taken);
        const float3 on = seenAt(reflections.viewProjection, from + toward * along);
        if (on.z <= near || outside(on.xy))
        {
            break;
        }
        const float there = depth.Load(int3(int2(on.xy * size), 0));
        const float behind = there > 0.0 ? on.z - near / there : -1.0;
        if (behind > 0.0 && behind < thickness)
        {
            farther = along;
            break;
        }
        nearer = along;
    }
    if (farther < 0.0)
    {
        return float4(0.0);
    }
    for (int halving = 0; halving < 4; ++halving)
    {
        const float between = (nearer + farther) * 0.5;
        const float3 on = seenAt(reflections.viewProjection, from + toward * between);
        const float there = depth.Load(int3(int2(clamp(on.xy, float2(0.0), float2(0.999)) * size), 0));
        if (there > 0.0 && on.z > near / there)
        {
            farther = between;
        }
        else
        {
            nearer = between;
        }
    }
    const float3 met = from + toward * farther;
    const float3 then = seenAt(reflections.previous, met);
    if (then.z <= 0.0 || outside(then.xy))
    {
        return float4(0.0);
    }
    const float3 onNow = seenAt(reflections.viewProjection, met);
    const float2 fromEdge = min(onNow.xy, 1.0 - onNow.xy);
    const float edge = smoothstep(0.0, 0.1, min(fromEdge.x, fromEdge.y));
    const float fade = 1.0 - smoothstep(0.7, 1.0, farther / distance);
    const float3 light = before.SampleLevel(blended, then.xy, 0.0).rgb;
    return float4(light, smooth * away * edge * fade);
}
