// The canvas's sprites (D278), for WebGPU: the entries of sprite.vert and
// sprite.frag.

@group(0) @binding(0) var spriteTexture: texture_2d<f32>;
@group(0) @binding(1) var spriteSampler: sampler;

struct Corner {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
}

// The color's channels are written in sRGB (ADR-0047): blending happens in
// linear light, so they are decoded here.
fn linearOf(encoded: vec3f) -> vec3f {
    return select(pow((encoded + 0.055) / 1.055, vec3f(2.4)), encoded / 12.92, encoded < vec3f(0.04045));
}

@vertex
fn vs(@location(0) position: vec2f, @location(1) uv: vec2f, @location(2) color: vec4f) -> Corner {
    var corner: Corner;
    corner.position = vec4f(position, 0.0, 1.0);
    corner.uv = uv;
    let ordered = color.wzyx;
    corner.color = vec4f(linearOf(ordered.rgb), ordered.a);
    return corner;
}

@fragment
fn fs(@location(0) uv: vec2f, @location(1) color: vec4f) -> @location(0) vec4f {
    return textureSample(spriteTexture, spriteSampler, uv) * color;
}
