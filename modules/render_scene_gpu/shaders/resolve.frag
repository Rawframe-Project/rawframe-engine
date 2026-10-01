// The multisampled depth's resolve (ADR-0051, D343), fragment entry
// "depth": each texel's first sample's depth, into the one-sample depth the
// screen-space effects and the post chain read.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2DMS depthSamples;

void main()
{
    gl_FragDepth = texelFetch(depthSamples, ivec2(gl_FragCoord.xy), 0).r;
}
