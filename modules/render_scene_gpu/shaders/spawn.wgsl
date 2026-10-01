// The particles' births (D353), for WebGPU: the entries of spawn.comp.

struct Emitter {
    anchor: vec4f,
    origin: vec4f,
    direction: vec4f,
    motion: vec4f,
    acceleration: vec4f,
    sizes: vec4f,
    colorStart: vec4f,
    colorEnd: vec4f,
    ring: vec4u,
    more: vec4u,
}

struct Particle {
    start: vec4f,
    velocity: vec4f,
    extra: vec4f,
}

@group(0) @binding(0) var<uniform> emitter: Emitter;
@group(0) @binding(1) var<storage, read_write> particles: array<Particle>;

const period = 4096.0;
const tau = 6.28318531;

fn hashed(value: u32) -> u32 {
    let state = value * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

fn drawn(seed: u32, index: u32) -> f32 {
    return f32(hashed(seed + index * 1640531527u) >> 8u) * (1.0 / 16777216.0);
}

@compute @workgroup_size(64, 1, 1)
fn spawn(@builtin(global_invocation_id) invocation: vec3u) {
    let at = invocation.x;
    if (at >= emitter.ring.w) {
        return;
    }
    let slot = emitter.ring.x + (emitter.ring.z + at) % emitter.ring.y;
    var born = emitter.sizes.w;
    if (at < emitter.more.x) {
        let since = emitter.acceleration.w + emitter.sizes.z * f32(at + 1u);
        born = since - period * floor(since / period);
    }
    let seed = hashed(emitter.more.y ^ hashed(bitcast<u32>(born) + at * 2654435769u));
    let variation = emitter.motion.w;
    let life = emitter.motion.y * (1.0 + variation * (2.0 * drawn(seed, 0u) - 1.0));
    let speed = emitter.motion.x * (1.0 + variation * (2.0 * drawn(seed, 1u) - 1.0));
    let scale = 1.0 + variation * (2.0 * drawn(seed, 2u) - 1.0);
    let way = emitter.direction.xyz;
    let cosine = 1.0 - drawn(seed, 3u) * (1.0 - emitter.direction.w);
    let sine = sqrt(max(1.0 - cosine * cosine, 0.0));
    let turn = tau * drawn(seed, 4u);
    var side = vec3f(1.0, 0.0, 0.0);
    if (abs(way.y) < 0.99) {
        side = vec3f(0.0, 1.0, 0.0);
    }
    let across = normalize(cross(side, way));
    let up = cross(way, across);
    let heading = way * cosine + (across * cos(turn) + up * sin(turn)) * sine;
    let height = 2.0 * drawn(seed, 5u) - 1.0;
    let around = tau * drawn(seed, 6u);
    let radius = emitter.origin.w * pow(drawn(seed, 7u), 1.0 / 3.0);
    let breadth = sqrt(max(1.0 - height * height, 0.0));
    let offset = radius * vec3f(breadth * cos(around), height, breadth * sin(around));
    particles[slot].start = vec4f(emitter.origin.xyz + offset, born);
    particles[slot].velocity = vec4f(heading * speed, max(life, 0.0));
    particles[slot].extra = vec4f(max(scale, 0.0), 0.0, 0.0, 0.0);
}

@compute @workgroup_size(64, 1, 1)
fn clear(@builtin(global_invocation_id) invocation: vec3u) {
    let at = invocation.x;
    if (at < emitter.ring.y) {
        particles[emitter.ring.x + at].velocity = vec4f(0.0);
    }
}
