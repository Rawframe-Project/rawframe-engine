// The 3D scene's models (D284, D289, D290, D291, D292, D293, D309), for WebGPU: the entries of
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
    clusterGrid: vec4f,
    clusterDepth: vec4f,
    unjittered: mat4x4f,
    previous: mat4x4f,
    ground: vec4f,
}

struct Light {
    placeRange: vec4f,
    intensity: vec4f,
    direction: vec4f,
    cone: vec4f,
    shadow: vec4f,
}

struct ShadowSlot {
    rect: vec4f,
    right: vec4f,
    up: vec4f,
    forward: vec4f,
    position: vec4f,
}

@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var shadowMap: texture_depth_2d;
@group(0) @binding(2) var shadowSampler: sampler_comparison;
@group(0) @binding(3) var<storage, read> lights: array<Light>;
@group(0) @binding(4) var<storage, read> ranges: array<vec2u>;
@group(0) @binding(5) var<storage, read> indices: array<u32>;
@group(0) @binding(6) var lightShadowMap: texture_depth_2d;
@group(0) @binding(7) var<storage, read> slots: array<ShadowSlot>;
@group(0) @binding(8) var<storage, read> exposure: vec4f;
// Every material's blob (D303), four vectors each.
@group(0) @binding(9) var<storage, read> materials: array<vec4f>;
// The texture the draw's material samples, and how (D309).
@group(0) @binding(10) var baseTexture: texture_2d<f32>;
@group(0) @binding(11) var baseSampler: sampler;

struct Placed {
    @invariant @builtin(position) position: vec4f,
    @location(0) normal: vec3f,
    @location(1) color: vec4f,
    @location(2) placed: vec3f,
    @location(3) now: vec3f,
    @location(4) before: vec3f,
    @location(5) @interpolate(flat) material: u32,
    @location(6) uv: vec2f,
}

struct Shaded {
    @location(0) color: vec4f,
    @location(1) motion: vec2f,
}

@vertex
fn vs(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) model0: vec4f,
      @location(3) model1: vec4f, @location(4) model2: vec4f, @location(5) normal0: vec3f,
      @location(6) normal1: vec3f, @location(7) normal2: vec3f, @location(8) color: vec4f,
      @location(9) previous0: vec4f, @location(10) previous1: vec4f, @location(11) previous2: vec4f,
      @location(12) material: f32, @location(13) uv: vec2f) -> Placed {
    let vertex = vec4f(position, 1.0);
    let placed = vec3f(dot(model0, vertex), dot(model1, vertex), dot(model2, vertex));
    var out: Placed;
    out.position = frame.viewProjection * vec4f(placed, 1.0);
    out.normal = mat3x3f(normal0, normal1, normal2) * normal;
    out.color = color;
    out.placed = placed;
    let was = vec3f(dot(previous0, vertex), dot(previous1, vertex), dot(previous2, vertex));
    out.now = (frame.unjittered * vec4f(placed, 1.0)).xyw;
    out.before = (frame.previous * vec4f(was, 1.0)).xyw;
    out.material = u32(material);
    out.uv = uv;
    return out;
}

const kPi = 3.14159265;

struct Surface {
    diffuse: vec3f,
    headOn: vec3f,
    roughness: f32,
}

fn reflected(surface: Surface, normal: vec3f, toEye: vec3f, toward: vec3f) -> vec3f {
    let nl = max(dot(normal, toward), 0.0);
    if (nl <= 0.0) {
        return vec3f(0.0);
    }
    let halfway = normalize(toEye + toward);
    let nv = max(dot(normal, toEye), 1e-4);
    let nh = max(dot(normal, halfway), 0.0);
    let alpha = max(surface.roughness * surface.roughness, 1e-3);
    let alpha2 = alpha * alpha;
    let fresnel = surface.headOn + (1.0 - surface.headOn) * pow(1.0 - max(dot(toEye, halfway), 0.0), 5.0);
    let spread = nh * nh * (alpha2 - 1.0) + 1.0;
    let distribution = alpha2 / (kPi * spread * spread);
    let visibility = 0.5 / (nl * sqrt(nv * nv * (1.0 - alpha2) + alpha2) + nv * sqrt(nl * nl * (1.0 - alpha2) + alpha2));
    return ((1.0 - fresnel) * surface.diffuse / kPi + fresnel * (distribution * visibility)) * nl;
}

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

fn lightShadow(slot: ShadowSlot, placed: vec3f, normal: vec3f) -> f32 {
    let near = slot.rect.w;
    let away = max(dot(placed - slot.position.xyz, slot.forward.xyz), near);
    let fromLight = placed + normal * (slot.position.w * away * 1.5) - slot.position.xyz;
    let ahead = dot(fromLight, slot.forward.xyz);
    if (ahead <= near) {
        return 1.0;
    }
    let seen = vec2f(dot(fromLight, slot.right.xyz), dot(fromLight, slot.up.xyz)) / (ahead * slot.right.w);
    let halfTexel = slot.position.w * 0.25 / slot.right.w;
    let inSquare = clamp(vec2f(seen.x * 0.5 + 0.5, 0.5 - seen.y * 0.5), vec2f(halfTexel), vec2f(1.0 - halfTexel));
    let inAtlas = slot.rect.xy + inSquare * slot.rect.z;
    return textureSampleCompareLevel(lightShadowMap, shadowSampler, inAtlas, near / ahead);
}

fn punctual(placed: vec3f, normal: vec3f, surface: Surface, toEye: vec3f) -> vec3f {
    if (frame.clusterGrid.w == 0.0) {
        return vec3f(0.0);
    }
    let clip = frame.unjittered * vec4f(placed, 1.0);
    let seen = clip.xy / clip.w;
    let grid = vec3u(frame.clusterGrid.xyz);
    let x = min(u32(max((seen.x * 0.5 + 0.5) * frame.clusterGrid.x, 0.0)), grid.x - 1u);
    let y = min(u32(max((0.5 - seen.y * 0.5) * frame.clusterGrid.y, 0.0)), grid.y - 1u);
    let ahead = dot(placed, frame.forward.xyz);
    var slice = 0u;
    if (ahead > frame.clusterDepth.x) {
        slice = min(u32(log(ahead / frame.clusterDepth.x) * frame.clusterDepth.y), grid.z - 1u);
    }
    let range = ranges[(slice * grid.y + y) * grid.x + x];
    var sum = vec3f(0.0);
    for (var at = range.x; at < range.x + range.y; at += 1u) {
        let light = lights[indices[at]];
        let toLight = light.placeRange.xyz - placed;
        let square = dot(toLight, toLight);
        let toward = toLight * inverseSqrt(max(square, 1e-8));
        let reached = square / (light.placeRange.w * light.placeRange.w);
        let window = clamp(1.0 - reached * reached, 0.0, 1.0);
        var falloff = window * window / max(square, 1e-4);
        if (light.intensity.w > 0.5) {
            let cone = clamp((dot(-toward, light.direction.xyz) - light.cone.y) / (light.cone.x - light.cone.y),
                             0.0, 1.0);
            falloff *= cone * cone;
        }
        if (light.shadow.y > 0.5 && falloff > 0.0) {
            var slot = u32(light.shadow.x);
            if (light.shadow.y > 1.5) {
                let axes = abs(toLight);
                if (axes.x >= axes.y && axes.x >= axes.z) {
                    slot += select(1u, 0u, toLight.x <= 0.0);
                } else if (axes.y >= axes.z) {
                    slot += select(3u, 2u, toLight.y <= 0.0);
                } else {
                    slot += select(5u, 4u, toLight.z <= 0.0);
                }
            }
            falloff *= lightShadow(slots[slot], placed, normal);
        }
        sum += light.intensity.rgb * falloff * reflected(surface, normal, toEye, toward);
    }
    return sum;
}

@fragment
fn fs(@location(0) normal: vec3f, @location(1) color: vec4f, @location(2) placed: vec3f,
      @location(3) now: vec3f, @location(4) before: vec3f, @location(5) @interpolate(flat) material: u32,
      @location(6) uv: vec2f) -> Shaded {
    let n = normalize(normal);
    let toEye = normalize(-placed);
    let at = min(material, arrayLength(&materials) / 4u - 1u) * 4u;
    let base = materials[at];
    let specular = materials[at + 1u];
    let emission = materials[at + 2u];
    let rest = materials[at + 3u];
    // Sampled before anything branches, so its derivatives hold.
    let sampled = textureSample(baseTexture, baseSampler, uv);
    let flags = u32(rest.w);
    let tinted = color.rgb * base.rgb * select(vec3f(1.0), sampled.rgb, (flags & 2u) != 0u);
    let opacity = rest.x * color.a * select(1.0, sampled.a, (flags & 4u) != 0u);
    var out: Shaded;
    out.motion = (now.xy / now.z - before.xy / before.z) * vec2f(0.5, -0.5);
    if ((flags & 1u) != 0u) {
        out.color = vec4f(tinted, opacity);
        return out;
    }
    let reflectance = (emission.w - 1.0) / (emission.w + 1.0);
    let surface = Surface(tinted * (1.0 - base.w), mix(reflectance * reflectance * specular.rgb, tinted, base.w),
                          specular.w);
    let direct = frame.sun.rgb * sunlit(placed, n) * reflected(surface, n, toEye, frame.toSun.xyz) +
                 punctual(placed, n, surface, toEye);
    let nv = max(dot(n, toEye), 0.0);
    let sheen = surface.headOn + (max(vec3f(1.0 - surface.roughness), surface.headOn) - surface.headOn) *
                                     pow(1.0 - nv, 5.0);
    let mirrored = reflect(-toEye, n);
    let around = mix(frame.ground.rgb, frame.sky.rgb, 0.5 + 0.5 * n.y);
    let along = mix(frame.ground.rgb, frame.sky.rgb, 0.5 + 0.5 * mirrored.y);
    let sky = rest.y * ((1.0 - sheen) * surface.diffuse * around + sheen * along);
    out.color = vec4f((direct + sky + emission.rgb) * exposure.y, opacity);
    return out;
}
