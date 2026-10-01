// A post process (D348, D350), fragment entry "fs": the picture coming in
// at the texel under it, and the post process's texture where its map puts
// the texel, folded as its material's form, per channel the constant, the
// picture times its factor, the texture times its, and the two multiplied
// times theirs; its alpha the constant's and the texture's. The color,
// never below nought, is laid over the picture by that alpha, and the
// result over the picture again by the camera's weight. A post process
// sampling no texture is given white, which its form multiplies by
// nought.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D picture;

// The form's constant, picture, texture, and both; the texture's scale and
// offset; and the weight.
layout(set = 0, binding = 1, std140) uniform Process
{
    vec4 fixedPart;
    vec4 scenePart;
    vec4 texturePart;
    vec4 bothPart;
    vec4 map;
    vec4 weight;
}
process;

layout(set = 0, binding = 2) uniform texture2D sampled;
layout(set = 0, binding = 3) uniform sampler sampling;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    const ivec2 kSize = textureSize(picture, 0);
    const ivec2 kTexel = min(ivec2(inUv * vec2(kSize)), kSize - 1);
    const vec3 kIn = texelFetch(picture, kTexel, 0).rgb;
    const vec4 kTexture = textureLod(sampler2D(sampled, sampling), inUv * process.map.xy + process.map.zw, 0.0);
    const vec3 kColor = process.fixedPart.rgb + process.scenePart.rgb * kIn + process.texturePart.rgb * kTexture.rgb +
                        process.bothPart.rgb * kIn * kTexture.rgb;
    const float kAlpha = clamp(process.fixedPart.a + process.texturePart.a * kTexture.a, 0.0, 1.0);
    outColor = vec4(mix(kIn, max(kColor, vec3(0.0)), kAlpha * process.weight.x), 1.0);
}
