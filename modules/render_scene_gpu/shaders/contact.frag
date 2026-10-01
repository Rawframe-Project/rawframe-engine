// The scene's contact shadows (ADR-0051, D338), fragment entry "shade":
// from each point the prepass left, sixteen steps toward the sun over the
// camera's length, each seen from the eye. Where one passes behind what
// the prepass left there by less than the length (what stands there is
// taken to be at least as thick), something stands between the point and
// the sun, and the point is shaded from it, less so the farther along the
// meeting is. The first step is jittered per pixel, which the temporal
// pass averages away.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D depth;

// The jittered view's inverse, from clip space to the eye-relative World;
// the view; the direction toward the sun; the length and the near plane.
layout(set = 0, binding = 1, std140) uniform Contact
{
    mat4 toPoint;
    mat4 viewProjection;
    vec4 toSun;
    vec4 settings;
}
contact;

layout(location = 0) in vec2 inUv;

layout(location = 0) out float outLit;

const int kSteps = 16;

// Where a place is on the target, rows top first, and how far ahead.
vec3 seenAt(vec3 place)
{
    const vec4 kClip = contact.viewProjection * vec4(place, 1.0);
    return vec3(kClip.x / kClip.w * 0.5 + 0.5, 0.5 - kClip.y / kClip.w * 0.5, kClip.w);
}

void main()
{
    outLit = 1.0;
    const ivec2 kAt = ivec2(gl_FragCoord.xy);
    const float kDepth = texelFetch(depth, kAt, 0).r;
    if (kDepth <= 0.0) {
        return;
    }
    const vec2 kSize = vec2(textureSize(depth, 0));
    const vec2 kHere = (vec2(kAt) + 0.5) / kSize;
    const vec4 kSeen = contact.toPoint * vec4(kHere.x * 2.0 - 1.0, 1.0 - kHere.y * 2.0, kDepth, 1.0);
    const vec3 kPoint = kSeen.xyz / kSeen.w;
    const float kLength = contact.settings.x;
    const float kNear = contact.settings.y;
    const float kStep = kLength / float(kSteps);
    const float kThickness = kLength;
    // Interleaved gradient noise (Jimenez 2014).
    const float kJitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    for (int step = 0; step < kSteps; ++step) {
        const float kAlong = kStep * (float(step) + 0.5 + kJitter);
        const vec3 kOn = seenAt(kPoint + contact.toSun.xyz * kAlong);
        if (kOn.z <= kNear || any(lessThan(kOn.xy, vec2(0.0))) || any(greaterThanEqual(kOn.xy, vec2(1.0)))) {
            return;
        }
        const float kThere = texelFetch(depth, ivec2(kOn.xy * kSize), 0).r;
        if (kThere <= 0.0) {
            continue;
        }
        const float kBehind = kOn.z - kNear / kThere;
        if (kBehind > 0.002 * kOn.z && kBehind < kThickness) {
            outLit = smoothstep(0.5, 1.0, kAlong / kLength);
            return;
        }
    }
}
