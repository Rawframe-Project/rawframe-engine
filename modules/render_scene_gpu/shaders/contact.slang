// The scene's contact shadows (ADR-0051, D338), in Slang (ADR-0026, D475):
// from each point the prepass left, sixteen steps toward the sun over the
// camera's length, each seen from the eye. Where one passes behind what
// the prepass left there by less than the length (what stands there is
// taken to be at least as thick), something stands between the point and
// the sun, and the point is shaded from it, less so the farther along the
// meeting is. The first step is jittered per pixel, which the temporal
// pass averages away.

import screen;

// The jittered view's inverse, from clip space to the eye-relative World;
// the view; the direction toward the sun; the length and the near plane.
struct Contact
{
    float4x4 toPoint;
    float4x4 viewProjection;
    float4 toSun;
    float4 settings;
}

[[vk::binding(0, 0)]] DepthTexture2D depth;
[[vk::binding(1, 0)]] ConstantBuffer<Contact> contact;

static const int kSteps = 16;

// Where a place is on the target, rows top first, and how far ahead.
float3 seenAt(float3 place)
{
    const float4 clip = mul(contact.viewProjection, float4(place, 1.0));
    return float3(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5, clip.w);
}

// The pass reads only where its fragment is, so its vertex half passes
// nothing on (D418).
[shader("vertex")]
Bare vs(uint vertex : SV_VulkanVertexID)
{
    return bareOf(vertex);
}

[shader("fragment")]
float shade(Bare bare) : SV_Target0
{
    const int2 at = int2(bare.position.xy);
    const float here = depth.Load(int3(at, 0));
    if (here <= 0.0)
    {
        return 1.0;
    }
    uint width;
    uint height;
    depth.GetDimensions(width, height);
    const float2 size = float2(float(width), float(height));
    const float2 middle = (float2(at) + 0.5) / size;
    const float4 seen = mul(contact.toPoint, float4(middle.x * 2.0 - 1.0, 1.0 - middle.y * 2.0, here, 1.0));
    const float3 place = seen.xyz / seen.w;
    const float reach = contact.settings.x;
    const float near = contact.settings.y;
    const float step = reach / float(kSteps);
    const float thickness = reach;
    // Interleaved gradient noise (Jimenez 2014).
    const float jitter = frac(52.9829189 * frac(dot(bare.position.xy, float2(0.06711056, 0.00583715))));
    for (int taken = 0; taken < kSteps; ++taken)
    {
        const float along = step * (float(taken) + 0.5 + jitter);
        const float3 on = seenAt(place + contact.toSun.xyz * along);
        if (on.z <= near || any(on.xy < float2(0.0)) || any(on.xy >= float2(1.0)))
        {
            return 1.0;
        }
        const float there = depth.Load(int3(int2(on.xy * size), 0));
        if (there <= 0.0)
        {
            continue;
        }
        const float behind = on.z - near / there;
        if (behind > 0.002 * on.z && behind < thickness)
        {
            return smoothstep(0.5, 1.0, along / reach);
        }
    }
    return 1.0;
}
