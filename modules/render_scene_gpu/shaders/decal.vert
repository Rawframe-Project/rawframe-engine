// The decals' atlas (D339), vertex entry "vs": one triangle covering a
// layer's mip, with its coordinates, rows top first.

#version 450

layout(location = 0) out vec2 outUv;

void main()
{
    const vec2 kCorner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    outUv = kCorner;
    gl_Position = vec4(kCorner.x * 2.0 - 1.0, 1.0 - kCorner.y * 2.0, 0.0, 1.0);
}
