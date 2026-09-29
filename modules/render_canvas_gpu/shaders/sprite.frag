// The canvas's sprites (D278), fragment entry "fs": the texture's texel,
// tinted.

#version 450

layout(set = 0, binding = 0) uniform texture2D spriteTexture;
layout(set = 0, binding = 1) uniform sampler spriteSampler;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(sampler2D(spriteTexture, spriteSampler), inUv) * inColor;
}
