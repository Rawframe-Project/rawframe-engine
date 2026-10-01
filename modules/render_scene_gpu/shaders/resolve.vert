// The multisampled depth's resolve (D343), vertex entry "vs": one triangle
// covering the target.

#version 450

void main()
{
    const vec2 kCorner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(kCorner.x * 2.0 - 1.0, 1.0 - kCorner.y * 2.0, 0.0, 1.0);
}
