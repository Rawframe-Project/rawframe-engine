// The 3D scene's models (D284, D289, D290, D291, D292, D293, D309, D310,
// D327), for WebGPU: the entries of scene.vert, scene.frag,
// scene.cut.frag, scene.normal.frag, and scene.cutnormal.frag.

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
    environment: vec4f,
    irradiance: array<vec4f, 9>,
    occlusion: vec4f,
    reflections: vec4f,
    contact: vec4f,
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
@group(0) @binding(4) var<storage, read> ranges: array<vec4u>;
@group(0) @binding(5) var<storage, read> indices: array<u32>;
@group(0) @binding(6) var lightShadowMap: texture_depth_2d;
@group(0) @binding(7) var<storage, read> slots: array<ShadowSlot>;
@group(0) @binding(8) var<storage, read> exposure: vec4f;
// Every material's blob (D303), nine vectors each.
@group(0) @binding(9) var<storage, read> materials: array<vec4f>;
// The texture the draw's material samples, and how (D309).
@group(0) @binding(10) var baseTexture: texture_2d<f32>;
@group(0) @binding(11) var baseSampler: sampler;
// The packed and the emission textures (D312).
@group(0) @binding(12) var packedTexture: texture_2d<f32>;
@group(0) @binding(13) var packedSampler: sampler;
@group(0) @binding(14) var emissionTexture: texture_2d<f32>;
@group(0) @binding(15) var emissionSampler: sampler;
// The normal texture (D313).
@group(0) @binding(16) var normalTexture: texture_2d<f32>;
@group(0) @binding(17) var normalSampler: sampler;
// The sky's picture (D322).
@group(0) @binding(18) var environmentTexture: texture_cube<f32>;
@group(0) @binding(19) var environmentSampler: sampler;
// What of the light from all around reaches each texel (D327).
@group(0) @binding(21) var occlusionTexture: texture_2d<f32>;
// What each texel's reflection met, and how much (D331).
@group(0) @binding(22) var reflectionTexture: texture_2d<f32>;
// How much of the sun's light the contact shadows let through (D338).
@group(0) @binding(23) var contactTexture: texture_2d<f32>;

// What each model reflects (D325): the sky's picture first, then each
// reflection probe; scene.frag's Probe.
struct Probe {
    place: vec4f,
    extent: vec4f,
    light: vec4f,
}

@group(0) @binding(20) var<storage, read> probes: array<Probe>;

// Where a reflection meets the probe's box, as seen from its middle:
// scene.frag's projected.
fn projected(probe: Probe, placed: vec3f, mirrored: vec3f) -> vec3f {
    let local = clamp(placed - probe.place.xyz, -probe.extent.xyz, probe.extent.xyz);
    let far = (probe.extent.xyz - local * sign(mirrored)) / max(abs(mirrored), vec3f(1e-5));
    return local + mirrored * min(min(far.x, far.y), far.z);
}

// A texture's channel a number is read from: one to four, red to alpha;
// nought for none, which reads one.
fn channelOf(texel: vec4f, channel: f32) -> f32 {
    if (channel < 0.5) {
        return 1.0;
    }
    return texel[min(u32(channel + 0.5), 4u) - 1u];
}

struct Placed {
    @invariant @builtin(position) position: vec4f,
    @location(0) normal: vec3f,
    @location(1) color: vec4f,
    @location(2) placed: vec3f,
    @location(3) now: vec3f,
    @location(4) before: vec3f,
    @location(5) @interpolate(flat) material: u32,
    @location(6) uv: vec2f,
    @location(7) tangent: vec4f,
    @location(8) @interpolate(flat) probe: u32,
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
      @location(12) material: f32, @location(13) uv: vec2f, @location(14) tangent: vec4f,
      @location(15) probe: f32) -> Placed {
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
    out.probe = u32(probe);
    let turned = vec3f(dot(model0.xyz, tangent.xyz), dot(model1.xyz, tangent.xyz), dot(model2.xyz, tangent.xyz));
    let mirror = select(1.0, -1.0, dot(model0.xyz, cross(model1.xyz, model2.xyz)) < 0.0);
    out.tangent = vec4f(turned, tangent.w * mirror);
    return out;
}

const kPi = 3.14159265;

struct Surface {
    diffuse: vec3f,
    headOn: vec3f,
    roughness: f32,
}

fn irradianceAt(normal: vec3f) -> vec3f {
    let sum = frame.irradiance[0].rgb * 0.282095 +
              (frame.irradiance[1].rgb * normal.y + frame.irradiance[2].rgb * normal.z +
               frame.irradiance[3].rgb * normal.x) * 0.488603 +
              (frame.irradiance[4].rgb * (normal.x * normal.y) + frame.irradiance[5].rgb * (normal.y * normal.z) +
               frame.irradiance[7].rgb * (normal.x * normal.z)) * 1.092548 +
              frame.irradiance[6].rgb * (0.315392 * (3.0 * normal.z * normal.z - 1.0)) +
              frame.irradiance[8].rgb * (0.546274 * (normal.x * normal.x - normal.y * normal.y));
    return max(sum, vec3f(0.0));
}

fn environmentBrdf(roughness: f32, nv: f32) -> vec2f {
    let fit = roughness * vec4f(-1.0, -0.0275, -0.572, 0.022) + vec4f(1.0, 0.0425, 1.04, -0.04);
    let a = min(fit.x * fit.x, exp2(-9.28 * nv)) * fit.x + fit.y;
    return vec2f(-1.04, 1.04) * a + fit.zw;
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

// scene.frag's Taps and tapsAbout (D330).
struct Taps {
    at: array<vec2f, 9>,
    weight: array<f32, 9>,
}

fn tapsAbout(texel: vec2f, size: vec2f) -> Taps {
    let base = floor(texel + 0.5);
    let into = texel + 0.5 - base;
    let us = vec3f(4.0 - 3.0 * into.x, 7.0, 1.0 + 3.0 * into.x);
    let vs = vec3f(4.0 - 3.0 * into.y, 7.0, 1.0 + 3.0 * into.y);
    let u = vec3f((3.0 - 2.0 * into.x) / us.x - 2.0, (3.0 + into.x) / us.y, into.x / us.z + 2.0);
    let v = vec3f((3.0 - 2.0 * into.y) / vs.x - 2.0, (3.0 + into.y) / vs.y, into.y / vs.z + 2.0);
    var made: Taps;
    for (var row = 0; row < 3; row++) {
        for (var column = 0; column < 3; column++) {
            made.at[row * 3 + column] = (base - 0.5 + vec2f(u[column], v[row])) / size;
            made.weight[row * 3 + column] = us[column] * vs[row] / 144.0;
        }
    }
    return made;
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
    let soft = frame.shadow.w > 0.5;
    let halfTexel = select(0.5, 3.0, soft) / frame.shadow.z;
    let inSquare = clamp(vec2f(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5), vec2f(halfTexel), vec2f(1.0 - halfTexel));
    let inMap = (inSquare + vec2f(f32(at % 2), f32(at / 2))) * 0.5;
    var lit = 0.0;
    if (soft) {
        let size = vec2f(textureDimensions(shadowMap));
        let taps = tapsAbout(inMap * size, size);
        for (var tap = 0; tap < 9; tap++) {
            lit += taps.weight[tap] * textureSampleCompareLevel(shadowMap, shadowSampler, taps.at[tap], clip.z);
        }
    } else {
        lit = textureSampleCompareLevel(shadowMap, shadowSampler, inMap, clip.z);
    }
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
    let soft = frame.shadow.w > 0.5;
    let halfTexel = slot.position.w * select(0.25, 1.5, soft) / slot.right.w;
    let inSquare = clamp(vec2f(seen.x * 0.5 + 0.5, 0.5 - seen.y * 0.5), vec2f(halfTexel), vec2f(1.0 - halfTexel));
    let inAtlas = slot.rect.xy + inSquare * slot.rect.z;
    if (!soft) {
        return textureSampleCompareLevel(lightShadowMap, shadowSampler, inAtlas, near / ahead);
    }
    let size = vec2f(textureDimensions(lightShadowMap));
    let taps = tapsAbout(inAtlas * size, size);
    var lit = 0.0;
    for (var tap = 0; tap < 9; tap++) {
        lit += taps.weight[tap] * textureSampleCompareLevel(lightShadowMap, shadowSampler, taps.at[tap], near / ahead);
    }
    return lit;
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
fn fs(@builtin(position) position: vec4f, @location(0) normal: vec3f, @location(1) color: vec4f, @location(2) placed: vec3f,
      @location(3) now: vec3f, @location(4) before: vec3f, @location(5) @interpolate(flat) material: u32,
      @location(6) uv: vec2f, @location(7) tangent: vec4f, @location(8) @interpolate(flat) probe: u32) -> Shaded {
    let toEye = normalize(-placed);
    let at = min(material, arrayLength(&materials) / 9u - 1u) * 9u;
    let base = materials[at];
    let specular = materials[at + 1u];
    let emission = materials[at + 2u];
    let rest = materials[at + 3u];
    let baseMap = materials[at + 4u];
    let packedMap = materials[at + 5u];
    let emissionMap = materials[at + 6u];
    let normalMap = materials[at + 7u];
    let channels = materials[at + 8u];
    let flags = u32(rest.w);
    // The coordinates' derivatives, taken before anything branches, so a
    // texture is sampled only where its material has one.
    let dx = dpdx(uv);
    let dy = dpdy(uv);
    var sampled = vec4f(1.0);
    if ((flags & 6u) != 0u) {
        sampled = textureSampleGrad(baseTexture, baseSampler, uv * baseMap.xy + baseMap.zw, dx * baseMap.xy,
                                    dy * baseMap.xy);
    }
    var packed = vec4f(1.0);
    if (channels.x + channels.y + channels.z > 0.5) {
        packed = textureSampleGrad(packedTexture, packedSampler, uv * packedMap.xy + packedMap.zw, dx * packedMap.xy,
                                   dy * packedMap.xy);
    }
    var glow = vec3f(1.0);
    if ((flags & 8u) != 0u) {
        glow = textureSampleGrad(emissionTexture, emissionSampler, uv * emissionMap.xy + emissionMap.zw,
                                 dx * emissionMap.xy, dy * emissionMap.xy).rgb;
    }
    var n = normalize(normal);
    if ((flags & 16u) != 0u) {
        var bent = textureSampleGrad(normalTexture, normalSampler, uv * normalMap.xy + normalMap.zw, dx * normalMap.xy,
                                     dy * normalMap.xy).xyz * 2.0 - 1.0;
        bent = vec3f(bent.xy * channels.w, bent.z);
        let across = normalize(tangent.xyz - n * dot(n, tangent.xyz));
        let up = cross(n, across) * tangent.w;
        n = normalize(across * bent.x + up * bent.y + n * bent.z);
    }
    let tinted = color.rgb * base.rgb * select(vec3f(1.0), sampled.rgb, (flags & 2u) != 0u);
    let opacity = rest.x * color.a * select(1.0, sampled.a, (flags & 4u) != 0u);
    let metalness = base.w * channelOf(packed, channels.x);
    let roughness = specular.w * channelOf(packed, channels.y);
    let occlusion = rest.y * channelOf(packed, channels.z);
    var out: Shaded;
    out.motion = (now.xy / now.z - before.xy / before.z) * vec2f(0.5, -0.5);
    if ((flags & 1u) != 0u) {
        out.color = vec4f(tinted, opacity);
        return out;
    }
    let reflectance = (emission.w - 1.0) / (emission.w + 1.0);
    let surface = Surface(tinted * (1.0 - metalness), mix(reflectance * reflectance * specular.rgb, tinted, metalness),
                          roughness);
    var contacted = 1.0;
    if (frame.contact.x > 0.5 && opacity >= 0.999) {
        contacted = textureLoad(contactTexture, vec2i(position.xy), 0).r;
    }
    let direct = frame.sun.rgb * sunlit(placed, n) * contacted * reflected(surface, n, toEye, frame.toSun.xyz) +
                 punctual(placed, n, surface, toEye);
    let nv = max(dot(n, toEye), 0.0);
    let sheen = surface.headOn + (max(vec3f(1.0 - surface.roughness), surface.headOn) - surface.headOn) *
                                     pow(1.0 - nv, 5.0);
    let mirrored = reflect(-toEye, n);
    var around = mix(frame.ground.rgb, frame.sky.rgb, 0.5 + 0.5 * n.y);
    if (frame.environment.w > 0.5) {
        around = frame.sky.rgb * irradianceAt(n);
    }
    var weight = sheen;
    var incoming = mix(frame.ground.rgb, frame.sky.rgb, 0.5 + 0.5 * mirrored.y);
    let chosen = probes[min(probe, arrayLength(&probes) - 1u)];
    if (chosen.place.w > 0.5) {
        let toward = select(mirrored, projected(chosen, placed, mirrored), chosen.extent.x > 0.0);
        let picture = textureSampleLevel(environmentTexture, environmentSampler, toward,
                                         surface.roughness * (chosen.place.w - 1.0)).rgb;
        let scaleBias = environmentBrdf(surface.roughness, nv);
        weight = surface.headOn * scaleBias.x + scaleBias.y;
        incoming = chosen.light.rgb * picture;
    }
    if (frame.reflections.x > 0.5 && opacity >= 0.999) {
        let met = textureLoad(reflectionTexture, vec2i(position.xy), 0);
        incoming = mix(incoming, met.rgb / max(exposure.y, 1e-12), met.a);
    }
    let along = weight * incoming;
    var reaches = 1.0;
    if (frame.occlusion.x > 0.5 && opacity >= 0.999) {
        reaches = textureLoad(occlusionTexture, vec2i(position.xy) / 2, 0).r;
    }
    let sky = occlusion * reaches * ((1.0 - sheen) * surface.diffuse * around + along);
    out.color = vec4f((direct + sky + emission.rgb * glow) * exposure.y, opacity);
    return out;
}

// The masked models in the depth prepass (D310): scene.cut.frag.
@fragment
fn cut(@location(1) color: vec4f, @location(5) @interpolate(flat) material: u32, @location(6) uv: vec2f) {
    let at = min(material, arrayLength(&materials) / 9u - 1u) * 9u;
    let rest = materials[at + 3u];
    let mapped = materials[at + 4u];
    let sampled = textureSample(baseTexture, baseSampler, uv * mapped.xy + mapped.zw);
    let opacity = rest.x * color.a * select(1.0, sampled.a, (u32(rest.w) & 4u) != 0u);
    if (opacity < rest.z) {
        discard;
    }
}

// The models' surfaces in the prepass when a screen-space effect asks
// (D327): scene.normal.frag.
@fragment
fn normal(@location(0) normal: vec3f, @location(2) placed: vec3f, @location(5) @interpolate(flat) material: u32)
    -> @location(0) vec4f {
    let at = min(material, arrayLength(&materials) / 9u - 1u) * 9u;
    let n = normalize(normal);
    return vec4f(select(n, -n, dot(n, placed) > 0.0), materials[at + 1u].w);
}

// The masked models' surfaces in the prepass: scene.cutnormal.frag.
@fragment
fn cutNormal(@location(0) normal: vec3f, @location(1) color: vec4f, @location(2) placed: vec3f,
             @location(5) @interpolate(flat) material: u32, @location(6) uv: vec2f) -> @location(0) vec4f {
    let at = min(material, arrayLength(&materials) / 9u - 1u) * 9u;
    let rest = materials[at + 3u];
    let mapped = materials[at + 4u];
    let sampled = textureSample(baseTexture, baseSampler, uv * mapped.xy + mapped.zw);
    let opacity = rest.x * color.a * select(1.0, sampled.a, (u32(rest.w) & 4u) != 0u);
    if (opacity < rest.z) {
        discard;
    }
    let n = normalize(normal);
    return vec4f(select(n, -n, dot(n, placed) > 0.0), materials[at + 1u].w);
}
