// The UI's shadows (SPEC-0032, D381), vertex entry "shadowVs": one quad for
// each shadow drawn, its instance, over what it can darken: an outer
// shadow's box offset, grown by its spread and three deviations of its
// blur, an inset one's box; in the picture's pixels from its top left, y
// down.

#version 450

layout(set = 0, binding = 0, std140) uniform View
{
    // The picture's width and height in pixels.
    vec4 size;
}
view;

struct Shadow
{
    vec4 rect;
    vec4 radii;
    vec4 color;
    // Its offset, its blur, and its spread.
    vec4 shape;
    // Whether it is inset, and its clip's index.
    vec4 flags;
};

layout(set = 0, binding = 6, std430) readonly buffer Shadows
{
    Shadow shadows[];
}
thrown;

layout(location = 0) out vec2 outPixel;
layout(location = 1) flat out uint outShadow;

void main()
{
    const vec2 kCorners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    vec4 rect = thrown.shadows[gl_InstanceIndex].rect;
    vec4 shape = thrown.shadows[gl_InstanceIndex].shape;
    if (thrown.shadows[gl_InstanceIndex].flags.x < 0.5)
    {
        float reach = max(shape.w, 0.0) + 1.5 * shape.z + 1.0;
        rect = vec4(rect.xy + shape.xy - vec2(reach), rect.zw + vec2(2.0 * reach));
    }
    vec2 pixel = rect.xy + kCorners[gl_VertexIndex] * rect.zw;
    // Maul RHI's clip space runs up on every driver, as WebGPU's does.
    gl_Position = vec4(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    outPixel = pixel;
    outShadow = gl_InstanceIndex;
}
