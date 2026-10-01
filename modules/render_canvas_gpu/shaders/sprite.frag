// The canvas's sprites (D278, D356), fragment entry "fs": the sprite's
// texel, tinted, times its material's color, the material's texture sampled
// across the sprite's region where it has one; and its material's emission
// added within the sprite's shape. Written premultiplied, as its
// material's blend asks of its pipeline: over what is behind, or added to
// it; or, multiplying, what is behind is scaled by its color as far as it
// covers, the emission left out.

#version 450

layout(set = 0, binding = 0) uniform texture2D spriteTexture;
layout(set = 0, binding = 1) uniform sampler spriteSampler;

// The material (D355): its color, and what multiplies its texture's; its
// emission, and whether it multiplies (its fourth); what multiplies its
// texture's color in its emission; and its texture's scale and offset.
layout(set = 0, binding = 2, std140) uniform Material
{
    vec4 color;
    vec4 colorTexture;
    vec4 emission;
    vec4 emissionTexture;
    vec4 map;
}
material;

layout(set = 0, binding = 3) uniform texture2D materialTexture;
layout(set = 0, binding = 4) uniform sampler materialSampler;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec4 kSprite = texture(sampler2D(spriteTexture, spriteSampler), inUv) * inColor;
    const vec4 kSampled = texture(sampler2D(materialTexture, materialSampler), inUv * material.map.xy + material.map.zw);
    const vec4 kColor = kSprite * (material.color + material.colorTexture * kSampled);
    if (material.emission.w > 0.5) {
        outColor = vec4(mix(vec3(1.0), kColor.rgb, kColor.a), 0.0);
        return;
    }
    const vec3 kGlow = (material.emission.rgb + material.emissionTexture.rgb * kSampled.rgb) * kSprite.a;
    outColor = vec4(kColor.rgb * kColor.a + kGlow, kColor.a);
}
