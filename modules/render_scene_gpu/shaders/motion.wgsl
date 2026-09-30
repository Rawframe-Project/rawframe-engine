// The scene's motion blur (D334), for WebGPU: the entries of motion.vert,
// motion.tile.frag, motion.neighbor.frag, and motion.gather.frag.

@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var motion: texture_2d<f32>;
@group(0) @binding(2) var depth: texture_depth_2d;
@group(0) @binding(3) var tiles: texture_2d<f32>;

struct Blur {
    settings: vec4f,
}

@group(0) @binding(4) var<uniform> blur: Blur;

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

fn clamped(value: vec2f) -> vec2f {
    let reach = length(value);
    return select(value, value * (blur.settings.y / reach), reach > blur.settings.y);
}

// motion.tile.frag's "tile".
@fragment
fn tile(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) vec2f {
    let side = i32(blur.settings.y);
    let size = vec2i(textureDimensions(motion, 0));
    let first = vec2i(position.xy) * side;
    var longest = vec2f(0.0);
    for (var y = 0; y < side; y++) {
        for (var x = 0; x < side; x++) {
            let at = min(first + vec2i(x, y), size - 1);
            let stretch = textureLoad(motion, at, 0).xy * vec2f(size) * blur.settings.x;
            if (dot(stretch, stretch) > dot(longest, longest)) {
                longest = stretch;
            }
        }
    }
    return clamped(longest);
}

// motion.neighbor.frag's "neighbor".
@fragment
fn neighbor(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) vec2f {
    let size = vec2i(textureDimensions(tiles, 0));
    let at = vec2i(position.xy);
    var longest = vec2f(0.0);
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            let stretch = textureLoad(tiles, clamp(at + vec2i(x, y), vec2i(0), size - 1), 0).xy;
            if (dot(stretch, stretch) > dot(longest, longest)) {
                longest = stretch;
            }
        }
    }
    return longest;
}

const kTaps = 15;

fn stretchAt(at: vec2i, size: vec2f) -> vec2f {
    return clamped(textureLoad(motion, at, 0).xy * size * blur.settings.x);
}

fn aheadAt(at: vec2i) -> f32 {
    return blur.settings.z / max(textureLoad(depth, at, 0), 1e-7);
}

fn inFront(nearer: f32, farther: f32) -> f32 {
    return clamp(1.0 - (nearer - farther) / (0.02 * min(nearer, farther)), 0.0, 1.0);
}

fn cone(apart: f32, reach: f32) -> f32 {
    return clamp(1.0 - apart / reach, 0.0, 1.0);
}

fn cylinder(apart: f32, reach: f32) -> f32 {
    return 1.0 - smoothstep(0.95 * reach, 1.05 * reach, apart);
}

// motion.gather.frag's "gather".
@fragment
fn gather(@builtin(position) position: vec4f, @location(0) uv: vec2f) -> @location(0) vec4f {
    let at = vec2i(position.xy);
    let here = textureLoad(scene, at, 0);
    let side = i32(blur.settings.y);
    let longest = textureLoad(tiles, at / side, 0).xy;
    if (dot(longest, longest) <= 0.25) {
        return here;
    }
    let size = vec2i(textureDimensions(scene, 0));
    let sizeF = vec2f(size);
    let reach = max(length(stretchAt(at, sizeF)), 0.5);
    let ahead = aheadAt(at);
    let jitter = fract(52.9829189 * fract(dot(position.xy, vec2f(0.06711056, 0.00583715)))) - 0.5;
    var weight = 1.0 / reach;
    var sum = here * weight;
    for (var taken = 0; taken < kTaps; taken++) {
        if (taken == kTaps / 2) {
            continue;
        }
        let along = mix(-1.0, 1.0, (f32(taken) + jitter + 1.0) / f32(kTaps + 1));
        let there = clamp(vec2i(floor(position.xy + longest * along)), vec2i(0), size - 1);
        let apart = length(longest * along);
        let thereAhead = aheadAt(there);
        let thereReach = max(length(stretchAt(there, sizeF)), 0.5);
        let share = inFront(thereAhead, ahead) * cone(apart, thereReach) +
                    inFront(ahead, thereAhead) * cone(apart, reach) +
                    cylinder(apart, thereReach) * cylinder(apart, reach) * 2.0;
        weight += share;
        sum += textureLoad(scene, there, 0) * share;
    }
    return sum / weight;
}
