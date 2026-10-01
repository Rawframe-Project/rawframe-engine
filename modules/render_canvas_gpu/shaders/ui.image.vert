// The UI's images (SPEC-0032, D378), vertex entry "imageVs": one quad for
// each image drawn, its instance, over the rectangle it fills, in the
// picture's pixels from its top left, y down; the fragment entry reads
// the image by its index.

#version 450

layout(set = 0, binding = 0, std140) uniform View
{
    // The picture's width and height in pixels.
    vec4 size;
}
view;

struct Image
{
    vec4 rect;
    vec4 uv;
    vec4 slice;
    vec4 tint;
    // Its clip's index, its texture's width and height in its pixels, and
    // the picture's pixels a logical one.
    vec4 clip;
};

layout(set = 0, binding = 3, std430) readonly buffer Images
{
    Image images[];
}
shown;

layout(location = 0) out vec2 outPixel;
layout(location = 1) flat out uint outImage;

void main()
{
    const vec2 kCorners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    vec4 rect = shown.images[gl_InstanceIndex].rect;
    vec2 pixel = rect.xy + kCorners[gl_VertexIndex] * rect.zw;
    // Maul RHI's clip space runs up on every driver, as WebGPU's does.
    gl_Position = vec4(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    outPixel = pixel;
    outImage = gl_InstanceIndex;
}
