// The scene's picture (D284), in Slang (ADR-0026, D475): the pre-exposed
// scene-linear light of the texel under it, mixed with the bloom's spread
// light as the camera asks (D328), graded as the camera asks (D294,
// ADR-0051: white balance, ASC CDL, saturation, contrast about middle
// grey, then its grading table, D344), then mapped for display by the
// camera's tonemapper (D295, ADR-0047's closed set: AgX, the default;
// Khronos PBR Neutral; linear), each keeping middle grey where AgX puts
// it, in linear light for the sRGB picture to encode, dithered by under
// one step of that encoding (D332). AgX as Troy Sobotka made it, fitted by
// Benjamin Wrensch.

import screen;

// The grade: the white balance's rows; the slope, with the saturation;
// the offset, with the contrast; the power, with whether to grade at all.
// Then the tonemapper's number, and what it scales light by first.
struct Grade
{
    float4 balance[3];
    float4 slope;
    float4 offset;
    float4 power;
    float4 tonemapper;
    // The bloom's share, and one over its chain's levels (D328).
    float4 bloom;
    // One where the picture is dithered (D332); one where its colors are
    // looked up in the grading table (D344); one where it is graded only,
    // its light written for the post processes before the tonemapper
    // (D350).
    float4 display;
}

[[vk::binding(0, 0)]] Texture2D scene;
[[vk::binding(1, 0)]] ConstantBuffer<Grade> grade;
// The bloom's spread light, summed over its chain's levels (D328), at a
// quarter of the target's sides.
[[vk::binding(2, 0)]] Texture2D bloom;
[[vk::binding(3, 0)]] SamplerState blended;
// The grading table (D344): a volume of colors, red along x, green along
// y, blue along its slices, each axis the ACEScct curve's encoding.
[[vk::binding(4, 0)]] Texture3D table;

float3 contrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

// The ACEScct curve (S-2016-001) and its inverse: linear light to the
// encoding a grading table is laid out along, and back.
float3 acescct(float3 light)
{
    return lerp(10.5402377416545 * light + 0.0729055341958355,
                (log2(max(light, float3(1e-10))) + 9.72) / 17.52,
                step(float3(0.0078125), light));
}

float3 linearOf(float3 encoded)
{
    return lerp((encoded - 0.0729055341958355) / 10.5402377416545,
                exp2(encoded * 17.52 - 9.72),
                step(float3(0.155251141552511), encoded));
}

// The light looked up in the grading table, its texels' middles at the
// encoding's nought and one.
float3 tabled(float3 light)
{
    uint width;
    uint height;
    uint depth;
    table.GetDimensions(width, height, depth);
    const float3 side = float3(float(width), float(height), float(depth));
    const float3 at = clamp(acescct(light), 0.0, 1.0) * (side - 1.0) / side + 0.5 / side;
    return linearOf(table.SampleLevel(blended, at, 0.0).rgb);
}

float3 graded(float3 color)
{
    if (grade.power.w < 0.5)
    {
        return color;
    }
    color = float3(dot(grade.balance[0].xyz, color), dot(grade.balance[1].xyz, color), dot(grade.balance[2].xyz, color));
    color = pow(max(color * grade.slope.xyz + grade.offset.xyz, float3(0.0)), grade.power.xyz);
    const float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = max(float3(luma) + (color - float3(luma)) * grade.slope.w, float3(0.0));
    return 0.18 * pow(color / 0.18, float3(grade.offset.w));
}

// Khronos PBR Neutral, as Khronos specifies it.
float3 neutral(float3 color)
{
    const float start = 0.8 - 0.04;
    const float desaturation = 0.15;
    const float least = min(color.r, min(color.g, color.b));
    const float offset = least < 0.08 ? least - 6.25 * least * least : 0.04;
    color -= offset;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < start)
    {
        return color;
    }
    const float room = 1.0 - start;
    const float newPeak = 1.0 - room * room / (peak + room - start);
    color *= newPeak / peak;
    const float grey = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak), grey);
}

// The sRGB encoding of linear light, and back.
float3 encoded(float3 light)
{
    return lerp(light * 12.92, 1.055 * pow(light, float3(1.0 / 2.4)) - 0.055, step(float3(0.0031308), light));
}

float3 decoded(float3 encoding)
{
    return lerp(encoding / 12.92, pow((encoding + 0.055) / 1.055, float3(2.4)), step(float3(0.04045), encoding));
}

// ADR-0051's debanding: the picture is kept in eight bits, sRGB-encoded,
// where a smooth gradient falls into bands a step apart. Under a step of
// noise in that encoding, fixed to the pixel, breaks them (Gjøl's
// screen-space dither, as Bevy's).
float4 shown(float3 light, float2 pixel)
{
    float3 color = clamp(light, 0.0, 1.0);
    if (grade.display.x > 0.5)
    {
        const float3 noise = frac(float3(dot(float2(171.0, 231.0), floor(pixel))) / float3(103.0, 71.0, 97.0));
        color = decoded(clamp(encoded(color) + (noise - 0.5) / 255.0, 0.0, 1.0));
    }
    return float4(color, 1.0);
}

[shader("vertex")]
Screen vs(uint vertex : SV_VulkanVertexID)
{
    return screenOf(vertex);
}

[shader("fragment")]
float4 fs(Screen screen) : SV_Target0
{
    // AgX's matrices, written a column to a row, as GLSL wrote them, so a
    // vector multiplies them from the left.
    const float3x3 inset = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992,
                                    0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434,
                                    0.879142973793104);
    const float3x3 outset = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368,
                                     1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433,
                                     1.15107367264116);
    const float lowest = -12.47393;
    const float highest = 4.026069;
    uint width;
    uint height;
    scene.GetDimensions(width, height);
    const int2 size = int2(int(width), int(height));
    const int2 texel = min(int2(screen.uv * float2(size)), size - 1);
    float3 seen = scene.Load(int3(texel, 0)).rgb;
    if (grade.bloom.x > 0.0)
    {
        seen = lerp(seen, bloom.SampleLevel(blended, screen.uv, 0.0).rgb * grade.bloom.y, grade.bloom.x);
    }
    float3 light = graded(seen);
    if (grade.display.y > 0.5)
    {
        light = tabled(light);
    }
    // Graded only, for the post processes before the tonemapper (D350).
    if (grade.display.z > 0.5)
    {
        return float4(light, 1.0);
    }
    const float3 scaled = light * grade.tonemapper.y;
    if (grade.tonemapper.x > 1.5)
    {
        return shown(scaled, screen.position.xy);
    }
    if (grade.tonemapper.x > 0.5)
    {
        return shown(neutral(max(scaled, float3(0.0))), screen.position.xy);
    }
    float3 color = mul(max(scaled, float3(1e-10)), inset);
    color = (clamp(log2(color), lowest, highest) - lowest) / (highest - lowest);
    color = mul(contrast(color), outset);
    return shown(pow(max(color, float3(0.0)), float3(2.2)), screen.position.xy);
}
