// A post process (D350), for WebGPU: the entries of post.vert and
// post.frag.

@group(0) @binding(0) var picture: texture_2d<f32>;

struct Process {
    fixedPart: vec4f,
    scenePart: vec4f,
    texturePart: vec4f,
    bothPart: vec4f,
    map: vec4f,
    weight: vec4f,
}

@group(0) @binding(1) var<uniform> process: Process;
@group(0) @binding(2) var sampled: texture_2d<f32>;
@group(0) @binding(3) var sampling: sampler;

struct Corner {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs(@builtin(vertex_index) index: u32) -> Corner {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    var made: Corner;
    made.uv = corner;
    made.position = vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    return made;
}

@fragment
fn fs(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {
    let size = vec2i(textureDimensions(picture, 0));
    let texel = min(vec2i(uv * vec2f(size)), size - 1);
    let seen = textureLoad(picture, texel, 0).rgb;
    let tex = textureSampleLevel(sampled, sampling, uv * process.map.xy + process.map.zw, 0.0);
    let color = process.fixedPart.rgb + process.scenePart.rgb * seen + process.texturePart.rgb * tex.rgb +
                process.bothPart.rgb * seen * tex.rgb;
    let alpha = clamp(process.fixedPart.a + process.texturePart.a * tex.a, 0.0, 1.0);
    return vec4f(mix(seen, max(color, vec3f(0.0)), alpha * process.weight.x), 1.0);
}
