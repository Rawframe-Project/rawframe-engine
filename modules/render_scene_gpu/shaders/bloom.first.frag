// The scene's bloom (ADR-0051, D328), fragment entry "first": the scene's
// light quartered into the first level of the bloom's chain by Jimenez's
// thirteen taps, spread twice as wide, as five overlapping boxes, each
// weighed by one over one plus its luma (Karis's average), so a lone
// bright texel does not flicker as it moves.

#version 450

layout(set = 0, binding = 0) uniform texture2D source;
layout(set = 0, binding = 1) uniform sampler blended;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

// The source's light `x` and `y` of its texels from here.
vec3 at(float x, float y)
{
    const vec2 kTexel = 1.0 / vec2(textureSize(sampler2D(source, blended), 0));
    return textureLod(sampler2D(source, blended), inUv + vec2(x, y) * kTexel, 0.0).rgb;
}

float weightOf(vec3 light)
{
    return 1.0 / (1.0 + dot(light, vec3(0.2126, 0.7152, 0.0722)));
}

void main()
{
    const vec3 kA = at(-4.0, -4.0);
    const vec3 kB = at(0.0, -4.0);
    const vec3 kC = at(4.0, -4.0);
    const vec3 kD = at(-4.0, 0.0);
    const vec3 kE = at(0.0, 0.0);
    const vec3 kF = at(4.0, 0.0);
    const vec3 kG = at(-4.0, 4.0);
    const vec3 kH = at(0.0, 4.0);
    const vec3 kI = at(4.0, 4.0);
    const vec3 kJ = at(-2.0, -2.0);
    const vec3 kK = at(2.0, -2.0);
    const vec3 kL = at(-2.0, 2.0);
    const vec3 kM = at(2.0, 2.0);
    const vec3 kBoxes[5] = vec3[]((kJ + kK + kL + kM) * 0.25,
                                  (kA + kB + kD + kE) * 0.25,
                                  (kB + kC + kE + kF) * 0.25,
                                  (kD + kE + kG + kH) * 0.25,
                                  (kE + kF + kH + kI) * 0.25);
    const float kShares[5] = float[](0.5, 0.125, 0.125, 0.125, 0.125);
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (int box = 0; box < 5; ++box) {
        const float kWeight = kShares[box] * weightOf(kBoxes[box]);
        sum += kBoxes[box] * kWeight;
        weight += kWeight;
    }
    outColor = vec4(sum / weight, 1.0);
}
