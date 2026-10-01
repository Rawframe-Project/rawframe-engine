// The particles (ADR-0053, D353), vertex entry "vs": one square facing
// the eye for each slot of an emitter's ring, drawn as its instance, where
// its age puts it: from where it started, its velocity slowed by its drag
// and turned by its acceleration in closed form, its size and color
// between its birth's and its death's, and its flipbook's cell by its age
// (D359); none for a slot whose particle is dead or was never born.

#version 450

// The view (D353, D357): its projection of the World relative to the eye;
// the eye's right and up in the World's axes, and the particle clock now
// and its period; and the near plane, and one where the view is flat (a
// canvas's), facing straight down its forward.
layout(set = 0, binding = 0, std140) uniform View
{
    mat4 viewProjection;
    vec4 right;
    vec4 up;
    vec4 lens;
}
view;

// The emitter, as the spawn reads it (D352, D353).
layout(set = 0, binding = 1, std140) uniform Emitter
{
    vec4 anchor;
    vec4 origin;
    vec4 direction;
    vec4 motion;
    vec4 acceleration;
    vec4 sizes;
    vec4 colorStart;
    vec4 colorEnd;
    uvec4 ring;
    uvec4 more;
    vec4 inherited;
}
emitter;

struct Particle
{
    vec4 start;
    vec4 velocity;
    vec4 extra;
};

layout(set = 0, binding = 2, std430) readonly buffer Pool
{
    Particle particles[];
}
pool;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
// How deep the particle fades into what is behind it: half its size.
layout(location = 2) out float outSoft;
layout(location = 3) out vec2 outShape;

const float kPeriod = 4096.0;

void main()
{
    const Particle kParticle = pool.particles[gl_InstanceIndex];
    const float kAge = mod(view.right.w - kParticle.start.w + kPeriod, kPeriod);
    const float kLife = kParticle.velocity.w;
    if (kLife <= 0.0 || kAge >= kLife) {
        outUv = vec2(0.0);
        outColor = vec4(0.0);
        outSoft = 1.0;
        outShape = vec2(0.0);
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    // Where x'' = a - k x' takes it from rest at its start: in closed form
    // with drag, uniformly accelerated without.
    const float kDrag = emitter.motion.z;
    const vec3 kAcceleration = emitter.acceleration.xyz;
    vec3 moved = kParticle.velocity.xyz * kAge + 0.5 * kAcceleration * kAge * kAge;
    if (kDrag > 1e-4) {
        const float kSlowed = (1.0 - exp(-kDrag * kAge)) / kDrag;
        moved = kParticle.velocity.xyz * kSlowed + kAcceleration * (kAge - kSlowed) / kDrag;
    }
    const float kThrough = kAge / kLife;
    const float kSize = mix(emitter.sizes.x, emitter.sizes.y, kThrough) * kParticle.extra.x;
    // Two triangles: corners at (0,0), (1,0), (0,1), (0,1), (1,0), (1,1).
    const uint kCorner = uint[6](0u, 1u, 2u, 2u, 1u, 3u)[gl_VertexIndex % 6];
    const vec2 kAt = vec2(float(kCorner & 1u), float(kCorner >> 1u));
    const vec3 kPlaced = emitter.anchor.xyz + kParticle.start.xyz + moved +
                         (view.right.xyz * (kAt.x - 0.5) + view.up.xyz * (kAt.y - 0.5)) * kSize;
    // Its flipbook's cell, stepped over its life (D359), left to right and
    // top to bottom; the whole texture where it has none.
    const uvec2 kCells = uvec2(emitter.more.w & 0xFFFFu, emitter.more.w >> 16u);
    const uint kFrames = max(kCells.x * kCells.y, 1u);
    const uint kFrame = min(uint(kThrough * float(kFrames)), kFrames - 1u);
    const vec2 kCell = vec2(float(kFrame % max(kCells.x, 1u)), float(kFrame / max(kCells.x, 1u)));
    outUv = (kCell + vec2(kAt.x, 1.0 - kAt.y)) / vec2(max(kCells, uvec2(1u)));
    outColor = mix(emitter.colorStart, emitter.colorEnd, kThrough);
    outSoft = max(0.5 * kSize, 1e-4);
    outShape = kAt * 2.0 - 1.0;
    gl_Position = view.viewProjection * vec4(kPlaced, 1.0);
}
