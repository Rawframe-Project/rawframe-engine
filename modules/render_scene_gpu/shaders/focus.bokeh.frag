// The scene's depth of field (D336), fragment entry "bokeh": Gustafsson's
// single-pass bokeh at half the target's size. Taps on a golden-angle
// spiral out to the longest radius, each taken where its own circle
// reaches the point; one behind the point spreads no more than twice the
// point's circle, so a sharp subject does not take in the blurred
// background. Beside the light, how far the blur that reaches the point
// spreads: its own circle, or a nearer one's over it.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 2) uniform texture2D halved;
layout(set = 0, binding = 3) uniform sampler blended;

layout(set = 0, binding = 4, std140) uniform Lens
{
    vec4 settings;
}
lens;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outBlurred;

const float kGoldenAngle = 2.39996323;

void main()
{
    const vec2 kSize = vec2(textureSize(halved, 0));
    const vec4 kHere = texelFetch(halved, ivec2(gl_FragCoord.xy), 0);
    const float kOwn = abs(kHere.a);
    const float kLongest = lens.settings.w * 0.5;
    vec3 light = kHere.rgb;
    float taps = 1.0;
    float reach = kOwn;
    float radius = 1.0;
    float angle = 0.0;
    for (int tap = 0; tap < 512 && radius < kLongest; ++tap) {
        const vec2 kAt = (gl_FragCoord.xy + vec2(cos(angle), sin(angle)) * radius) / kSize;
        const vec4 kThere = textureLod(sampler2D(halved, blended), kAt, 0.0);
        const bool kBehind = kThere.a > kHere.a;
        const float kCircle = kBehind ? min(abs(kThere.a), kOwn * 2.0) : abs(kThere.a);
        const float kTaken = smoothstep(radius - 0.5, radius + 0.5, kCircle);
        light += mix(light / taps, kThere.rgb, kTaken);
        taps += 1.0;
        if (!kBehind) {
            reach = max(reach, kTaken * kCircle);
        }
        radius += 1.0 / radius;
        angle += kGoldenAngle;
    }
    outBlurred = vec4(light / taps, reach);
}
