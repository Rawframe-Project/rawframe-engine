// The UI's boxes (SPEC-0032, D375), vertex entry "vs": one quad for each
// box of the list, its instance, a pixel past its border box on every side
// so its edge is smoothed, in the picture's pixels from its top left, y
// down; the fragment entry reads the box by its index.

#version 450

layout(set = 0, binding = 0, std140) uniform View
{
    // The picture's width and height in pixels.
    vec4 size;
}
view;

struct Box
{
    vec4 rect;
    vec4 radii;
    vec4 fill;
    vec4 widths;
    vec4 borders[4];
    // Its clip's index in the clips, nought for none.
    vec4 clip;
};

layout(set = 0, binding = 1, std430) readonly buffer Boxes
{
    Box boxes[];
}
list;

layout(location = 0) out vec2 outPixel;
layout(location = 1) flat out uint outBox;

void main()
{
    const vec2 kCorners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    vec4 rect = list.boxes[gl_InstanceIndex].rect;
    vec2 pixel = rect.xy - vec2(1.0) + kCorners[gl_VertexIndex] * (rect.zw + vec2(2.0));
    // Maul RHI's clip space runs up on every driver, as WebGPU's does.
    gl_Position = vec4(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    outPixel = pixel;
    outBox = gl_InstanceIndex;
}
