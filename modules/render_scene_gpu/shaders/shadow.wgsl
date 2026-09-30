// The sun's shadow map (D289), for WebGPU: the entries of shadow.vert, and
// of shadow.cut.vert and shadow.cut.frag for masked casters (D310).

struct Cascade {
    viewProjection: mat4x4f,
}

@group(0) @binding(0) var<uniform> cascade: Cascade;
@group(0) @binding(1) var<storage, read> materials: array<vec4f>;
@group(0) @binding(2) var baseTexture: texture_2d<f32>;
@group(0) @binding(3) var baseSampler: sampler;

@vertex
fn vs(@location(0) position: vec3f, @location(2) model0: vec4f, @location(3) model1: vec4f,
      @location(4) model2: vec4f) -> @builtin(position) vec4f {
    let vertex = vec4f(position, 1.0);
    let placed = vec3f(dot(model0, vertex), dot(model1, vertex), dot(model2, vertex));
    return cascade.viewProjection * vec4f(placed, 1.0);
}

struct Cut {
    @builtin(position) position: vec4f,
    @location(1) color: vec4f,
    @location(5) @interpolate(flat) material: u32,
    @location(6) uv: vec2f,
}

@vertex
fn vsCut(@location(0) position: vec3f, @location(2) model0: vec4f, @location(3) model1: vec4f,
         @location(4) model2: vec4f, @location(8) color: vec4f, @location(12) material: f32,
         @location(13) uv: vec2f) -> Cut {
    let vertex = vec4f(position, 1.0);
    let placed = vec3f(dot(model0, vertex), dot(model1, vertex), dot(model2, vertex));
    var out: Cut;
    out.position = cascade.viewProjection * vec4f(placed, 1.0);
    out.color = color;
    out.material = u32(material);
    out.uv = uv;
    return out;
}

@fragment
fn cut(@location(1) color: vec4f, @location(5) @interpolate(flat) material: u32, @location(6) uv: vec2f) {
    let at = min(material, arrayLength(&materials) / 8u - 1u) * 8u;
    let rest = materials[at + 3u];
    let mapped = materials[at + 4u];
    let sampled = textureSample(baseTexture, baseSampler, uv * mapped.xy + mapped.zw);
    let opacity = rest.x * color.a * select(1.0, sampled.a, (u32(rest.w) & 4u) != 0u);
    if (opacity < rest.z) {
        discard;
    }
}
