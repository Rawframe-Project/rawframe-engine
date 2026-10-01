// The scene's contact shadows (D338), for WebGPU: the entries of
// contact.vert and contact.frag.

@group(0) @binding(0) var depth: texture_depth_2d;

struct Contact {
    toPoint: mat4x4f,
    viewProjection: mat4x4f,
    toSun: vec4f,
    settings: vec4f,
}

@group(0) @binding(1) var<uniform> contact: Contact;

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

const kSteps = 16;

fn seenAt(place: vec3f) -> vec3f {
    let clip = contact.viewProjection * vec4f(place, 1.0);
    return vec3f(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5, clip.w);
}

// contact.frag's "shade".
@fragment
fn shade(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) f32 {
    let at = vec2i(position.xy);
    let seenDepth = textureLoad(depth, at, 0);
    if (seenDepth <= 0.0) {
        return 1.0;
    }
    let size = vec2f(textureDimensions(depth, 0));
    let here = (vec2f(at) + 0.5) / size;
    let seen = contact.toPoint * vec4f(here.x * 2.0 - 1.0, 1.0 - here.y * 2.0, seenDepth, 1.0);
    let point = seen.xyz / seen.w;
    let reach = contact.settings.x;
    let near = contact.settings.y;
    let stepLength = reach / f32(kSteps);
    let thickness = reach;
    let jitter = fract(52.9829189 * fract(dot(position.xy, vec2f(0.06711056, 0.00583715))));
    for (var taken = 0; taken < kSteps; taken++) {
        let along = stepLength * (f32(taken) + 0.5 + jitter);
        let on = seenAt(point + contact.toSun.xyz * along);
        if (on.z <= near || any(on.xy < vec2f(0.0)) || any(on.xy >= vec2f(1.0))) {
            return 1.0;
        }
        let there = textureLoad(depth, vec2i(on.xy * size), 0);
        if (there <= 0.0) {
            continue;
        }
        let behind = on.z - near / there;
        if (behind > 0.002 * on.z && behind < thickness) {
            return smoothstep(0.5, 1.0, along / reach);
        }
    }
    return 1.0;
}
