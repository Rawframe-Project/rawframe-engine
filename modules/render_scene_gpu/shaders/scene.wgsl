// The 3D scene's models (D284, D289), for WebGPU: the entries of
// scene.vert and scene.frag.

struct Frame {
    viewProjection: mat4x4f,
    toSun: vec4f,
    sun: vec4f,
    sky: vec4f,
    exposure: vec4f,
    forward: vec4f,
    cascadeFar: vec4f,
    cascadeTexel: vec4f,
    shadow: vec4f,
    cascades: array<mat4x4f, 4>,
}

@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var shadowMap: texture_depth_2d;
@group(0) @binding(2) var shadowSampler: sampler_comparison;

struct Placed {
    @invariant @builtin(position) position: vec4f,
    @location(0) normal: vec3f,
    @location(1) color: vec4f,
    @location(2) placed: vec3f,
}

@vertex
fn vs(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) model0: vec4f,
      @location(3) model1: vec4f, @location(4) model2: vec4f, @location(5) normal0: vec3f,
      @location(6) normal1: vec3f, @location(7) normal2: vec3f, @location(8) color: vec4f) -> Placed {
    let vertex = vec4f(position, 1.0);
    let placed = vec3f(dot(model0, vertex), dot(model1, vertex), dot(model2, vertex));
    var out: Placed;
    out.position = frame.viewProjection * vec4f(placed, 1.0);
    out.normal = mat3x3f(normal0, normal1, normal2) * normal;
    out.color = color;
    out.placed = placed;
    return out;
}

const kPi = 3.14159265;

fn sunlit(placed: vec3f, normal: vec3f) -> f32 {
    let count = i32(frame.shadow.x);
    let ahead = dot(placed, frame.forward.xyz);
    if (count == 0 || ahead > frame.shadow.y) {
        return 1.0;
    }
    var at = 0;
    while (at < count - 1 && ahead > frame.cascadeFar[at]) {
        at += 1;
    }
    let clip = frame.cascades[at] * vec4f(placed + normal * (frame.cascadeTexel[at] * 1.5), 1.0);
    let halfTexel = 0.5 / frame.shadow.z;
    let inSquare = clamp(vec2f(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5), vec2f(halfTexel), vec2f(1.0 - halfTexel));
    let inMap = (inSquare + vec2f(f32(at % 2), f32(at / 2))) * 0.5;
    let lit = textureSampleCompareLevel(shadowMap, shadowSampler, inMap, clip.z);
    let fade = clamp((frame.shadow.y - ahead) / (0.1 * frame.shadow.y), 0.0, 1.0);
    return mix(1.0, lit, fade);
}

@fragment
fn fs(@location(0) normal: vec3f, @location(1) color: vec4f, @location(2) placed: vec3f) -> @location(0) vec4f {
    let n = normalize(normal);
    let facing = max(dot(n, frame.toSun.xyz), 0.0) * sunlit(placed, n);
    let light = frame.sun.rgb * (facing / kPi) + frame.sky.rgb * (0.5 + 0.5 * n.y);
    return vec4f(color.rgb * light * frame.exposure.x, 1.0);
}
