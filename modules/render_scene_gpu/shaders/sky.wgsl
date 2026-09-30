// The sky behind the scene's models (D293), for WebGPU: the entries of
// sky.vert and sky.frag.

struct Sky {
    light: vec4f,
    environment: vec4f,
    toDirection: mat4x4f,
    unjittered: mat4x4f,
    previous: mat4x4f,
}

@group(0) @binding(0) var<uniform> sky: Sky;
@group(0) @binding(1) var<storage, read> exposure: vec4f;
@group(0) @binding(2) var environmentTexture: texture_cube<f32>;
@group(0) @binding(3) var environmentSampler: sampler;

struct Covered {
    @builtin(position) position: vec4f,
    @location(0) seen: vec2f,
}

@vertex
fn vs(@builtin(vertex_index) index: u32) -> Covered {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    var out: Covered;
    out.seen = vec2f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0);
    out.position = vec4f(out.seen, 0.0, 1.0);
    return out;
}

struct Shaded {
    @location(0) color: vec4f,
    @location(1) motion: vec2f,
}

@fragment
fn fs(@location(0) seen: vec2f) -> Shaded {
    var out: Shaded;
    if (sky.environment.w < 0.5) {
        out.color = vec4f(sky.light.rgb * exposure.y, 1.0);
        out.motion = vec2f(0.0);
        return out;
    }
    let toward = normalize((sky.toDirection * vec4f(seen, 0.0, 1.0)).xyz);
    let picture = textureSampleLevel(environmentTexture, environmentSampler, toward, 0.0).rgb;
    out.color = vec4f(picture * sky.light.rgb * exposure.y, 1.0);
    let now = sky.unjittered * vec4f(toward, 0.0);
    let before = sky.previous * vec4f(toward, 0.0);
    out.motion = select(vec2f(0.0), (now.xy / now.w - before.xy / before.w) * vec2f(0.5, -0.5),
                        before.w > 0.0 && now.w > 0.0);
    return out;
}
