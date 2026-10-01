// The particles (D353) and ribbons (D354), for WebGPU: the entries of
// particle.vert, particle.ribbon.vert, and particle.frag. The ribbons'
// entry reads the table's slots 2 and 3 as its ribbon and its points.

struct Frame {
    viewProjection: mat4x4f,
    toSun: vec4f,
    sun: vec4f,
    sky: vec4f,
    exposure: vec4f,
    forward: vec4f,
    cascadeFar: vec4f,
    cascadeTexel: vec4f,
    shadow: vec4f,
    cascades: array<mat4x4f, 4>,
    clusterGrid: vec4f,
    clusterDepth: vec4f,
    unjittered: mat4x4f,
    previous: mat4x4f,
    ground: vec4f,
    environment: vec4f,
    irradiance: array<vec4f, 9>,
    occlusion: vec4f,
    reflections: vec4f,
    contact: vec4f,
    decals: vec4f,
}

struct View {
    right: vec4f,
    up: vec4f,
    lens: vec4f,
}

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

struct Ribbon {
    range: vec4u,
}

struct Point {
    placeWidth: vec4f,
    color: vec4f,
    along: vec4f,
}

@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var<uniform> view: View;
@group(0) @binding(2) var<uniform> emitter: Emitter;
@group(0) @binding(3) var<storage, read> particles: array<Particle>;
@group(0) @binding(2) var<uniform> drawnRibbon: Ribbon;
@group(0) @binding(3) var<storage, read> ribbonPoints: array<Point>;
@group(0) @binding(4) var<storage, read> exposure: vec4f;
@group(0) @binding(5) var<storage, read> materials: array<vec4f>;
@group(0) @binding(6) var depth: texture_depth_2d;
@group(0) @binding(7) var baseTexture: texture_2d<f32>;
@group(0) @binding(8) var baseSampler: sampler;
@group(0) @binding(9) var emissionTexture: texture_2d<f32>;
@group(0) @binding(10) var emissionSampler: sampler;

const period = 4096.0;

struct Corner {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
    @location(2) soft: f32,
    @location(3) @interpolate(flat) material: u32,
    @location(4) shape: vec2f,
}

@vertex
fn vs(@builtin(vertex_index) index: u32, @builtin(instance_index) instance: u32) -> Corner {
    let particle = particles[instance];
    let since = view.right.w - particle.start.w + period;
    let age = since - period * floor(since / period);
    let life = particle.velocity.w;
    var made: Corner;
    if (life <= 0.0 || age >= life) {
        made.uv = vec2f(0.0);
        made.color = vec4f(0.0);
        made.soft = 1.0;
        made.material = 0u;
        made.shape = vec2f(0.0);
        made.position = vec4f(2.0, 2.0, 2.0, 1.0);
        return made;
    }
    let drag = emitter.motion.z;
    let acceleration = emitter.acceleration.xyz;
    var moved = particle.velocity.xyz * age + 0.5 * acceleration * age * age;
    if (drag > 1e-4) {
        let slowed = (1.0 - exp(-drag * age)) / drag;
        moved = particle.velocity.xyz * slowed + acceleration * (age - slowed) / drag;
    }
    let through = age / life;
    let size = mix(emitter.sizes.x, emitter.sizes.y, through) * particle.extra.x;
    var corners = array<u32, 6>(0u, 1u, 2u, 2u, 1u, 3u);
    let corner = corners[index % 6u];
    let at = vec2f(f32(corner & 1u), f32(corner >> 1u));
    let placed = emitter.anchor.xyz + particle.start.xyz + moved +
                 (view.right.xyz * (at.x - 0.5) + view.up.xyz * (at.y - 0.5)) * size;
    made.uv = vec2f(at.x, 1.0 - at.y);
    made.color = mix(emitter.colorStart, emitter.colorEnd, through);
    made.soft = max(0.5 * size, 1e-4);
    made.material = emitter.more.z;
    made.shape = at * 2.0 - 1.0;
    made.position = frame.viewProjection * vec4f(placed, 1.0);
    return made;
}

@vertex
fn ribbon(@builtin(vertex_index) index: u32) -> Corner {
    var corners = array<u32, 6>(0u, 1u, 2u, 2u, 1u, 3u);
    let corner = corners[index % 6u];
    let first = drawnRibbon.range.x;
    let last = drawnRibbon.range.x + drawnRibbon.range.y - 1u;
    let at = first + index / 6u + (corner >> 1u);
    let point = ribbonPoints[at];
    let runs = ribbonPoints[min(at + 1u, last)].placeWidth.xyz - ribbonPoints[max(at, first + 1u) - 1u].placeWidth.xyz;
    var across = cross(runs, -point.placeWidth.xyz);
    if (dot(across, across) > 1e-12) {
        across = normalize(across);
    } else {
        across = view.right.xyz;
    }
    let side = f32(corner & 1u) - 0.5;
    var made: Corner;
    made.uv = vec2f(point.along.x, f32(corner & 1u));
    made.color = point.color;
    made.soft = max(0.5 * point.placeWidth.w, 1e-4);
    made.material = drawnRibbon.range.z;
    made.shape = vec2f(0.0, side * 2.0);
    made.position = frame.viewProjection * vec4f(point.placeWidth.xyz + across * (side * point.placeWidth.w), 1.0);
    return made;
}

@fragment
fn fs(@builtin(position) position: vec4f,
      @location(0) uv: vec2f,
      @location(1) color: vec4f,
      @location(2) soft: f32,
      @location(3) @interpolate(flat) material: u32,
      @location(4) shape: vec2f) -> @location(0) vec4f {
    let at = min(material, arrayLength(&materials) / 9u - 1u) * 9u;
    let base = materials[at];
    let emission = materials[at + 2u];
    let rest = materials[at + 3u];
    let baseMap = materials[at + 4u];
    let emissionMap = materials[at + 6u];
    let flags = u32(rest.w);
    // Sampled before anything branches, as WGSL's uniformity asks.
    let sampled = textureSample(baseTexture, baseSampler, uv * baseMap.xy + baseMap.zw);
    let glowing = textureSample(emissionTexture, emissionSampler, uv * emissionMap.xy + emissionMap.zw).rgb;
    let behind = textureLoad(depth, vec2i(position.xy), 0);
    if (position.z < behind) {
        discard;
    }
    var fade = 1.0;
    if (behind > 0.0) {
        fade = clamp((view.lens.x / behind - view.lens.x / position.z) / soft, 0.0, 1.0);
    }
    var tinted = vec3f(1.0);
    if ((flags & 2u) != 0u) {
        tinted = sampled.rgb;
    }
    let shade = color.rgb * base.rgb * tinted;
    var disc = 1.0 - smoothstep(0.5, 1.0, length(shape));
    if ((flags & 4u) != 0u) {
        disc = sampled.a;
    }
    var glow = vec3f(1.0);
    if ((flags & 8u) != 0u) {
        glow = glowing;
    }
    let opacity = clamp(rest.x * color.a * disc * fade, 0.0, 1.0);
    let shine = emission.rgb * glow * color.a * disc * fade * exposure.y;
    return vec4f(shade * opacity + shine, opacity);
}
