// The scene's FXAA (D296), for WebGPU: the entries of fxaa.vert and
// fxaa.frag.

@group(0) @binding(0) var picture: texture_2d<f32>;
@group(0) @binding(1) var filtered: sampler;

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

const kEdgeThreshold = 0.166;
const kEdgeThresholdMin = 0.0833;
const kReduceMin = 1.0 / 128.0;
const kReduceMul = 1.0 / 8.0;
const kSpanMax = 8.0;

fn at(uv: vec2f) -> vec3f {
    return textureSampleLevel(picture, filtered, uv, 0.0).rgb;
}

fn lumaOf(color: vec3f) -> f32 {
    return sqrt(dot(color, vec3f(0.2126, 0.7152, 0.0722)));
}

@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
    let texel = 1.0 / vec2f(textureDimensions(picture, 0));
    let middle = at(uv);
    let nw = lumaOf(at(uv + vec2f(-1.0, -1.0) * texel));
    let ne = lumaOf(at(uv + vec2f(1.0, -1.0) * texel));
    let sw = lumaOf(at(uv + vec2f(-1.0, 1.0) * texel));
    let se = lumaOf(at(uv + vec2f(1.0, 1.0) * texel));
    let m = lumaOf(middle);
    let lowest = min(m, min(min(nw, ne), min(sw, se)));
    let highest = max(m, max(max(nw, ne), max(sw, se)));
    if (highest - lowest < max(kEdgeThresholdMin, highest * kEdgeThreshold)) {
        return vec4f(middle, 1.0);
    }
    var direction = vec2f(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    let reduce = max((nw + ne + sw + se) * 0.25 * kReduceMul, kReduceMin);
    let scale = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * scale, vec2f(-kSpanMax), vec2f(kSpanMax)) * texel;
    let narrow = 0.5 * (at(uv + direction * (1.0 / 3.0 - 0.5)) + at(uv + direction * (2.0 / 3.0 - 0.5)));
    let wide = narrow * 0.5 + 0.25 * (at(uv - direction * 0.5) + at(uv + direction * 0.5));
    let wideLuma = lumaOf(wide);
    return vec4f(select(wide, narrow, wideLuma < lowest || wideLuma > highest), 1.0);
}
