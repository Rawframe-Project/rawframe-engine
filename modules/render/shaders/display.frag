// A frame's picture shown on a window's surface (D280), fragment entry
// "fs": the picture's bytes as they are, read through its linear view, so
// its sRGB encoding reaches the surface unchanged.

#version 450

layout(set = 0, binding = 0) uniform texture2D picture;
layout(set = 0, binding = 1) uniform sampler pictureSampler;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(sampler2D(picture, pictureSampler), inUv);
}
