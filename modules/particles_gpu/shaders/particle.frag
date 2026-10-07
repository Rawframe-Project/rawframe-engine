// The particles and ribbons (ADR-0053, D352 to D354, D357), fragment entry
// "fs": unlit, a particle's or a ribbon's color times its material's
// color, a constant plus a constant times its texture's, standing as it is
// whatever the exposure, as an unlit model's does; soft at its edges (a
// particle a disc, a ribbon along its sides) where the texture's alpha
// does not shape it; its material's emission added, a constant plus a
// constant times its emission texture's, in physical units times the
// exposure the device holds (D293), one on a canvas. Hidden behind the
// depth it is given, and faded where it meets it across half its size, so
// it never cuts a hard line through the surface behind; a canvas gives a
// depth of nought, the farthest, and nothing is hidden or faded. Written
// premultiplied, so its emission adds even where it covers nothing; or,
// for a material that multiplies, what multiplies what is behind.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0, std140) uniform View
{
    mat4 viewProjection;
    vec4 right;
    vec4 up;
    vec4 lens;
}
view;

layout(set = 0, binding = 3, std430) readonly buffer Exposure
{
    vec4 value;
}
exposure;

// The material (D357): its color, and what multiplies its texture's; its
// emission, and what multiplies its emission texture's color; each
// texture's scale and offset; and its flags: one where its texture's
// alpha shapes it, two where it multiplies what is behind, four where it
// is drawn over everything, nothing hiding it (D464).
layout(set = 0, binding = 4, std140) uniform Material
{
    vec4 color;
    vec4 colorTexture;
    vec4 emission;
    vec4 emissionTexture;
    vec4 baseMap;
    vec4 emissionMap;
    uvec4 flags;
}
material;

layout(set = 0, binding = 5) uniform texture2D depth;
layout(set = 0, binding = 6) uniform texture2D baseTexture;
layout(set = 0, binding = 7) uniform sampler baseSampler;
layout(set = 0, binding = 8) uniform texture2D emissionTexture;
layout(set = 0, binding = 9) uniform sampler emissionSampler;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inSoft;
// How far from its middle toward its edges, a particle's both ways, a
// ribbon's across it.
layout(location = 3) in vec2 inShape;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec4 kSampled = texture(sampler2D(baseTexture, baseSampler), inUv * material.baseMap.xy + material.baseMap.zw);
    const vec3 kGlowing =
        texture(sampler2D(emissionTexture, emissionSampler), inUv * material.emissionMap.xy + material.emissionMap.zw)
            .rgb;
    // Behind the depth it is given (reversed-Z: nearer is greater), hidden,
    // once its textures are sampled; nearer, faded in over half its size.
    // The sky is never in front.
    // Its texel, held to the depth's sides: a canvas's is one texel.
    // A material drawn over everything (flag four, a line's, D464) is
    // neither hidden nor faded: the depth is taken as the farthest.
    const bool kOver = (material.flags.x & 4u) != 0u;
    const float kBehind =
        kOver ? 0.0 : texelFetch(depth, min(ivec2(gl_FragCoord.xy), textureSize(depth, 0) - 1), 0).r;
    if (gl_FragCoord.z < kBehind) {
        discard;
    }
    const float kFade = kBehind > 0.0
                            ? clamp((view.lens.x / kBehind - view.lens.x / gl_FragCoord.z) / inSoft, 0.0, 1.0)
                            : 1.0;
    const bool kShaped = (material.flags.x & 1u) != 0u;
    const vec4 kTinted = material.color + material.colorTexture * kSampled;
    const float kDisc = kShaped ? kSampled.a : 1.0 - smoothstep(0.5, 1.0, length(inShape));
    const vec3 kColor = inColor.rgb * kTinted.rgb;
    const float kOpacity = clamp(kTinted.a * inColor.a * (kShaped ? 1.0 : kDisc) * kFade, 0.0, 1.0);
    if ((material.flags.x & 2u) != 0u) {
        outColor = vec4(kColor * kOpacity + vec3(1.0 - kOpacity), kOpacity);
        return;
    }
    // Its emission in physical units, over the coverage alone: a material
    // of no opacity only adds its light.
    const vec3 kGlow = (material.emission.rgb + material.emissionTexture.rgb * kGlowing) * inColor.a * kDisc * kFade *
                       exposure.value.y;
    outColor = vec4(kColor * kOpacity + kGlow, kOpacity);
}
