// The sky behind the scene's models (D293), fragment entry "fs": its light
// in candela per square meter times the exposure the device holds, which a
// metered camera moves from frame to frame; and no motion. With the sky's
// picture (D322), its light the way each point looks, and how far that way
// moved on the target since the frame before, for the temporal pass.

#version 450

layout(set = 0, binding = 0, std140) uniform Sky
{
    vec4 light;
    // The picture's levels, nought for none; where a point of the target
    // looks, the jittered view's inverse; and the view unjittered and the
    // frame before's.
    vec4 environment;
    mat4 toDirection;
    mat4 unjittered;
    mat4 previous;
}
sky;

// The exposure's EV100 and the factor it scales light by.
layout(set = 0, binding = 1, std430) readonly buffer Exposure
{
    vec4 value;
}
exposure;

layout(set = 0, binding = 2) uniform textureCube environmentTexture;
layout(set = 0, binding = 3) uniform sampler environmentSampler;

layout(location = 0) in vec2 inSeen;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outMotion;

void main()
{
    if (sky.environment.w < 0.5) {
        outColor = vec4(sky.light.rgb * exposure.value.y, 1.0);
        outMotion = vec2(0.0);
        return;
    }
    // A point at the far end of depth is a direction: the view's inverse
    // takes it back to the way it looks.
    const vec3 kToward = normalize((sky.toDirection * vec4(inSeen, 0.0, 1.0)).xyz);
    const vec3 kPicture = textureLod(samplerCube(environmentTexture, environmentSampler), kToward, 0.0).rgb;
    outColor = vec4(kPicture * sky.light.rgb * exposure.value.y, 1.0);
    const vec4 kNow = sky.unjittered * vec4(kToward, 0.0);
    const vec4 kBefore = sky.previous * vec4(kToward, 0.0);
    outMotion = kBefore.w > 0.0 && kNow.w > 0.0 ? (kNow.xy / kNow.w - kBefore.xy / kBefore.w) * vec2(0.5, -0.5)
                                                : vec2(0.0);
}
