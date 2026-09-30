// The scene's bloom (D328), for WebGPU: the entries of bloom.vert,
// bloom.first.frag, bloom.down.frag, and bloom.up.frag.

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

fn at(uv: vec2f, x: f32, y: f32) -> vec3f {
    let texel = 1.0 / vec2f(textureDimensions(source));
    return textureSampleLevel(source, blended, uv + vec2f(x, y) * texel, 0.0).rgb;
}

fn weightOf(light: vec3f) -> f32 {
    return 1.0 / (1.0 + dot(light, vec3f(0.2126, 0.7152, 0.0722)));
}

// bloom.first.frag's "first".
@fragment
fn first(@location(0) uv: vec2f) -> @location(0) vec4f {
    let a = at(uv, -4.0, -4.0);
    let b = at(uv, 0.0, -4.0);
    let c = at(uv, 4.0, -4.0);
    let d = at(uv, -4.0, 0.0);
    let e = at(uv, 0.0, 0.0);
    let f = at(uv, 4.0, 0.0);
    let g = at(uv, -4.0, 4.0);
    let h = at(uv, 0.0, 4.0);
    let i = at(uv, 4.0, 4.0);
    let j = at(uv, -2.0, -2.0);
    let k = at(uv, 2.0, -2.0);
    let l = at(uv, -2.0, 2.0);
    let m = at(uv, 2.0, 2.0);
    let boxes = array<vec3f, 5>((j + k + l + m) * 0.25, (a + b + d + e) * 0.25, (b + c + e + f) * 0.25,
                                (d + e + g + h) * 0.25, (e + f + h + i) * 0.25);
    let shares = array<f32, 5>(0.5, 0.125, 0.125, 0.125, 0.125);
    var sum = vec3f(0.0);
    var weight = 0.0;
    for (var box = 0; box < 5; box++) {
        let w = shares[box] * weightOf(boxes[box]);
        sum += boxes[box] * w;
        weight += w;
    }
    return vec4f(sum / weight, 1.0);
}

// bloom.down.frag's "down".
@fragment
fn down(@location(0) uv: vec2f) -> @location(0) vec4f {
    let corners = at(uv, -2.0, -2.0) + at(uv, 2.0, -2.0) + at(uv, -2.0, 2.0) + at(uv, 2.0, 2.0);
    let edges = at(uv, 0.0, -2.0) + at(uv, -2.0, 0.0) + at(uv, 2.0, 0.0) + at(uv, 0.0, 2.0);
    let inner = at(uv, -1.0, -1.0) + at(uv, 1.0, -1.0) + at(uv, -1.0, 1.0) + at(uv, 1.0, 1.0);
    return vec4f(at(uv, 0.0, 0.0) * 0.125 + corners * 0.03125 + edges * 0.0625 + inner * 0.125, 1.0);
}

// bloom.up.frag's "up".
@fragment
fn up(@location(0) uv: vec2f) -> @location(0) vec4f {
    let corners = at(uv, -1.0, -1.0) + at(uv, 1.0, -1.0) + at(uv, -1.0, 1.0) + at(uv, 1.0, 1.0);
    let edges = at(uv, 0.0, -1.0) + at(uv, -1.0, 0.0) + at(uv, 1.0, 0.0) + at(uv, 0.0, 1.0);
    return vec4f((at(uv, 0.0, 0.0) * 4.0 + edges * 2.0 + corners) / 16.0, 1.0);
}
