// The particles' births (ADR-0053's GPU simulation, D353), in Slang
// (ADR-0026, D475), from which the SPIR-V and the WGSL are compiled.
//
// Compute entry "spawn": each invocation one particle an emitter spawns
// this frame, written into its slot of the emitter's ring in the pool.
// The steady ones are born one step apart across the frame, the rest (its
// bursts) now; each starts in the sphere about the spawn, heading within
// the cone about the emitter's way, its life, speed, and size varied by
// the emitter's variation, all drawn from a hash of the emitter's seed,
// the particle's birth, and its place in the spawn. Nothing moves a
// particle after: its place at any age is in closed form from what is
// written here. Compute entry "clear" leaves each slot of a ring starting
// anew without a particle.

// The emitter (D352): its anchor relative to the eye; where it spawns
// relative to its anchor, and the sphere's radius; its way, and the
// cosine of the cone's half angle; its particles' speed, life, drag, and
// variation; their acceleration, and when on the particle clock the frame
// began; their size at birth and death, the step between births, and the
// clock now; their color at birth and death; its ring's first slot in the
// pool, its size, where this spawn starts in it, and how many it spawns;
// how many of them are steady, its seed, its material's place, and its
// flipbook's columns and rows, sixteen bits each; and the velocity its
// particles inherit (D359).
struct Emitter
{
    float4 anchor;
    float4 origin;
    float4 direction;
    float4 motion;
    float4 acceleration;
    float4 sizes;
    float4 colorStart;
    float4 colorEnd;
    uint4 ring;
    uint4 more;
    float4 inherited;
}

// A particle: where it started relative to its emitter's anchor, and when
// it was born; its velocity at birth, and its life (nought for none); and
// the scale on its size.
struct Particle
{
    float4 start;
    float4 velocity;
    float4 extra;
}

[[vk::binding(0, 0)]] ConstantBuffer<Emitter> emitter;
[[vk::binding(1, 0)]] RWStructuredBuffer<Particle> particles;

static const float kPeriod = 4096.0;
static const float kTau = 6.28318531;

// O'Neill's PCG hash of one word.
uint hashed(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// The `index`th number of a particle's `seed`, from nought to one.
float drawn(uint seed, uint index)
{
    return float(hashed(seed + index * 1640531527u) >> 8u) * (1.0 / 16777216.0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void clear(uint3 invocation : SV_DispatchThreadID)
{
    if (invocation.x < emitter.ring.y)
    {
        particles[emitter.ring.x + invocation.x].velocity = float4(0.0);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void spawn(uint3 invocation : SV_DispatchThreadID)
{
    const uint at = invocation.x;
    if (at >= emitter.ring.w)
    {
        return;
    }
    const uint slot = emitter.ring.x + (emitter.ring.z + at) % emitter.ring.y;
    // A remainder toward minus infinity, as GLSL's mod (fmod truncates).
    const float steady = emitter.acceleration.w + emitter.sizes.z * float(at + 1u);
    const float born = at < emitter.more.x ? steady - kPeriod * floor(steady / kPeriod) : emitter.sizes.w;
    const uint seed = hashed(emitter.more.y ^ hashed(asuint(born) + at * 2654435769u));
    const float variation = emitter.motion.w;
    const float life = emitter.motion.y * (1.0 + variation * (2.0 * drawn(seed, 0u) - 1.0));
    const float speed = emitter.motion.x * (1.0 + variation * (2.0 * drawn(seed, 1u) - 1.0));
    const float scale = 1.0 + variation * (2.0 * drawn(seed, 2u) - 1.0);
    // Within the cone, evenly over its cap.
    const float3 way = emitter.direction.xyz;
    const float cosine = 1.0 - drawn(seed, 3u) * (1.0 - emitter.direction.w);
    const float sine = sqrt(max(1.0 - cosine * cosine, 0.0));
    const float turn = kTau * drawn(seed, 4u);
    const float3 across = normalize(cross(abs(way.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0), way));
    const float3 up = cross(way, across);
    const float3 heading = way * cosine + (across * cos(turn) + up * sin(turn)) * sine;
    // Within the sphere, evenly through it.
    const float height = 2.0 * drawn(seed, 5u) - 1.0;
    const float around = kTau * drawn(seed, 6u);
    const float radius = emitter.origin.w * pow(drawn(seed, 7u), 1.0 / 3.0);
    const float flat = sqrt(max(1.0 - height * height, 0.0));
    const float3 offset = radius * float3(flat * cos(around), height, flat * sin(around));
    particles[slot].start = float4(emitter.origin.xyz + offset, born);
    particles[slot].velocity = float4(heading * speed + emitter.inherited.xyz, max(life, 0.0));
    particles[slot].extra = float4(max(scale, 0.0), 0.0, 0.0, 0.0);
}
