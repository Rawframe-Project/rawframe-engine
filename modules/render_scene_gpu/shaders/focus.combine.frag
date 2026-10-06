// The scene's depth of field (D336), fragment entry "combine": each point
// of the full-size light, blended toward the half-size bokeh as far as
// its own circle or a nearer one's over it reaches past a pixel. The
// bokeh is taken from the four half-size texels about the point, each
// weighed by its nearness and by how alike its circle is to the point's,
// so a blurred background does not take in a sharp subject's edge.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D scene;
layout(set = 0, binding = 1) uniform texture2D depth;
layout(set = 0, binding = 2) uniform texture2D halved;
layout(set = 0, binding = 3) uniform sampler blended;
// The halved light, for each half-size texel's circle.
layout(set = 0, binding = 5) uniform texture2D circles;

layout(set = 0, binding = 4, std140) uniform Lens
{
    vec4 settings;
}
lens;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    // Read for the interface alone (D418): Direct3D 12 links stages by place.
    const float kInterface = inUv.x;
    const ivec2 kAt = ivec2(gl_FragCoord.xy);
    const vec4 kSharp = texelFetch(scene, kAt, 0);
    const float kAhead = lens.settings.z / max(texelFetch(depth, kAt, 0).r, 1e-7);
    const float kCircle =
        clamp(lens.settings.x * (kAhead - lens.settings.y) / kAhead, -lens.settings.w, lens.settings.w) * 0.5;
    const ivec2 kSize = textureSize(halved, 0);
    const vec2 kAcross = gl_FragCoord.xy * 0.5 - 0.5;
    const ivec2 kFirst = ivec2(floor(kAcross));
    const vec2 kFraction = kAcross - vec2(kFirst);
    vec4 blurred = vec4(0.0);
    float weight = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const ivec2 kTexel = clamp(kFirst + ivec2(x, y), ivec2(0), kSize - 1);
            const vec2 kNear = mix(1.0 - kFraction, kFraction, vec2(x, y));
            const float kAlike = 1.0 / (0.05 + abs(texelFetch(circles, kTexel, 0).a - kCircle));
            const float kWeight = kNear.x * kNear.y * kAlike + 1e-6;
            blurred += texelFetch(halved, kTexel, 0) * kWeight;
            weight += kWeight;
        }
    }
    blurred /= weight;
    const float kBlend = smoothstep(0.5, 1.5, max(abs(kCircle) * 2.0, blurred.a * 2.0));
    outColor = vec4(mix(kSharp.rgb, blurred.rgb, kBlend), kSharp.a);
}
