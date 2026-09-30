// The 3D scene's models (D284), for WebGPU: the entries of scene.vert and
// scene.frag.

struct Frame {
    viewProjection: mat4x4f,
    toSun: vec4f,
    sun: vec4f,
    sky: vec4f,
    exposure: vec4f,
}

@group(0) @binding(0) var<uniform> frame: Frame;

struct Placed {
    @invariant @builtin(position) position: vec4f,
    @location(0) normal: vec3f,
    @location(1) color: vec4f,
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
    return out;
}

const kPi = 3.14159265;

@fragment
fn fs(@location(0) normal: vec3f, @location(1) color: vec4f) -> @location(0) vec4f {
    let n = normalize(normal);
    let facing = max(dot(n, frame.toSun.xyz), 0.0);
    let light = frame.sun.rgb * (facing / kPi) + frame.sky.rgb * (0.5 + 0.5 * n.y);
    return vec4f(color.rgb * light * frame.exposure.x, 1.0);
}
