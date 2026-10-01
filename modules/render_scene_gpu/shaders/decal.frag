// The decals' atlas (ADR-0051, D339), fragment entry "fill": a decal's
// texture drawn into one mip of its layer, sampled at the texture's own
// mip nearest that size (the triangle's coordinates step one target texel
// a pixel), filtered between its mips.

#version 450

layout(set = 0, binding = 0) uniform texture2D source;
layout(set = 0, binding = 1) uniform sampler blended;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(sampler2D(source, blended), inUv);
}
