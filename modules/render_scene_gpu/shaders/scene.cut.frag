// The 3D scene's masked models in the depth prepass (D310), fragment entry
// "cut": where the material's opacity falls below its alpha cutoff the
// model is cut away, so the depth the prepass leaves is where the model
// stands. The lit pass draws a masked model only where its depth equals
// what the prepass left, so it needs no cut of its own, and the lit
// shader never discards, which would cost every model its early depth
// test.

#version 450

layout(location = 1) in vec4 inColor;
layout(location = 5) flat in uint inMaterial;
layout(location = 6) in vec2 inUv;

// Every material's blob (D303), four vectors each; the fourth holds the
// opacity, the occlusion, the alpha cutoff, and the flags.
layout(set = 0, binding = 9, std430) readonly buffer Materials
{
    vec4 materials[];
};

layout(set = 0, binding = 10) uniform texture2D baseTexture;
layout(set = 0, binding = 11) uniform sampler baseSampler;

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
