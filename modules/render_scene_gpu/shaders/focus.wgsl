// The scene's depth of field (D336), for WebGPU: the entries of
// focus.vert, focus.prefilter.frag, focus.bokeh.frag, and
// focus.combine.frag.

@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var depth: texture_depth_2d;
@group(0) @binding(2) var halved: texture_2d<f32>;
@group(0) @binding(3) var blended: sampler;

struct Lens {
    settings: vec4f,
}

@group(0) @binding(4) var<uniform> lens: Lens;
@group(0) @binding(5) var circles: texture_2d<f32>;

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

// The circle of confusion's radius in full-size pixels, signed, for a
// point `ahead` meters away.
fn circleAt(ahead: f32) -> f32 {
    return lens.settings.x * (ahead - lens.settings.y) / ahead;
}

// focus.prefilter.frag's "prefilter".
@fragment
fn prefilter(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {
    let size = vec2i(textureDimensions(scene, 0));
    let first = vec2i(position.xy) * 2;
    var light = vec3f(0.0);
    var nearest = 0.0;
    for (var y = 0; y < 2; y++) {
        for (var x = 0; x < 2; x++) {
            let at = min(first + vec2i(x, y), size - 1);
            light += textureLoad(scene, at, 0).rgb;
            nearest = max(nearest, textureLoad(depth, at, 0));
        }
    }
    let ahead = lens.settings.z / max(nearest, 1e-7);
    return vec4f(light * 0.25, clamp(circleAt(ahead), -lens.settings.w, lens.settings.w) * 0.5);
}

const kGoldenAngle = 2.39996323;

// focus.bokeh.frag's "bokeh".
@fragment
fn bokeh(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {
    let size = vec2f(textureDimensions(halved, 0));
    let here = textureLoad(halved, vec2i(position.xy), 0);
    let own = abs(here.a);
    let longest = lens.settings.w * 0.5;
    var light = here.rgb;
    var taps = 1.0;
    var reach = own;
    var radius = 1.0;
    var angle = 0.0;
    for (var tap = 0; tap < 512 && radius < longest; tap++) {
        let at = (position.xy + vec2f(cos(angle), sin(angle)) * radius) / size;
        let there = textureSampleLevel(halved, blended, at, 0.0);
        let behind = there.a > here.a;
        let circle = select(abs(there.a), min(abs(there.a), own * 2.0), behind);
        let taken = smoothstep(radius - 0.5, radius + 0.5, circle);
        light += mix(light / taps, there.rgb, taken);
        taps += 1.0;
        if (!behind) {
            reach = max(reach, taken * circle);
        }
        radius += 1.0 / radius;
        angle += kGoldenAngle;
    }
    return vec4f(light / taps, reach);
}

// focus.combine.frag's "combine".
@fragment
fn combine(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {
    let at = vec2i(position.xy);
    let sharp = textureLoad(scene, at, 0);
    let ahead = lens.settings.z / max(textureLoad(depth, at, 0), 1e-7);
    let circle = clamp(circleAt(ahead), -lens.settings.w, lens.settings.w) * 0.5;
    let size = vec2i(textureDimensions(halved, 0));
    let across = position.xy * 0.5 - 0.5;
    let first = vec2i(floor(across));
    let fraction = across - vec2f(first);
    var blurred = vec4f(0.0);
    var weight = 0.0;
    for (var y = 0; y < 2; y++) {
        for (var x = 0; x < 2; x++) {
            let texel = clamp(first + vec2i(x, y), vec2i(0), size - 1);
            let nearness = mix(1.0 - fraction, fraction, vec2f(f32(x), f32(y)));
            let alike = 1.0 / (0.05 + abs(textureLoad(circles, texel, 0).a - circle));
            let share = nearness.x * nearness.y * alike + 1e-6;
            blurred += textureLoad(halved, texel, 0) * share;
            weight += share;
        }
    }
    blurred /= weight;
    let blend = smoothstep(0.5, 1.5, max(abs(circle) * 2.0, blurred.a * 2.0));
    return vec4f(mix(sharp.rgb, blurred.rgb, blend), sharp.a);
}
