// The reflection probes' atlas (D340), for WebGPU: the entries of
// probe.vert and probe.frag.

@group(0) @binding(0) var source: texture_cube<f32>;
@group(0) @binding(1) var blended: sampler;

struct Corner {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) @interpolate(flat) at: u32,
}

@vertex
fn vs(@builtin(vertex_index) index: u32, @builtin(instance_index) instance: u32) -> Corner {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    var out: Corner;
    out.uv = corner;
    out.at = instance;
    out.position = vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    return out;
}

// probe.frag's "fill".
@fragment
fn fill(@location(0) uv: vec2f, @location(1) @interpolate(flat) at: u32) -> @location(0) vec4f {
    let face = at / 8u;
    let s = uv.x * 2.0 - 1.0;
    let t = uv.y * 2.0 - 1.0;
    var toward = vec3f(-s, -t, -1.0);
    if (face == 0u) {
        toward = vec3f(1.0, -t, -s);
    } else if (face == 1u) {
        toward = vec3f(-1.0, -t, s);
    } else if (face == 2u) {
        toward = vec3f(s, 1.0, t);
    } else if (face == 3u) {
        toward = vec3f(s, -1.0, -t);
    } else if (face == 4u) {
        toward = vec3f(s, -t, 1.0);
    }
    let last = f32(textureNumLevels(source) - 1u);
    return textureSampleLevel(source, blended, toward, f32(at % 8u) / 7.0 * last);
}
