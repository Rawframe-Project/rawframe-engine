// The scene's screen-space reflections (D331), for WebGPU: the entries of
// reflect.vert and reflect.frag.

@group(0) @binding(0) var depth: texture_depth_2d;
@group(0) @binding(1) var surfaces: texture_2d<f32>;
@group(0) @binding(2) var before: texture_2d<f32>;
@group(0) @binding(3) var blended: sampler;

struct Reflections {
    toPoint: mat4x4f,
    viewProjection: mat4x4f,
    previous: mat4x4f,
    settings: vec4f,
}

@group(0) @binding(4) var<uniform> reflections: Reflections;

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

const kSteps = 32;

fn seenAt(view: mat4x4f, place: vec3f) -> vec3f {
    let clip = view * vec4f(place, 1.0);
    return vec3f(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5, clip.w);
}

fn outside(on: vec2f) -> bool {
    return any(on < vec2f(0.0)) || any(on >= vec2f(1.0));
}

// reflect.frag's "march".
@fragment
fn march(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {
    let at = vec2i(position.xy);
    let seenDepth = textureLoad(depth, at, 0);
    let surface = textureLoad(surfaces, at, 0);
    let smoothness = 1.0 - smoothstep(0.2, 0.4, surface.w);
    if (seenDepth <= 0.0 || smoothness <= 0.0) {
        return vec4f(0.0);
    }
    let size = vec2f(textureDimensions(depth));
    let reach = reflections.settings.x;
    let near = reflections.settings.y;
    let here = (vec2f(at) + 0.5) / size;
    let seen = reflections.toPoint * vec4f(here.x * 2.0 - 1.0, 1.0 - here.y * 2.0, seenDepth, 1.0);
    let point = seen.xyz / seen.w;
    let normal = normalize(surface.xyz);
    let toward = reflect(normalize(point), normal);
    let away = smoothstep(-0.2, 0.2, dot(toward, normalize(point)));
    if (away <= 0.0) {
        return vec4f(0.0);
    }
    let stepLength = reach / f32(kSteps);
    let thickness = max(stepLength * 1.5, 0.05);
    let start = point + normal * 0.02;
    var nearest = 0.0;
    var farthest = -1.0;
    for (var taken = 1; taken <= kSteps; taken++) {
        let along = stepLength * f32(taken);
        let on = seenAt(reflections.viewProjection, start + toward * along);
        if (on.z <= near || outside(on.xy)) {
            break;
        }
        let there = textureLoad(depth, vec2i(on.xy * size), 0);
        let behind = select(-1.0, on.z - near / there, there > 0.0);
        if (behind > 0.0 && behind < thickness) {
            farthest = along;
            break;
        }
        nearest = along;
    }
    if (farthest < 0.0) {
        return vec4f(0.0);
    }
    for (var halving = 0; halving < 4; halving++) {
        let middle = (nearest + farthest) * 0.5;
        let on = seenAt(reflections.viewProjection, start + toward * middle);
        let there = textureLoad(depth, vec2i(clamp(on.xy, vec2f(0.0), vec2f(0.999)) * size), 0);
        if (there > 0.0 && on.z > near / there) {
            farthest = middle;
        } else {
            nearest = middle;
        }
    }
    let met = start + toward * farthest;
    let then = seenAt(reflections.previous, met);
    if (then.z <= 0.0 || outside(then.xy)) {
        return vec4f(0.0);
    }
    let onNow = seenAt(reflections.viewProjection, met);
    let fromEdge = min(onNow.xy, 1.0 - onNow.xy);
    let edge = smoothstep(0.0, 0.1, min(fromEdge.x, fromEdge.y));
    let fading = 1.0 - smoothstep(0.7, 1.0, farthest / reach);
    let light = textureSampleLevel(before, blended, then.xy, 0.0).rgb;
    return vec4f(light, smoothness * away * edge * fading);
}
