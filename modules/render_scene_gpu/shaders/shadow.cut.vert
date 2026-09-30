// The shadows' masked casters (D310), vertex entry "vsCut": placed as "vs"
// places a caster, with what the cut needs of it: its color, its
// material's place, and its texture coordinates.

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
layout(location = 8) in vec4 inColor;
layout(location = 12) in float inMaterial;
layout(location = 13) in vec2 inUv;

layout(location = 1) out vec4 outColor;
layout(location = 5) flat out uint outMaterial;
layout(location = 6) out vec2 outUv;

void main()
{
    const vec4 kVertex = vec4(inPosition, 1.0);
    const vec3 kPlaced = vec3(dot(inModel0, kVertex), dot(inModel1, kVertex), dot(inModel2, kVertex));
    gl_Position = cascade.viewProjection * vec4(kPlaced, 1.0);
    outColor = inColor;
    outMaterial = uint(inMaterial);
    outUv = inUv;
}
