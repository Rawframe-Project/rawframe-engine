// The sky behind the scene's models (D293), in Slang (ADR-0026, D475): one
// triangle covering the target at the far end of reversed-Z depth, so it
// lands only where no model's depth lies. Its light in candela per square
// meter times the exposure the device holds, which a metered camera moves
// from frame to frame; and no motion. With the sky's picture (D322), its
// light the way each point looks, and how far that way moved on the target
// since the frame before, for the temporal pass.

struct Sky
{
    float4 light;
    // The picture's levels, nought for none; where a point of the target
    // looks, the jittered view's inverse; and the view unjittered and the
    // frame before's.
    float4 environment;
    float4x4 toDirection;
    float4x4 unjittered;
    float4x4 previous;
}

// The exposure's EV100 and the factor it scales light by.
struct Exposure
{
    float4 value;
}

[[vk::binding(0, 0)]] ConstantBuffer<Sky> sky;
[[vk::binding(1, 0)]] StructuredBuffer<Exposure> exposure;
[[vk::binding(2, 0)]] TextureCube environmentTexture;
[[vk::binding(3, 0)]] SamplerState environmentSampler;

// Where on the target each point is, in clip space, for the sky's picture
// (D322).
struct Corner
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 seen;
}

struct Written
{
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
}

[shader("vertex")]
Corner vs(uint vertex : SV_VulkanVertexID)
{
    const float2 at = float2(float((vertex << 1) & 2), float(vertex & 2));
    Corner corner;
    corner.seen = float2(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0);
    corner.position = float4(corner.seen, 0.0, 1.0);
    return corner;
}

[shader("fragment")]
Written fs(Corner corner)
{
    Written written;
    if (sky.environment.w < 0.5)
    {
        written.color = float4(sky.light.rgb * exposure[0].value.y, 1.0);
        written.motion = float2(0.0);
        return written;
    }
    // A point at the far end of depth is a direction: the view's inverse
    // takes it back to the way it looks.
    const float3 toward = normalize(mul(sky.toDirection, float4(corner.seen, 0.0, 1.0)).xyz);
    const float3 picture = environmentTexture.SampleLevel(environmentSampler, toward, 0.0).rgb;
    written.color = float4(picture * sky.light.rgb * exposure[0].value.y, 1.0);
    const float4 now = mul(sky.unjittered, float4(toward, 0.0));
    const float4 before = mul(sky.previous, float4(toward, 0.0));
    written.motion =
        before.w > 0.0 && now.w > 0.0 ? (now.xy / now.w - before.xy / before.w) * float2(0.5, -0.5) : float2(0.0);
    return written;
}
