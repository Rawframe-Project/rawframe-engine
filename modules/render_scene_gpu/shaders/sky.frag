// The sky behind the scene's models (D293), fragment entry "fs": its light
// in candela per square meter times the exposure the device holds, which a
// metered camera moves from frame to frame; and no motion.

#version 450

layout(set = 0, binding = 0, std140) uniform Sky
{
    vec4 light;
}
sky;

// The exposure's EV100 and the factor it scales light by.
layout(set = 0, binding = 1, std430) readonly buffer Exposure
{
    vec4 value;
}
exposure;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outMotion;

void main()
{
    outColor = vec4(sky.light.rgb * exposure.value.y, 1.0);
    outMotion = vec2(0.0);
}
