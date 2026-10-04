// The UI's glyphs (SPEC-0032, D398), vertex entry "glyphVs": one quad for
// each glyph of a run, its instance, over the rectangle its image fills, in
// the picture's pixels from its top left, y down, its edges on pixels; the
// fragment entry reads its coverage by its index.

#version 450

layout(set = 0, binding = 0, std140) uniform View
{
    // The picture's width and height in pixels.
    vec4 size;
}
view;

struct Glyph
{
    vec4 rect;
    vec4 atlas;
    vec4 color;
    vec4 clip;
};

layout(set = 0, binding = 8, std430) readonly buffer Glyphs
{
    Glyph glyphs[];
}
shown;

layout(location = 0) out vec2 outPixel;
layout(location = 1) flat out uint outGlyph;

void main()
{
    const vec2 kCorners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    vec4 rect = shown.glyphs[gl_InstanceIndex].rect;
    vec2 pixel = rect.xy + kCorners[gl_VertexIndex] * rect.zw;
    // Maul RHI's clip space runs up on every driver, as WebGPU's does.
    gl_Position = vec4(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    outPixel = pixel;
    outGlyph = gl_InstanceIndex;
}
