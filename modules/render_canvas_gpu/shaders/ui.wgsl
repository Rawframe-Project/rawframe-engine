// The UI's boxes, images, shadows, and gradients (SPEC-0032, D375, D377,
// D378, D381, D382), for WebGPU: the entries of ui.vert, ui.frag, ui.image.vert,
// ui.image.frag, ui.shadow.vert, and ui.shadow.frag.

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

struct Glyph {
    rect: vec4f,
    atlas: vec4f,
    color: vec4f,
    clip: vec4f,
}

@group(0) @binding(8) var<storage, read> glyphs: array<Glyph>;

struct Shadow {
    rect: vec4f,
    radii: vec4f,
    color: vec4f,
    shape: vec4f,
    flags: vec4f,
}

@group(0) @binding(6) var<storage, read> shadows: array<Shadow>;

struct Gradient {
    head: vec4f,
    colors: array<vec4f, 4>,
    positions: vec4f,
}

@group(0) @binding(7) var<storage, read> gradients: array<Gradient>;

fn toOklab(color: vec3f) -> vec3f {
    let lms = mat3x3f(0.4122214708, 0.2119034982, 0.0883024619,
                      0.5363325363, 0.6806995451, 0.2817188376,
                      0.0514459929, 0.1073969566, 0.6299787005) * color;
    let root = sign(lms) * pow(abs(lms), vec3f(1.0 / 3.0));
    return mat3x3f(0.2104542553, 1.9779984951, 0.0259040371,
                   0.7936177850, -2.4285922050, 0.7827717662,
                   -0.0040720468, 0.4505937099, -0.8086757660) * root;
}

fn fromOklab(lab: vec3f) -> vec3f {
    let lms = mat3x3f(1.0, 1.0, 1.0,
                      0.3963377774, -0.1055613458, -0.0894841775,
                      0.2158037573, -0.0638541728, -1.2914855480) * lab;
    return mat3x3f(4.0767416621, -1.2684380046, -0.0041960863,
                   -3.3077115913, 2.6097574011, -0.7034186147,
                   0.2309699292, -0.3413193965, 1.7076147010) * (lms * lms * lms);
}

fn premultipliedOklab(color: vec4f) -> vec4f {
    if (color.a <= 0.0) {
        return vec4f(0.0);
    }
    return vec4f(toOklab(color.rgb / color.a) * color.a, color.a);
}

fn ramp(index: u32, pixel: vec2f, rect: vec4f) -> vec4f {
    let head = gradients[index].head;
    let halfSize = rect.zw * 0.5;
    let at = pixel - (rect.xy + halfSize);
    var along = 0.0;
    if (head.x < 1.5) {
        let angle = radians(head.z);
        let toward = vec2f(sin(angle), -cos(angle));
        let span = abs(rect.z * toward.x) + abs(rect.w * toward.y);
        along = dot(at, toward) / max(span, 1e-4) + 0.5;
    } else {
        along = length(at / max(halfSize * 1.41421356, vec2f(1e-4)));
    }
    let stops = i32(head.y);
    let positions = gradients[index].positions;
    var lab = premultipliedOklab(gradients[index].colors[0]);
    for (var stop = 1; stop < stops; stop++) {
        if (along > positions[stop - 1]) {
            let span = max(positions[stop] - positions[stop - 1], 1e-6);
            let share = clamp((along - positions[stop - 1]) / span, 0.0, 1.0);
            lab = mix(premultipliedOklab(gradients[index].colors[stop - 1]),
                      premultipliedOklab(gradients[index].colors[stop]), share);
        }
    }
    if (lab.a <= 0.0) {
        return vec4f(0.0);
    }
    return vec4f(max(fromOklab(lab.rgb / lab.a), vec3f(0.0)) * lab.a, lab.a);
}

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
    var fill = box.fill;
    let gradient = u32(box.clip.y);
    if (gradient > 0u) {
        let over = ramp(gradient, pixel, box.rect);
        fill = over + fill * (1.0 - over.a);
    }
    var color = mix(box.borders[side], fill, filled) * outer;
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

struct Inked {
    @builtin(position) position: vec4f,
    @location(0) pixel: vec2f,
    @location(1) @interpolate(flat) glyph: u32,
}

@vertex
fn glyphVs(@builtin(vertex_index) index: u32, @builtin(instance_index) instance: u32) -> Inked {
    var corners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(0.0, 1.0),
                                  vec2f(1.0, 0.0), vec2f(1.0, 1.0));
    let rect = glyphs[instance].rect;
    let pixel = rect.xy + corners[index] * rect.zw;
    var out: Inked;
    out.position = vec4f(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    out.pixel = pixel;
    out.glyph = instance;
    return out;
}

@fragment
fn glyphFs(@location(0) pixel: vec2f, @location(1) @interpolate(flat) index: u32) -> @location(0) vec4f {
    let glyph = glyphs[index];
    let within = clamp(floor(pixel - glyph.rect.xy), vec2f(0.0), max(glyph.atlas.zw - 1.0, vec2f(0.0)));
    let covered = textureLoad(picture, vec2i(glyph.atlas.xy + within), 0).r;
    var color = glyph.color * covered;
    var clip = i32(glyph.clip.x);
    for (var depth = 0; depth < kDeepestClip && clip > 0; depth++) {
        let kept = coverage(distanceTo(pixel, clips[clip].rect, clips[clip].radii));
        color *= select(kept, 1.0 - kept, clips[clip].link.y > 0.5);
        clip = i32(clips[clip].link.x);
    }
    return color;
}

struct Cast {
    @builtin(position) position: vec4f,
    @location(0) pixel: vec2f,
    @location(1) @interpolate(flat) shadow: u32,
}

@vertex
fn shadowVs(@builtin(vertex_index) index: u32, @builtin(instance_index) instance: u32) -> Cast {
    var corners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(0.0, 1.0),
                                  vec2f(1.0, 0.0), vec2f(1.0, 1.0));
    var rect = shadows[instance].rect;
    let shape = shadows[instance].shape;
    if (shadows[instance].flags.x < 0.5) {
        let reach = max(shape.w, 0.0) + 1.5 * shape.z + 1.0;
        rect = vec4f(rect.xy + shape.xy - vec2f(reach), rect.zw + vec2f(2.0 * reach));
    }
    let pixel = rect.xy + corners[index] * rect.zw;
    var out: Cast;
    out.position = vec4f(pixel.x / view.size.x * 2.0 - 1.0, 1.0 - pixel.y / view.size.y * 2.0, 0.0, 1.0);
    out.pixel = pixel;
    out.shadow = instance;
    return out;
}

fn errorFunction(value: vec2f) -> vec2f {
    let signs = sign(value);
    let magnitude = abs(value);
    var x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (magnitude * magnitude)) * magnitude) * magnitude;
    x *= x;
    return signs - signs / (x * x);
}

fn gaussian(x: f32, sigma: f32) -> f32 {
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (2.5066283 * sigma);
}

fn across(x: f32, y: f32, sigma: f32, corner: f32, halfSize: vec2f) -> f32 {
    let delta = min(halfSize.y - corner - abs(y), 0.0);
    let curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    let integral = 0.5 + 0.5 * errorFunction((x + vec2f(-curved, curved)) * (0.70710678 / sigma));
    return integral.y - integral.x;
}

fn blurred(pixel: vec2f, rect: vec4f, radii: vec4f, sigma: f32) -> f32 {
    if (sigma < 0.25) {
        return coverage(distanceTo(pixel, rect, radii));
    }
    let halfSize = rect.zw * 0.5;
    let at = pixel - (rect.xy + halfSize);
    var corner = radii.y;
    if (at.x < 0.0) {
        corner = select(radii.w, radii.x, at.y < 0.0);
    } else {
        corner = select(radii.z, radii.y, at.y < 0.0);
    }
    corner = min(corner, min(halfSize.x, halfSize.y));
    let low = at.y - halfSize.y;
    let high = at.y + halfSize.y;
    let start = clamp(-3.0 * sigma, low, high);
    let end = clamp(3.0 * sigma, low, high);
    let step = (end - start) / 4.0;
    var y = start + step * 0.5;
    var value = 0.0;
    for (var row = 0; row < 4; row++) {
        value += across(at.x, at.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

@fragment
fn shadowFs(@location(0) pixel: vec2f, @location(1) @interpolate(flat) index: u32) -> @location(0) vec4f {
    let shadow = shadows[index];
    let sigma = shadow.shape.z * 0.5;
    let inside = coverage(distanceTo(pixel, shadow.rect, shadow.radii));
    var value = 0.0;
    if (shadow.flags.x < 0.5) {
        let grown = vec4f(shadow.rect.xy + shadow.shape.xy - vec2f(shadow.shape.w),
                          shadow.rect.zw + vec2f(2.0 * shadow.shape.w));
        value = blurred(pixel, grown, max(shadow.radii + vec4f(shadow.shape.w), vec4f(0.0)), sigma) * (1.0 - inside);
    } else {
        let shrunk = vec4f(shadow.rect.xy + shadow.shape.xy + vec2f(shadow.shape.w),
                           max(shadow.rect.zw - vec2f(2.0 * shadow.shape.w), vec2f(0.0)));
        value = inside * (1.0 - blurred(pixel, shrunk, max(shadow.radii - vec4f(shadow.shape.w), vec4f(0.0)), sigma));
    }
    var color = shadow.color * value;
    var clip = i32(shadow.flags.y);
    for (var depth = 0; depth < kDeepestClip && clip > 0; depth++) {
        let kept = coverage(distanceTo(pixel, clips[clip].rect, clips[clip].radii));
        color *= select(kept, 1.0 - kept, clips[clip].link.y > 0.5);
        clip = i32(clips[clip].link.x);
    }
    return color;
}
