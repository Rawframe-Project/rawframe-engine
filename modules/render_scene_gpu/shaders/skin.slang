// Skinned models' vertices (D508), in Slang (ADR-0026, D475): each vertex
// of a posed model's mesh, as the scene's pipelines read it, moved by the
// joints it follows into a buffer of the same layout, which the frame's
// draws of that model read in its mesh's place.

// What one model's skinning reads and writes: its mesh's vertex count, its
// first joint matrix among the frame's, and its first vertex among the
// frame's skinned ones.
struct Skinned
{
    uint4 counts;
}

// A vertex as the scene's pipelines read it: position, normal, texture
// coordinates, then the tangent and its sign; twelve floats.
[[vk::binding(0, 0)]] StructuredBuffer<float> rest;
// Each vertex's four joints, two to a word, low half first, then its four
// weights' bits; six words.
[[vk::binding(1, 0)]] StructuredBuffer<uint> influences;
// The frame's joint matrices, each four columns.
[[vk::binding(2, 0)]] StructuredBuffer<float4> palette;
[[vk::binding(3, 0)]] RWStructuredBuffer<float> posed;
[[vk::binding(4, 0)]] ConstantBuffer<Skinned> skinned;

// A joint's columns, weighed, added to `columns`.
void weigh(uint joint, float weight, inout float4 columns[4])
{
    const uint first = (skinned.counts.y + joint) * 4u;
    for (uint column = 0u; column < 4u; ++column)
    {
        columns[column] += palette[first + column] * weight;
    }
}

// One vertex a thread: its joints' matrices weighed and summed, the
// position moved by it, the normal and the tangent turned and made a unit
// long again, the texture coordinates and the tangent's sign kept.
[shader("compute")]
[numthreads(64, 1, 1)]
void skin(uint3 invocation : SV_DispatchThreadID)
{
    const uint which = invocation.x;
    if (which >= skinned.counts.x)
    {
        return;
    }
    const uint words = influences[which * 6u];
    const uint more = influences[which * 6u + 1u];
    const uint4 joints = uint4(words & 0xFFFFu, words >> 16u, more & 0xFFFFu, more >> 16u);
    const float4 weights = float4(asfloat(influences[which * 6u + 2u]),
                                  asfloat(influences[which * 6u + 3u]),
                                  asfloat(influences[which * 6u + 4u]),
                                  asfloat(influences[which * 6u + 5u]));
    float4 columns[4] = { float4(0.0), float4(0.0), float4(0.0), float4(0.0) };
    weigh(joints.x, weights.x, columns);
    weigh(joints.y, weights.y, columns);
    weigh(joints.z, weights.z, columns);
    weigh(joints.w, weights.w, columns);
    // Columns given; Slang's constructor takes rows.
    const float4x4 moved = transpose(float4x4(columns[0], columns[1], columns[2], columns[3]));
    const uint from = which * 12u;
    const float3 place = mul(moved, float4(rest[from], rest[from + 1u], rest[from + 2u], 1.0)).xyz;
    const float3x3 turn = (float3x3)moved;
    const float3 normal = normalize(mul(turn, float3(rest[from + 3u], rest[from + 4u], rest[from + 5u])));
    const float3 along = mul(turn, float3(rest[from + 8u], rest[from + 9u], rest[from + 10u]));
    const float3 tangent = dot(along, along) > 0.0 ? normalize(along) : along;
    const uint to = (skinned.counts.z + which) * 12u;
    posed[to] = place.x;
    posed[to + 1u] = place.y;
    posed[to + 2u] = place.z;
    posed[to + 3u] = normal.x;
    posed[to + 4u] = normal.y;
    posed[to + 5u] = normal.z;
    posed[to + 6u] = rest[from + 6u];
    posed[to + 7u] = rest[from + 7u];
    posed[to + 8u] = tangent.x;
    posed[to + 9u] = tangent.y;
    posed[to + 10u] = tangent.z;
    posed[to + 11u] = rest[from + 11u];
}
