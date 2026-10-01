// The multisampled depth's resolve (D343), for WebGPU: the entries of
// resolve.vert and resolve.frag.

@group(0) @binding(0) var depthSamples: texture_depth_multisampled_2d;

@vertex
fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
}

// resolve.frag's "depth".
@fragment
fn depth(@builtin(position) position: vec4f) -> @builtin(frag_depth) f32 {
    return textureLoad(depthSamples, vec2i(position.xy), 0);
}
