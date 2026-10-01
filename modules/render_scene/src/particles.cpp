#include "particles.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <vector>

namespace rawframe::render_scene {

namespace {

/// An sRGB color and its linear alpha.
std::array<float, 4> colorAndAlpha(std::uint32_t color) noexcept {
    const Vector kColor = colorOf(color);
    return {kColor[0], kColor[1], kColor[2], static_cast<float>(color & 0xFFU) / 255.0F};
}

/// A time on the particle clock, wrapped into its period.
float wrapped(double seconds) noexcept {
    const double kWrapped = std::fmod(seconds, double{kParticleClockPeriod});
    return static_cast<float>(kWrapped < 0 ? kWrapped + kParticleClockPeriod : kWrapped);
}

/// An emitter's seed when it names none: its identity, mixed.
std::uint32_t seedOf(std::uint64_t key) noexcept {
    std::uint64_t mixed = key + 0x9e3779b97f4a7c15ULL;
    mixed = (mixed ^ (mixed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94d049bb133111ebULL;
    return static_cast<std::uint32_t>(mixed ^ (mixed >> 31U)) | 1U;
}

/// An emitter kept for the view: what it is, how far it is, and its values
/// held to the limit points.
struct Kept {
    const EmitterInstance* instance = nullptr;
    float distance = 0;
    float rate = 0;
    float lifetime = 0;
    std::uint32_t capacity = 0;
};

} // namespace

void spawnParticles(SceneFrame& frame,
                    std::span<const EmitterInstance> emitters,
                    const SceneCamera& camera,
                    const std::array<Vector, 3>& axes,
                    const ViewShape& view,
                    const std::map<std::uint64_t, std::uint32_t>& materials,
                    const SceneLimits& limits,
                    double clock,
                    float elapsed,
                    std::uint64_t frameIndex,
                    std::map<EmitterKey, EmitterHistory>& histories) {
    frame.emitters.clear();
    frame.emittersLeftOut = 0;
    frame.emittersHeld = 0;
    frame.particlesLeftOut = 0;
    frame.particleClock = wrapped(clock);
    std::vector<Kept> kept;
    std::set<EmitterKey> present;
    for (const EmitterInstance& kInstance : emitters) {
        const ParticleEmitter& kEmitter = kInstance.emitter;
        present.insert({kInstance.entity, kInstance.component});
        const bool kFinite = std::ranges::all_of(std::array{kEmitter.rate,
                                                            kEmitter.lifetime,
                                                            kEmitter.speed,
                                                            kEmitter.spread,
                                                            kEmitter.radius,
                                                            kEmitter.sizeStart,
                                                            kEmitter.sizeEnd,
                                                            kEmitter.accelerationX,
                                                            kEmitter.accelerationY,
                                                            kEmitter.accelerationZ,
                                                            kEmitter.drag,
                                                            kEmitter.variation},
                                                 [](float value) {
                                                     return std::isfinite(value);
                                                 }) &&
                             std::ranges::all_of(kInstance.position,
                                                 [](double value) {
                                                     return std::isfinite(value);
                                                 }) &&
                             std::ranges::all_of(kInstance.rotation, [](float value) {
                                 return std::isfinite(value);
                             });
        if (!kFinite) {
            ++frame.emittersLeftOut;
            continue;
        }
        // Nothing to show: no particles, no life, or no size.
        const float kVariation = std::clamp(kEmitter.variation, 0.0F, 1.0F);
        const float kRate = std::clamp(kEmitter.rate, 0.0F, limits.maximumParticleRate);
        const float kLifetime = std::min(kEmitter.lifetime, limits.maximumParticleLifetime);
        if ((kRate <= 0 && kEmitter.burstCount == 0) || kLifetime <= 0 ||
            (kEmitter.sizeStart <= 0 && kEmitter.sizeEnd <= 0)) {
            continue;
        }
        bool held = kEmitter.rate > limits.maximumParticleRate || kEmitter.lifetime > limits.maximumParticleLifetime;
        const double kWanted =
            std::ceil(double{kRate} * kLifetime * (1 + kVariation)) + static_cast<double>(kEmitter.burstCount);
        held = held || kWanted > limits.maximumParticlesPerEmitter;
        const auto kCapacity =
            static_cast<std::uint32_t>(std::min(kWanted, static_cast<double>(limits.maximumParticlesPerEmitter)));
        // How far its particles may reach from where it is: its sphere,
        // the farthest they travel, and their size.
        const float kAcceleration = std::hypot(kEmitter.accelerationX, kEmitter.accelerationY, kEmitter.accelerationZ);
        const float kLongest = kLifetime * (1 + kVariation);
        const float kReach = std::max(kEmitter.radius, 0.0F) +
                             (std::abs(kEmitter.speed) * (1 + kVariation) * kLongest) +
                             (0.5F * kAcceleration * kLongest * kLongest) +
                             (std::max(kEmitter.sizeStart, kEmitter.sizeEnd) * (1 + kVariation));
        const Vector kCenter = {static_cast<float>(kInstance.position[0] - camera.eye[0]),
                                static_cast<float>(kInstance.position[1] - camera.eye[1]),
                                static_cast<float>(kInstance.position[2] - camera.eye[2])};
        if (kCapacity == 0 || outsideView(axes, view, kCenter, kReach)) {
            continue;
        }
        frame.emittersHeld += held ? 1 : 0;
        kept.push_back(Kept{.instance = &kInstance,
                            .distance = std::hypot(kCenter[0], kCenter[1], kCenter[2]),
                            .rate = kRate,
                            .lifetime = kLifetime,
                            .capacity = kCapacity});
    }
    // An emitter gone keeps nothing.
    std::erase_if(histories, [&present](const auto& each) {
        return !present.contains(each.first);
    });
    // The nearest up to the limit, drawn farthest first.
    std::ranges::stable_sort(kept, {}, &Kept::distance);
    if (kept.size() > limits.maximumEmitters) {
        frame.emittersLeftOut += kept.size() - limits.maximumEmitters;
        kept.resize(limits.maximumEmitters);
    }
    std::ranges::reverse(kept);
    const float kElapsed = std::isfinite(elapsed) ? std::clamp(elapsed, 0.0F, 0.25F) : 0.0F;
    for (const Kept& kKept : kept) {
        const EmitterInstance& kInstance = *kKept.instance;
        const ParticleEmitter& kEmitter = kInstance.emitter;
        auto [history, fresh] = histories.try_emplace(EmitterKey{kInstance.entity, kInstance.component});
        EmitterHistory& remembered = history->second;
        const double kAway = std::hypot(kInstance.position[0] - remembered.anchor[0],
                                        kInstance.position[1] - remembered.anchor[1],
                                        kInstance.position[2] - remembered.anchor[2]);
        // A new ring where it is new, its ring's size changes, or it has
        // gone a kilometer from its anchor.
        if (fresh || remembered.capacity != kKept.capacity || kAway > 1000) {
            remembered = EmitterHistory{.anchor = kInstance.position,
                                        .bursts = kEmitter.bursts,
                                        .capacity = kKept.capacity,
                                        .drawn = remembered.drawn,
                                        .ring = remembered.ring + 1};
        }
        if (remembered.drawn + 1 != frameIndex) {
            remembered.owed = 0;
        }
        remembered.owed += double{kKept.rate} * kElapsed;
        auto steady = static_cast<std::uint64_t>(std::floor(remembered.owed));
        remembered.owed -= static_cast<double>(steady);
        const std::uint64_t kBurst =
            std::uint64_t{kEmitter.bursts - remembered.bursts} * std::uint64_t{kEmitter.burstCount};
        remembered.bursts = kEmitter.bursts;
        const std::uint64_t kWanted = steady + kBurst;
        if (kWanted > kKept.capacity) {
            frame.particlesLeftOut += static_cast<std::size_t>(kWanted - kKept.capacity);
        }
        steady = std::min<std::uint64_t>(steady, kKept.capacity);
        const std::uint64_t kSpawned = std::min<std::uint64_t>(kWanted, kKept.capacity);
        const std::uint64_t kKey = (std::uint64_t{kInstance.entity.slot} << 32U) |
                                   ((std::uint64_t{kInstance.entity.generation} & 0xFFFFFFU) << 8U) |
                                   (kInstance.component & 0xFFU);
        const auto kMaterial = materials.find(kEmitter.material);
        const Vector kWay = turnOf(kInstance.rotation)[1];
        const float kVariation = std::clamp(kEmitter.variation, 0.0F, 1.0F);
        SceneEmitter made{.key = kKey,
                          .material = kMaterial != materials.end() ? kMaterial->second : 0,
                          .anchor = {static_cast<float>(remembered.anchor[0] - camera.eye[0]),
                                     static_cast<float>(remembered.anchor[1] - camera.eye[1]),
                                     static_cast<float>(remembered.anchor[2] - camera.eye[2])},
                          .origin = {static_cast<float>(kInstance.position[0] - remembered.anchor[0]),
                                     static_cast<float>(kInstance.position[1] - remembered.anchor[1]),
                                     static_cast<float>(kInstance.position[2] - remembered.anchor[2])},
                          .direction = normalized(kWay),
                          .lifetime = kKept.lifetime,
                          .speed = kEmitter.speed,
                          .spread = std::clamp(kEmitter.spread, 0.0F, std::numbers::pi_v<float>),
                          .radius = std::max(kEmitter.radius, 0.0F),
                          .sizeStart = std::max(kEmitter.sizeStart, 0.0F),
                          .sizeEnd = std::max(kEmitter.sizeEnd, 0.0F),
                          .colorStart = colorAndAlpha(kEmitter.colorStart),
                          .colorEnd = colorAndAlpha(kEmitter.colorEnd),
                          .acceleration = {kEmitter.accelerationX, kEmitter.accelerationY, kEmitter.accelerationZ},
                          .drag = std::max(kEmitter.drag, 0.0F),
                          .variation = kVariation,
                          .capacity = kKept.capacity,
                          .first = remembered.next,
                          .spawned = static_cast<std::uint32_t>(kSpawned),
                          .steady = static_cast<std::uint32_t>(steady),
                          .born = wrapped(clock - kElapsed),
                          .step = steady > 0 ? kElapsed / static_cast<float>(steady) : 0.0F,
                          .seed = kEmitter.seed != 0 ? kEmitter.seed : seedOf(kKey),
                          .ring = remembered.ring};
        remembered.next = static_cast<std::uint32_t>((remembered.next + kSpawned) % kKept.capacity);
        remembered.drawn = frameIndex;
        frame.emitters.push_back(made);
    }
}

} // namespace rawframe::render_scene
