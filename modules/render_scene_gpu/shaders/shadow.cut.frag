// The shadows' masked casters (D310), fragment entry "cut": a caster casts
// no shadow where its material's opacity falls below its alpha cutoff, as
// scene.cut.frag cuts it from the view.

#version 450

layout(location = 1) in vec4 inColor;
layout(location = 5) flat in uint inMaterial;
layout(location = 6) in vec2 inUv;

// Every material's blob (D303), four vectors each; the fourth holds the
// opacity, the occlusion, the alpha cutoff, and the flags.
layout(set = 0, binding = 1, std430) readonly buffer Materials
{
    vec4 materials[];
};

layout(set = 0, binding = 2) uniform texture2D baseTexture;
layout(set = 0, binding = 3) uniform sampler baseSampler;

void main()
{
    const uint kAt = min(inMaterial, uint(materials.length()) / 4u - 1u) * 4u;
    const vec4 kRest = materials[kAt + 3u];
    const vec4 kSampled = texture(sampler2D(baseTexture, baseSampler), inUv);
    const float kOpacity = kRest.x * inColor.a * ((uint(kRest.w) & 4u) != 0u ? kSampled.a : 1.0);
    if (kOpacity < kRest.z) {
        discard;
    }
}
