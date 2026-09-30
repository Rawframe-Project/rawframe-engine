// The sky behind the scene's models (D293), for WebGPU: the entries of
// sky.vert and sky.frag.

struct Sky {
    light: vec4f,
}

@group(0) @binding(0) var<uniform> sky: Sky;
@group(0) @binding(1) var<storage, read> exposure: vec4f;

@vertex
fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
}

struct Shaded {
    @location(0) color: vec4f,
    @location(1) motion: vec2f,
}

@fragment
fn fs() -> Shaded {
    var out: Shaded;
    out.color = vec4f(sky.light.rgb * exposure.y, 1.0);
    out.motion = vec2f(0.0);
    return out;
}
