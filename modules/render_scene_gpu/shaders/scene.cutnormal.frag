// The 3D scene's masked models in the depth prepass when a screen-space
// effect asks for their surfaces (D310, D327), fragment entry "cutNormal":
// cut as "cut" cuts them, and where they stand, their normal and roughness
// as "normal" gives them.

#version 450

// Every output of scene.vert, in its order, each read in main though this
// entry needs only some: Direct3D 12 links stages by place, and an input
// never read is left out of the entry when it is crossed to HLSL, so the
// rest would not line up with the vertex entry's (D418).
layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inPlaced;
layout(location = 3) in vec3 inNow;
layout(location = 4) in vec3 inBefore;
layout(location = 5) flat in uint inMaterial;
layout(location = 6) in vec2 inUv;
layout(location = 7) in vec4 inTangent;

// Every material's blob (D303), nine vectors each: the second holds the
// roughness, the fourth the opacity and the alpha cutoff, the fifth the
// base texture's scale and offset (D311).
layout(set = 0, binding = 9, std430) readonly buffer Materials
{
    vec4 materials[];
};

layout(set = 0, binding = 10) uniform texture2D baseTexture;
layout(set = 0, binding = 11) uniform sampler baseSampler;

layout(location = 0) out vec4 outSurface;

void main()
{
    // Read for the interface alone (D418); nothing uses it.
    const float kInterface = inNow.x + inBefore.x + inTangent.x;
    const uint kAt = min(inMaterial, uint(materials.length()) / 9u - 1u) * 9u;
    const vec4 kRest = materials[kAt + 3u];
    const vec4 kMapped = materials[kAt + 4u];
    const vec4 kSampled = texture(sampler2D(baseTexture, baseSampler), inUv * kMapped.xy + kMapped.zw);
    const float kOpacity = kRest.x * inColor.a * ((uint(kRest.w) & 4u) != 0u ? kSampled.a : 1.0);
    if (kOpacity < kRest.z) {
        discard;
    }
    const vec3 kNormal = normalize(inNormal);
    outSurface = vec4(dot(kNormal, inPlaced) > 0.0 ? -kNormal : kNormal, materials[kAt + 1u].w);
}
