// The scene's screen-space reflections (ADR-0051, D331), fragment entry
// "march": from each point the prepass left smooth enough, its reflection
// is followed through the World in steps, each seen from the eye, until it
// passes behind what the prepass left there by less than a step and a
// half; the meeting is narrowed by halving four times, and what the view
// saw there the frame before, where that place was then, is what the
// point reflects. Its weight fades as the surface roughens, toward the
// target's edges, toward the far end of the distance, and for reflections
// turning back toward the eye, which the view cannot have seen.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D depth;
layout(set = 0, binding = 1) uniform texture2D surfaces;
layout(set = 0, binding = 2) uniform texture2D before;
layout(set = 0, binding = 3) uniform sampler blended;

// The jittered view's inverse, from clip space to the eye-relative World;
// the view; the frame before's view taking this frame's places; and the
// distance followed and the near plane.
layout(set = 0, binding = 4, std140) uniform Reflections
{
    mat4 toPoint;
    mat4 viewProjection;
    mat4 previous;
    vec4 settings;
}
reflections;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outReflected;

const int kSteps = 32;

// Where a place is on the target, rows top first, and how far ahead.
vec3 seenAt(mat4 view, vec3 place)
{
    const vec4 kClip = view * vec4(place, 1.0);
    return vec3(kClip.x / kClip.w * 0.5 + 0.5, 0.5 - kClip.y / kClip.w * 0.5, kClip.w);
}

void main()
{
    outReflected = vec4(0.0);
    const ivec2 kAt = ivec2(gl_FragCoord.xy);
    const float kDepth = texelFetch(depth, kAt, 0).r;
    const vec4 kSurface = texelFetch(surfaces, kAt, 0);
    const float kSmooth = 1.0 - smoothstep(0.2, 0.4, kSurface.w);
    if (kDepth <= 0.0 || kSmooth <= 0.0) {
        return;
    }
    const vec2 kSize = vec2(textureSize(depth, 0));
    const float kDistance = reflections.settings.x;
    const float kNear = reflections.settings.y;
    const vec2 kHere = (vec2(kAt) + 0.5) / kSize;
    const vec4 kSeen = reflections.toPoint * vec4(kHere.x * 2.0 - 1.0, 1.0 - kHere.y * 2.0, kDepth, 1.0);
    const vec3 kPoint = kSeen.xyz / kSeen.w;
    const vec3 kNormal = normalize(kSurface.xyz);
    const vec3 kToward = reflect(normalize(kPoint), kNormal);
    // Turning back toward the eye, it meets what the view cannot see.
    const float kAway = smoothstep(-0.2, 0.2, dot(kToward, normalize(kPoint)));
    if (kAway <= 0.0) {
        return;
    }
    const float kStep = kDistance / float(kSteps);
    const float kThickness = max(kStep * 1.5, 0.05);
    // From a little off the surface, so it does not meet itself.
    const vec3 kFrom = kPoint + kNormal * 0.02;
    float near = 0.0;
    float far = -1.0;
    for (int step = 1; step <= kSteps; ++step) {
        const float kAlong = kStep * float(step);
        const vec3 kOn = seenAt(reflections.viewProjection, kFrom + kToward * kAlong);
        if (kOn.z <= kNear || any(lessThan(kOn.xy, vec2(0.0))) || any(greaterThanEqual(kOn.xy, vec2(1.0)))) {
            break;
        }
        const float kThere = texelFetch(depth, ivec2(kOn.xy * kSize), 0).r;
        const float kBehind = kThere > 0.0 ? kOn.z - kNear / kThere : -1.0;
        if (kBehind > 0.0 && kBehind < kThickness) {
            far = kAlong;
            break;
        }
        near = kAlong;
    }
    if (far < 0.0) {
        return;
    }
    for (int halving = 0; halving < 4; ++halving) {
        const float kMiddle = (near + far) * 0.5;
        const vec3 kOn = seenAt(reflections.viewProjection, kFrom + kToward * kMiddle);
        const float kThere = texelFetch(depth, ivec2(clamp(kOn.xy, vec2(0.0), vec2(0.999)) * kSize), 0).r;
        if (kThere > 0.0 && kOn.z > kNear / kThere) {
            far = kMiddle;
        } else {
            near = kMiddle;
        }
    }
    const vec3 kMet = kFrom + kToward * far;
    const vec3 kThen = seenAt(reflections.previous, kMet);
    if (kThen.z <= 0.0 || any(lessThan(kThen.xy, vec2(0.0))) || any(greaterThanEqual(kThen.xy, vec2(1.0)))) {
        return;
    }
    const vec3 kOnNow = seenAt(reflections.viewProjection, kMet);
    const vec2 kFromEdge = min(kOnNow.xy, 1.0 - kOnNow.xy);
    const float kEdge = smoothstep(0.0, 0.1, min(kFromEdge.x, kFromEdge.y));
    const float kFar = 1.0 - smoothstep(0.7, 1.0, far / kDistance);
    const vec3 kLight = textureLod(sampler2D(before, blended), kThen.xy, 0.0).rgb;
    outReflected = vec4(kLight, kSmooth * kAway * kEdge * kFar);
}
