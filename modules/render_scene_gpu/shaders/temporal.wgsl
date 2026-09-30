// The scene's temporal anti-aliasing (D291), for WebGPU: the entries of
// temporal.vert and temporal.frag.

@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var history: texture_2d<f32>;
@group(0) @binding(2) var motion: texture_2d<f32>;
@group(0) @binding(3) var blended: sampler;

struct Temporal {
    state: vec4f,
}

@group(0) @binding(4) var<uniform> temporal: Temporal;

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

fn ycocgOf(color: vec3f) -> vec3f {
    return vec3f(0.25 * color.r + 0.5 * color.g + 0.25 * color.b,
                 0.5 * color.r - 0.5 * color.b,
                 -0.25 * color.r + 0.5 * color.g - 0.25 * color.b);
}

fn rgbOf(color: vec3f) -> vec3f {
    return vec3f(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z);
}

fn lumaOf(color: vec3f) -> f32 {
    return dot(color, vec3f(0.2126, 0.7152, 0.0722));
}

@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
    let size = vec2i(textureDimensions(scene, 0));
    let texel = min(vec2i(uv * vec2f(size)), size - 1);
    let now = textureLoad(scene, texel, 0).rgb;
    var lowest = ycocgOf(now);
    var highest = lowest;
    for (var y = -1; y <= 1; y += 1) {
        for (var x = -1; x <= 1; x += 1) {
            let around = ycocgOf(textureLoad(scene, clamp(texel + vec2i(x, y), vec2i(0), size - 1), 0).rgb);
            lowest = min(lowest, around);
            highest = max(highest, around);
        }
    }
    let was = (vec2f(texel) + 0.5) / vec2f(size) - textureLoad(motion, texel, 0).xy;
    let before = textureSampleLevel(history, blended, was, 0.0).rgb;
    if (temporal.state.x == 0.0 || any(was < vec2f(0.0)) || any(was > vec2f(1.0))) {
        return vec4f(now, 1.0);
    }
    let kept = rgbOf(clamp(ycocgOf(before), lowest, highest));
    let nowWeight = 0.1 / (1.0 + lumaOf(now));
    let keptWeight = 0.9 / (1.0 + lumaOf(kept));
    return vec4f((now * nowWeight + kept * keptWeight) / (nowWeight + keptWeight), 1.0);
}
