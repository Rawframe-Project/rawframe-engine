// The scene's picture (D284), for WebGPU: the entries of tonemap.vert and
// tonemap.frag.

@group(0) @binding(0) var scene: texture_2d<f32>;

struct Grade {
    balance: array<vec4f, 3>,
    slope: vec4f,
    offset: vec4f,
    power: vec4f,
    tonemapper: vec4f,
}

@group(0) @binding(1) var<uniform> grade: Grade;

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

fn contrast(x: vec3f) -> vec3f {
    let x2 = x * x;
    let x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

fn graded(light: vec3f) -> vec3f {
    if (grade.power.w < 0.5) {
        return light;
    }
    var color = vec3f(dot(grade.balance[0].xyz, light), dot(grade.balance[1].xyz, light),
                      dot(grade.balance[2].xyz, light));
    color = pow(max(color * grade.slope.xyz + grade.offset.xyz, vec3f(0.0)), grade.power.xyz);
    let luma = dot(color, vec3f(0.2126, 0.7152, 0.0722));
    color = max(vec3f(luma) + (color - vec3f(luma)) * grade.slope.w, vec3f(0.0));
    return 0.18 * pow(color / 0.18, vec3f(grade.offset.w));
}

fn neutral(light: vec3f) -> vec3f {
    let start = 0.8 - 0.04;
    let desaturation = 0.15;
    let least = min(light.r, min(light.g, light.b));
    var offset = 0.04;
    if (least < 0.08) {
        offset = least - 6.25 * least * least;
    }
    var color = light - offset;
    let peak = max(color.r, max(color.g, color.b));
    if (peak < start) {
        return color;
    }
    let room = 1.0 - start;
    let newPeak = 1.0 - room * room / (peak + room - start);
    color *= newPeak / peak;
    let grey = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3f(newPeak), grey);
}

@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
    let inset = mat3x3f(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992,
                        0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    let outset = mat3x3f(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368,
                         1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433,
                         1.15107367264116);
    let lowest = -12.47393;
    let highest = 4.026069;
    let size = vec2i(textureDimensions(scene, 0));
    let texel = min(vec2i(uv * vec2f(size)), size - 1);
    let light = graded(textureLoad(scene, texel, 0).rgb) * grade.tonemapper.y;
    if (grade.tonemapper.x > 1.5) {
        return vec4f(clamp(light, vec3f(0.0), vec3f(1.0)), 1.0);
    }
    if (grade.tonemapper.x > 0.5) {
        return vec4f(clamp(neutral(max(light, vec3f(0.0))), vec3f(0.0), vec3f(1.0)), 1.0);
    }
    var color = inset * max(light, vec3f(1e-10));
    color = (clamp(log2(color), vec3f(lowest), vec3f(highest)) - lowest) / (highest - lowest);
    color = outset * contrast(color);
    return vec4f(pow(max(color, vec3f(0.0)), vec3f(2.2)), 1.0);
}
