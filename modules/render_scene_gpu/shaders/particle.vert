// The particles (ADR-0053, D353), vertex entry "vs": one square facing
// the eye for each slot of an emitter's ring, drawn as its instance, where
// its age puts it: from where it started, its velocity slowed by its drag
// and turned by its acceleration in closed form, its size and color
// between its birth's and its death's; none for a slot whose particle is
// dead or was never born.

#version 450

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 viewProjection;
    vec4 toSun;
    vec4 sun;
    vec4 sky;
    vec4 exposure;
    // The eye's forward; each cascade's far end and texel; the cascades,
    // the shadows' distance, a cascade's side in texels (D289), and one
    // where the shadows are filtered soft (D330).
    vec4 forward;
    vec4 cascadeFar;
    vec4 cascadeTexel;
    vec4 shadow;
    mat4 cascades[4];
    // The clusters' tiles across and down, their slices, and the lights;
    // their near end, and the slices over the log of their far over near
    // (D290).
    vec4 clusterGrid;
    vec4 clusterDepth;
    // The view and projection without the jitter, and the frame before's
    // taking this frame's places (D291).
    mat4 unjittered;
    mat4 previous;
    // The ground's luminance below the horizon (D304).
    vec4 ground;
    // The sky's picture (D322): its levels, nought for none; and the
    // irradiance over π it gives, per unit of the sky's light, as nine
    // spherical harmonics' coefficients.
    vec4 environment;
    vec4 irradiance[9];
    // Whether the view's ambient occlusion is on (D327), its screen-space
    // reflections (D331), its contact shadows (D338), and whether it has
    // decals (D339).
    vec4 occlusion;
    vec4 reflections;
    vec4 contact;
    vec4 decals;
}
frame;

// The eye's right and up in the World's axes, and the particle clock now;
// and the near plane (D353).
layout(set = 0, binding = 1, std140) uniform View
{
    vec4 right;
    vec4 up;
    vec4 lens;
}
view;

// The emitter, as the spawn reads it (D352, D353).
layout(set = 0, binding = 2, std140) uniform Emitter
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
}
emitter;

struct Particle
{
    vec4 start;
    vec4 velocity;
    vec4 extra;
};

layout(set = 0, binding = 3, std430) readonly buffer Pool
{
    Particle particles[];
}
pool;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
// How deep the particle fades into what is behind it: half its size.
layout(location = 2) out float outSoft;
// Its material's place among the frame's materials.
layout(location = 3) flat out uint outMaterial;
layout(location = 4) out vec2 outShape;

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
        outMaterial = 0u;
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
    outUv = vec2(kAt.x, 1.0 - kAt.y);
    outColor = mix(emitter.colorStart, emitter.colorEnd, kThrough);
    outSoft = max(0.5 * kSize, 1e-4);
    outMaterial = emitter.more.z;
    outShape = kAt * 2.0 - 1.0;
    gl_Position = frame.viewProjection * vec4(kPlaced, 1.0);
}
