// The sky behind the scene's models (D293), vertex entry "vs": one triangle
// covering the target at the far end of reversed-Z depth, so it lands only
// where no model's depth lies; and where on the target each point is, in
// clip space, for the sky's picture (D322).

#version 450

layout(location = 0) out vec2 outSeen;

void main()
{
    const vec2 kCorner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    outSeen = vec2(kCorner.x * 2.0 - 1.0, 1.0 - kCorner.y * 2.0);
    gl_Position = vec4(outSeen, 0.0, 1.0);
}
