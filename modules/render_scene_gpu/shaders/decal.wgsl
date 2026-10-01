// The decals' atlas (D339), for WebGPU: the entries of decal.vert and
// decal.frag.

@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var blended: sampler;

struct Corner {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs(@builtin(vertex_index) index: u32) -> Corner {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    var out: Corner;
    out.uv = corner;
    out.position = vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    return out;
}

// decal.frag's "fill".
@fragment
fn fill(@location(0) uv: vec2f) -> @location(0) vec4f {
    return textureSample(source, blended, uv);
}
