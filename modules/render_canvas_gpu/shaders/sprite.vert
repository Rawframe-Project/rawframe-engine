// The canvas's sprites (D278), vertex entry "vs": a quad's corner as the
// queue stage made it, in clip space, where y runs up; its
// place in the texture; and its color, 0xRRGGBBAA, whose bytes arrive in
// memory order, alpha first.

#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

// The color's channels are written in sRGB (ADR-0047): blending happens in
// linear light, so they are decoded here.
vec3 linearOf(vec3 encoded)
{
    return mix(encoded / 12.92, pow((encoded + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), encoded));
}

void main()
{
    // Maul RHI's clip space runs up on every driver, as WebGPU's does.
    gl_Position = vec4(inPosition, 0.0, 1.0);
    outUv = inUv;
    vec4 color = inColor.wzyx;
    outColor = vec4(linearOf(color.rgb), color.a);
}
