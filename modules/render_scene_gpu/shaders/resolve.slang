// The multisampled depth's resolve (ADR-0051, D343), in Slang (ADR-0026,
// D475): each texel's first sample's depth, into the one-sample depth the
// screen-space effects and the post chain read.

import screen;

[[vk::binding(0, 0)]] DepthTexture2DMS depthSamples;

// Slang 2026.19 writes a depth returned bare as a color in WGSL; one in a
// record is written as the fragment's depth.
struct Written
{
    float depth : SV_Depth;
}

[shader("vertex")]
Bare vs(uint vertex : SV_VulkanVertexID)
{
    return bareOf(vertex);
}

[shader("fragment")]
Written depth(Bare bare)
{
    Written written;
    written.depth = depthSamples.Load(int2(bare.position.xy), 0);
    return written;
}
