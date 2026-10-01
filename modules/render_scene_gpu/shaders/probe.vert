// The reflection probes' atlas (D340), vertex entry "vs": one triangle
// covering a face's mip, with its coordinates, rows top first; and which
// face and mip, from the draw's first instance.

#version 450

layout(location = 0) out vec2 outUv;
layout(location = 1) flat out uint outAt;

void main()
{
    const vec2 kCorner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    outUv = kCorner;
    outAt = uint(gl_InstanceIndex);
    gl_Position = vec4(kCorner.x * 2.0 - 1.0, 1.0 - kCorner.y * 2.0, 0.0, 1.0);
}
