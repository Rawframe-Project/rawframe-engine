// The UI's boxes and images (SPEC-0032, D375, D377, D378), for WebGPU:
// the entries of ui.vert, ui.frag, ui.image.vert, and ui.image.frag.

struct View {
    size: vec4f,
}

struct Box {
    rect: vec4f,
    radii: vec4f,
    fill: vec4f,
    widths: vec4f,
    borders: array<vec4f, 4>,
    clip: vec4f,
}

struct Clip {
    rect: vec4f,
    radii: vec4f,
    link: vec4f,
}

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> boxes: array<Box>;
@group(0) @binding(2) var<storage, read> clips: array<Clip>;

struct Image {
    rect: vec4f,
    uv: vec4f,
    slice: vec4f,
    tint: vec4f,
    clip: vec4f,
}

@group(0) @binding(3) var<storage, read> images: array<Image>;
@group(0) @binding(4) var picture: texture_2d<f32>;
@group(0) @binding(5) var pictureSampler: sampler;

const kDeepestClip = 64;

struct Corner {
    @builtin(position) position: vec4f,
    @location(0) pixel: vec2f,
    @location(1) @interpolate(flat) box: u32,
}

@vertex
fn vs(@builtin(vertex_index) index: u32, @builtin(instance_index) instance: u32) -> Corner {
    var corners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(0.0, 1.0),
                                  vec2f(1.0, 0.0), vec2f(1.0, 1.0));
    let rect = boxes[instance].rect;
    let pixel = rect.xy - vec2f(1.0) + corners[index] * (rect.zw + vec2f(2.0));
    var out: Corner;
    out.position = vec4f(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    out.pixel = pixel;
    out.box = instance;
    return out;
}

fn distanceTo(pixel: vec2f, rect: vec4f, radii: vec4f) -> f32 {
    let halfSize = rect.zw * 0.5;
    let at = pixel - (rect.xy + halfSize);
    var radius = radii.y;
    if (at.x < 0.0) {
        radius = select(radii.w, radii.x, at.y < 0.0);
    } else {
        radius = select(radii.z, radii.y, at.y < 0.0);
    }
    let past = abs(at) - halfSize + vec2f(radius);
    return min(max(past.x, past.y), 0.0) + length(max(past, vec2f(0.0))) - radius;
}

fn coverage(distance: f32) -> f32 {
    return clamp(0.5 - distance, 0.0, 1.0);
}

@fragment
fn fs(@location(0) pixel: vec2f, @location(1) @interpolate(flat) index: u32) -> @location(0) vec4f {
    let box = boxes[index];
    let outer = coverage(distanceTo(pixel, box.rect, box.radii));
    let widths = box.widths;
    let inner = vec4f(box.rect.x + widths.w, box.rect.y + widths.x, box.rect.z - widths.w - widths.y,
                      box.rect.w - widths.x - widths.z);
    let innerRadii = max(box.radii - vec4f(max(widths.w, widths.x), max(widths.x, widths.y),
                                           max(widths.y, widths.z), max(widths.z, widths.w)), vec4f(0.0));
    var filled = 0.0;
    if (inner.z > 0.0 && inner.w > 0.0) {
        filled = coverage(distanceTo(pixel, inner, innerRadii));
    }
    let reach = vec4f(pixel.y - box.rect.y, box.rect.x + box.rect.z - pixel.x, box.rect.y + box.rect.w - pixel.y,
                      pixel.x - box.rect.x) / max(widths, vec4f(1e-4));
    var side = 0;
    for (var at = 1; at < 4; at++) {
        if (reach[at] < reach[side]) {
            side = at;
        }
    }
    var color = mix(box.borders[side], box.fill, filled) * outer;
    var clip = i32(box.clip.x);
    for (var depth = 0; depth < kDeepestClip && clip > 0; depth++) {
        let kept = coverage(distanceTo(pixel, clips[clip].rect, clips[clip].radii));
        color *= select(kept, 1.0 - kept, clips[clip].link.y > 0.5);
        clip = i32(clips[clip].link.x);
    }
    return color;
}

struct Shown {
    @builtin(position) position: vec4f,
    @location(0) pixel: vec2f,
    @location(1) @interpolate(flat) image: u32,
}

@vertex
fn imageVs(@builtin(vertex_index) index: u32, @builtin(instance_index) instance: u32) -> Shown {
    var corners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(0.0, 1.0),
                                  vec2f(1.0, 0.0), vec2f(1.0, 1.0));
    let rect = images[instance].rect;
    let pixel = rect.xy + corners[index] * rect.zw;
    var out: Shown;
    out.position = vec4f(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    out.pixel = pixel;
    out.image = instance;
    return out;
}

fn sliced(along: f32, extent: f32, size: f32, before: f32, after: f32, scale: f32) -> f32 {
    var first = before * scale;
    var last = after * scale;
    if (first + last <= 0.0) {
        return along / max(extent, 1e-4) * size;
    }
    let shrink = min(1.0, extent / (first + last));
    first *= shrink;
    last *= shrink;
    if (along < first) {
        return along / first * before;
    }
    if (along > extent - last) {
        return size - (extent - along) / max(last, 1e-4) * after;
    }
    return before + (along - first) / max(extent - first - last, 1e-4) * (size - before - after);
}

@fragment
fn imageFs(@location(0) pixel: vec2f, @location(1) @interpolate(flat) index: u32) -> @location(0) vec4f {
    let image = images[index];
    let size = max(image.clip.yz, vec2f(1.0));
    let texel = vec2f(sliced(pixel.x - image.rect.x, image.rect.z, size.x, image.slice.w, image.slice.y, image.clip.w),
                      sliced(pixel.y - image.rect.y, image.rect.w, size.y, image.slice.x, image.slice.z, image.clip.w));
    let at = image.uv.xy + texel / size * image.uv.zw;
    let sampled = textureSample(picture, pictureSampler, at);
    var color = vec4f(sampled.rgb * sampled.a, sampled.a) * image.tint;
    var clip = i32(image.clip.x);
    for (var depth = 0; depth < kDeepestClip && clip > 0; depth++) {
        let kept = coverage(distanceTo(pixel, clips[clip].rect, clips[clip].radii));
        color *= select(kept, 1.0 - kept, clips[clip].link.y > 0.5);
        clip = i32(clips[clip].link.x);
    }
    return color;
}
