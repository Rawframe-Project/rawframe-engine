// The 3D scene's models (D284), fragment entry "fs": the base color lit by
// the sun (Lambert) and the sky (brighter facing up), in physical units,
// times the camera's exposure, so the scene target holds pre-exposed
// scene-linear light (ADR-0047).

#version 450

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 viewProjection;
    vec4 toSun;
    vec4 sun;
    vec4 sky;
    vec4 exposure;
}
frame;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265;

void main()
{
    const vec3 kNormal = normalize(inNormal);
    const float kFacing = max(dot(kNormal, frame.toSun.xyz), 0.0);
    const vec3 kLight = frame.sun.rgb * (kFacing / kPi) + frame.sky.rgb * (0.5 + 0.5 * kNormal.y);
    outColor = vec4(inColor.rgb * kLight * frame.exposure.x, 1.0);
}
