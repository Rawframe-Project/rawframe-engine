// The sun's shadow map (D289), vertex entry "vs": a caster's vertex placed
// by its instance's model rows relative to the eye, then seen through one
// cascade's square, reversed-Z (ADR-0051): depth one toward the sun.

#version 450

layout(set = 0, binding = 0, std140) uniform Cascade
{
    mat4 viewProjection;
}
cascade;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec4 inModel0;
layout(location = 3) in vec4 inModel1;
layout(location = 4) in vec4 inModel2;

void main()
{
    const vec4 kVertex = vec4(inPosition, 1.0);
    const vec3 kPlaced = vec3(dot(inModel0, kVertex), dot(inModel1, kVertex), dot(inModel2, kVertex));
    gl_Position = cascade.viewProjection * vec4(kPlaced, 1.0);
}
