// The sun's shadow map (D289), for WebGPU: the entry of shadow.vert.

struct Cascade {
    viewProjection: mat4x4f,
}

@group(0) @binding(0) var<uniform> cascade: Cascade;

@vertex
fn vs(@location(0) position: vec3f, @location(2) model0: vec4f, @location(3) model1: vec4f,
      @location(4) model2: vec4f) -> @builtin(position) vec4f {
    let vertex = vec4f(position, 1.0);
    let placed = vec3f(dot(model0, vertex), dot(model1, vertex), dot(model2, vertex));
    return cascade.viewProjection * vec4f(placed, 1.0);
}
