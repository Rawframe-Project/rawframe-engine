// The scene's ambient occlusion (D327), for WebGPU: the entries of
// occlusion.vert, occlusion.frag, and occlusion.blur.frag.

@group(0) @binding(0) var depth: texture_depth_2d;
@group(0) @binding(1) var surfaces: texture_2d<f32>;

struct Occlusion {
    toPoint: mat4x4f,
    viewProjection: mat4x4f,
    settings: vec4f,
}

@group(0) @binding(2) var<uniform> occlusion: Occlusion;
@group(0) @binding(3) var occluded: texture_2d<f32>;

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

const kKernel = array<vec3f, 12>(
    vec3f(0.2020, 0.0000, 0.0381),
    vec3f(-0.1584, 0.1451, 0.0569),
    vec3f(0.0207, -0.2353, 0.0818),
    vec3f(0.1613, 0.2103, 0.1150),
    vec3f(-0.2948, -0.0521, 0.1589),
    vec3f(0.2842, -0.1808, 0.2158),
    vec3f(-0.0971, 0.3612, 0.2883),
    vec3f(-0.1874, -0.3609, 0.3785),
    vec3f(0.4024, 0.1470, 0.4889),
    vec3f(-0.3968, 0.1638, 0.6218),
    vec3f(0.1658, -0.3544, 0.7795),
    vec3f(0.0789, 0.2517, 0.9646));

const kBayer = array<u32, 16>(0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);

// occlusion.frag's "occlude".
@fragment
fn occlude(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) f32 {
    let small = vec2i(position.xy);
    let at = small * 2;
    let seenDepth = textureLoad(depth, at, 0);
    if (seenDepth <= 0.0) {
        return 1.0;
    }
    let size = vec2f(textureDimensions(depth));
    let radius = occlusion.settings.x;
    let near = occlusion.settings.z;
    let here = (vec2f(at) + 0.5) / size;
    let seen = occlusion.toPoint * vec4f(here.x * 2.0 - 1.0, 1.0 - here.y * 2.0, seenDepth, 1.0);
    let point = seen.xyz / seen.w;
    let normal = normalize(textureLoad(surfaces, at, 0).xyz);
    let turn = f32(kBayer[u32(small.y & 3) * 4u + u32(small.x & 3)]) * (6.28318531 / 16.0);
    let helper = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), abs(normal.y) < 0.99);
    let across = normalize(cross(helper, normal));
    let up = cross(normal, across);
    let tangent = cos(turn) * across + sin(turn) * up;
    let bitangent = cross(normal, tangent);
    var hidden = 0.0;
    for (var i = 0; i < 12; i++) {
        let offset = kKernel[i];
        let placed = point + (tangent * offset.x + bitangent * offset.y + normal * offset.z) * radius;
        let clip = occlusion.viewProjection * vec4f(placed, 1.0);
        if (clip.w <= near) {
            continue;
        }
        let on = vec2f(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
        if (any(on < vec2f(0.0)) || any(on >= vec2f(1.0))) {
            continue;
        }
        let texel = vec2i(on * size);
        let there = textureLoad(depth, texel, 0);
        if (there <= 0.0) {
            continue;
        }
        let middle = (vec2f(texel) + 0.5) / size;
        let found = occlusion.toPoint * vec4f(middle.x * 2.0 - 1.0, 1.0 - middle.y * 2.0, there, 1.0);
        let toward = found.xyz / found.w - point;
        let far = length(toward);
        if (far > 1e-4 && far < radius) {
            let fade = 1.0 - (far * far) / (radius * radius);
            hidden += max(dot(normal, toward / far) - 0.1, 0.0) * fade;
        }
    }
    return clamp(1.0 - occlusion.settings.y * hidden / 6.0, 0.0, 1.0);
}

// occlusion.blur.frag's "blur".
@fragment
fn blur(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) f32 {
    let at = vec2i(position.xy);
    let last = vec2i(textureDimensions(occluded)) - 1;
    let centerDepth = textureLoad(depth, at * 2, 0);
    var sum = 0.0;
    var weight = 0.0;
    for (var y = -2; y < 2; y++) {
        for (var x = -2; x < 2; x++) {
            let there = clamp(at + vec2i(x, y), vec2i(0), last);
            let thereDepth = textureLoad(depth, there * 2, 0);
            if (abs(thereDepth - centerDepth) <= 0.1 * centerDepth) {
                sum += textureLoad(occluded, there, 0).r;
                weight += 1.0;
            }
        }
    }
    if (weight > 0.0) {
        return sum / weight;
    }
    return textureLoad(occluded, at, 0).r;
}
