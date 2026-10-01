// The particles (ADR-0053, D352, D353), fragment entry "fs": unlit, a
// particle's color times its emitter's material's base color and base
// texture standing as it is, whatever the exposure, as an unlit model's
// does; a soft disc where the texture's alpha does not shape it; its
// material's emission added in physical units times the exposure the
// device holds (D293). Hidden behind what the prepass left, and faded
// where it meets it across half its size, so it never cuts a hard line
// through the surface behind; written premultiplied, so its emission adds
// even where it covers nothing.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 viewProjection;
    vec4 toSun;
    vec4 sun;
    vec4 sky;
    vec4 exposure;
    // The eye's forward; each cascade's far end and texel; the cascades,
    // the shadows' distance, a cascade's side in texels (D289), and one
    // where the shadows are filtered soft (D330).
    vec4 forward;
    vec4 cascadeFar;
    vec4 cascadeTexel;
    vec4 shadow;
    mat4 cascades[4];
    // The clusters' tiles across and down, their slices, and the lights;
    // their near end, and the slices over the log of their far over near
    // (D290).
    vec4 clusterGrid;
    vec4 clusterDepth;
    // The view and projection without the jitter, and the frame before's
    // taking this frame's places (D291).
    mat4 unjittered;
    mat4 previous;
    // The ground's luminance below the horizon (D304).
    vec4 ground;
    // The sky's picture (D322): its levels, nought for none; and the
    // irradiance over π it gives, per unit of the sky's light, as nine
    // spherical harmonics' coefficients.
    vec4 environment;
    vec4 irradiance[9];
    // Whether the view's ambient occlusion is on (D327), its screen-space
    // reflections (D331), its contact shadows (D338), and whether it has
    // decals (D339).
    vec4 occlusion;
    vec4 reflections;
    vec4 contact;
    vec4 decals;
}
frame;

// The eye's right and up in the World's axes, and the particle clock now;
// and the near plane (D353).
layout(set = 0, binding = 1, std140) uniform View
{
    vec4 right;
    vec4 up;
    vec4 lens;
}
view;

// The emitter, as the spawn reads it (D352, D353).
layout(set = 0, binding = 2, std140) uniform Emitter
{
    vec4 anchor;
    vec4 origin;
    vec4 direction;
    vec4 motion;
    vec4 acceleration;
    vec4 sizes;
    vec4 colorStart;
    vec4 colorEnd;
    uvec4 ring;
    uvec4 more;
}
emitter;

layout(set = 0, binding = 4, std430) readonly buffer Exposure
{
    vec4 value;
}
exposure;

// Every material's blob (D303), nine vectors each, as the models read it.
layout(set = 0, binding = 5, std430) readonly buffer Materials
{
    vec4 materials[];
};

layout(set = 0, binding = 6) uniform texture2D depth;
layout(set = 0, binding = 7) uniform texture2D baseTexture;
layout(set = 0, binding = 8) uniform sampler baseSampler;
layout(set = 0, binding = 9) uniform texture2D emissionTexture;
layout(set = 0, binding = 10) uniform sampler emissionSampler;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inSoft;

layout(location = 0) out vec4 outColor;

void main()
{
    const uint kAt = min(emitter.more.z, uint(materials.length()) / 9u - 1u) * 9u;
    const vec4 kBase = materials[kAt];
    const vec4 kEmission = materials[kAt + 2u];
    const vec4 kRest = materials[kAt + 3u];
    const vec4 kBaseMap = materials[kAt + 4u];
    const vec4 kEmissionMap = materials[kAt + 6u];
    const uint kFlags = uint(kRest.w);
    vec4 sampled = vec4(1.0);
    if ((kFlags & 6u) != 0u) {
        sampled = texture(sampler2D(baseTexture, baseSampler), inUv * kBaseMap.xy + kBaseMap.zw);
    }
    vec3 glow = vec3(1.0);
    if ((kFlags & 8u) != 0u) {
        glow = texture(sampler2D(emissionTexture, emissionSampler), inUv * kEmissionMap.xy + kEmissionMap.zw).rgb;
    }
    // Behind what the prepass left (reversed-Z: nearer is greater), hidden,
    // once its textures are sampled; nearer, faded in over half its size.
    // The sky is never in front.
    const float kBehind = texelFetch(depth, ivec2(gl_FragCoord.xy), 0).r;
    if (gl_FragCoord.z < kBehind) {
        discard;
    }
    const float kFade = kBehind > 0.0
                            ? clamp((view.lens.x / kBehind - view.lens.x / gl_FragCoord.z) / inSoft, 0.0, 1.0)
                            : 1.0;
    const vec3 kColor = inColor.rgb * kBase.rgb * ((kFlags & 2u) != 0u ? sampled.rgb : vec3(1.0));
    const float kDisc = (kFlags & 4u) != 0u ? sampled.a : 1.0 - smoothstep(0.5, 1.0, length(inUv * 2.0 - 1.0));
    const float kOpacity = clamp(kRest.x * inColor.a * kDisc * kFade, 0.0, 1.0);
    // Its emission in physical units, over the coverage alone: a material
    // of no opacity only adds its light.
    const vec3 kGlow = kEmission.rgb * glow * inColor.a * kDisc * kFade * exposure.value.y;
    outColor = vec4(kColor * kOpacity + kGlow, kOpacity);
}
