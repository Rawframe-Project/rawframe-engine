// The camera's metering (D293), for WebGPU: the entries of
// meter.histogram.comp and meter.adapt.comp.

struct Meter {
    bounds: vec4f,
    fractions: vec4f,
    snap: vec4f,
}

@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var<storage, read_write> exposure: vec4f;
@group(0) @binding(2) var<storage, read_write> bins: array<atomic<u32>, 128>;
@group(0) @binding(3) var<uniform> meter: Meter;

var<workgroup> counted: array<atomic<u32>, 128>;

const lowest = -8.0;
const span = 32.0;

@compute @workgroup_size(16, 16, 1)
fn histogram(@builtin(local_invocation_index) index: u32, @builtin(global_invocation_id) invocation: vec3u) {
    if (index < 128u) {
        atomicStore(&counted[index], 0u);
    }
    workgroupBarrier();
    let size = vec2i(textureDimensions(scene, 0));
    let texel = vec2i(invocation.xy) * 4;
    if (texel.x < size.x && texel.y < size.y) {
        let color = textureLoad(scene, texel, 0).rgb;
        let luminance = dot(color, vec3f(0.2126, 0.7152, 0.0722)) / max(exposure.y, 1e-30);
        var bin = 127u;
        if (luminance <= 0.0) {
            bin = 0u;
        } else if (luminance <= 3.0e38) {
            bin = u32(clamp((log2(luminance) - lowest) / span * 127.0 + 1.0, 1.0, 127.0));
        }
        let away = length((vec2f(texel) + 0.5) / vec2f(size) * 2.0 - 1.0) * 0.70710678;
        atomicAdd(&counted[bin], 1u + u32(15.0 * meter.snap.y * (1.0 - smoothstep(0.0, 1.0, away)) + 0.5));
    }
    workgroupBarrier();
    if (index < 128u) {
        let count = atomicLoad(&counted[index]);
        if (count > 0u) {
            atomicAdd(&bins[index], count);
        }
    }
}

@compute @workgroup_size(1, 1, 1)
fn adapt() {
    var total = 0.0;
    for (var bin = 1u; bin < 128u; bin += 1u) {
        total += f32(atomicLoad(&bins[bin]));
    }
    if (total == 0.0) {
        return;
    }
    let low = meter.fractions.x * total;
    let high = meter.fractions.y * total;
    var seen = 0.0;
    var sum = 0.0;
    var weight = 0.0;
    for (var bin = 1u; bin < 128u; bin += 1u) {
        let count = f32(atomicLoad(&bins[bin]));
        let first = max(seen, low);
        let last = min(seen + count, high);
        if (last > first) {
            sum += (last - first) * (lowest + (f32(bin) - 0.5) / 127.0 * span);
            weight += last - first;
        }
        seen += count;
    }
    if (weight == 0.0) {
        return;
    }
    let wanted = clamp(sum / weight + log2(100.0 / 12.5) - meter.fractions.z, meter.bounds.x, meter.bounds.y);
    let now = exposure.x;
    let change = wanted - now;
    var moved = min(change, meter.bounds.w * meter.fractions.w);
    if (change < 0.0) {
        moved = max(change, -meter.bounds.z * meter.fractions.w);
    }
    if (meter.snap.x > 0.5) {
        moved = change;
    }
    let ev = clamp(now + moved, meter.bounds.x, meter.bounds.y);
    exposure = vec4f(ev, 1.0 / (1.2 * exp2(ev)), 0.0, 0.0);
}
