// The 3D scene's models in the depth prepass when a screen-space effect
// asks for their surfaces (ADR-0051, D327), fragment entry "normal": each
// point's normal in the World's axes, turned toward the eye, and its
// material's roughness, into the normal-roughness target.

#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 2) in vec3 inPlaced;
layout(location = 5) flat in uint inMaterial;

// Every material's blob (D303), nine vectors each; the second holds the
// roughness.
layout(set = 0, binding = 9, std430) readonly buffer Materials
{
    vec4 materials[];
};

layout(location = 0) out vec4 outSurface;

void main()
{
    const uint kAt = min(inMaterial, uint(materials.length()) / 9u - 1u) * 9u;
    const vec3 kNormal = normalize(inNormal);
    outSurface = vec4(dot(kNormal, inPlaced) > 0.0 ? -kNormal : kNormal, materials[kAt + 1u].w);
}
