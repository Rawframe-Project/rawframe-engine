// The reflection probes' atlas (ADR-0051, D340), fragment entry "fill": one
// face of a probe's picture drawn into one mip of its cube in the atlas,
// the face and the mip `inAt` names (the face times eight, plus the mip).
// Each mip is the light as an ever rougher surface reflects it, as the
// picture's levels are (D321): mip m of the atlas's eight from the
// picture's level m sevenths of the way to its last.

#version 450

layout(set = 0, binding = 0) uniform textureCube source;
layout(set = 0, binding = 1) uniform sampler blended;

layout(location = 0) in vec2 inUv;
layout(location = 1) flat in uint inAt;

layout(location = 0) out vec4 outColor;

void main()
{
    const uint kFace = inAt / 8u;
    // The direction through the texel, s rightward and t downward, as
    // Vulkan addresses a cube.
    const float kS = inUv.x * 2.0 - 1.0;
    const float kT = inUv.y * 2.0 - 1.0;
    vec3 toward = vec3(-kS, -kT, -1.0);
    if (kFace == 0u) {
        toward = vec3(1.0, -kT, -kS);
    } else if (kFace == 1u) {
        toward = vec3(-1.0, -kT, kS);
    } else if (kFace == 2u) {
        toward = vec3(kS, 1.0, kT);
    } else if (kFace == 3u) {
        toward = vec3(kS, -1.0, -kT);
    } else if (kFace == 4u) {
        toward = vec3(kS, -kT, 1.0);
    }
    const float kLast = float(textureQueryLevels(samplerCube(source, blended)) - 1);
    outColor = textureLod(samplerCube(source, blended), toward, float(inAt % 8u) / 7.0 * kLast);
}
